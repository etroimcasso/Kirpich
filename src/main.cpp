// The boot host: Tetris, on screen.
//
// The first program in this port that opens a window and draws. It composes what the simulation
// layers already ship — the state dispatcher and every solo handler, the piece randomizer and the
// garbage fill on their shared machine, the hosted sound driver — and adds the two things a picture
// needs: the tile art, uploaded once, and a per-frame bridge that turns the board into a layer.
//
// The frame is two beats. simTick advances the machine's divider by one tick's worth of cycles and
// runs one game frame; renderLoop composes the board and submits it. The engine's run loop owns
// pacing at the true Game Boy rate, so the port sets no rate of its own.
//
// The frame draws two layers: the board as a tile layer, and the object buffer as sprites over it.
//
// The machine starts from the ported boot path (systems/boot.h), which is also what the four-button
// reset chord runs. What that path does NOT do is write the original's display, interrupt, stack and
// timer registers: this port draws through a display the engine owns and takes its frame from the
// engine's run loop, so those writes have nothing to reach. docs/contracts/boot.md §4 accounts for
// every line of the original's startup routine and what became of it.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <spdlog/spdlog.h>

#include <retropp/app_identity.h>
#include <retropp/asset_registry.h>
#include <retropp/clock.h>
#include <retropp/draw_state.h>
#include <retropp/engine_config.h>
#include <retropp/input.h>
#include <retropp/renderer.h>
#include <retropp/run_loop.h>
#include <retropp/save_store.h>
#include <retropp/sdl_platform.h>
#include <retropp/user_files.h>
#include <retropp/version.h>
#include <retropp/viewport.h>
#include <retropp/vm.h>
#include <retropp/windowed_host.h>

#include "assets/asset_root.h"
#include "assets/first_start.h"
#include "render/background.h"
#include "render/settings_overlay.h"
#include "render/sprites.h"
#include "render/tile_atlas.h"
#include "state/high_score_persistence.h"
#include "state/settings.h"
#include "systems/boot.h"
#include "systems/demo.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/high_scores.h"
#include "systems/input.h"
#include "systems/launch_scenes.h"
#include "systems/line_clear.h"
#include "systems/menu_screens.h"
#include "systems/readouts.h"
#include "systems/scoring.h"
#include "systems/settings_screen.h"
#include "systems/sound.h"
#include "systems/title_screens.h"
#include "systems/type_b_ending.h"
#include "vm/garbage_fill.h"
#include "vm/piece_random.h"

namespace {

// The faithful internal resolution, and the one the tile bridge's 20x18 window assumes.
constexpr retropp::ViewportResolution kViewport = retropp::ViewportResolution::GameBoy;

// Where LoadFromPath assets resolve from.
//
// A player's assets are their own files: extracted from their cartridge, belonging to them and to
// this machine, and they live in the per-user data directory beside their save — the same place
// UserFiles and SaveStore resolve. That is where the extractor writes and where the loaders read,
// and it is the same directory wherever the binary itself happens to sit.
//
// A development build reads its project tree instead, so a developer who has run
// scripts/setup-dev-assets exercises the shipped load path against the files in their checkout.
// That only applies to a binary still inside that tree: developmentAssetRoot decides, and
// assets/asset_root.h explains why a binary that has been moved must not keep it.
void configureAssetRoot() {
#ifdef KIRPICH_PROJECT_ROOT
    // assetRoot() is the engine's default at this point: an absolute path to the executable's
    // directory, resolved by EngineConfig::setActive. Both sides are canonicalised so that symlinks
    // and `..` cannot make an inside path look like an outside one.
    std::error_code ec;
    const std::filesystem::path here = std::filesystem::weakly_canonical(retropp::assetRoot(), ec);
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::path{KIRPICH_PROJECT_ROOT}, ec);
    if (!ec) {
        if (const auto devRoot = kirpich::assets::developmentAssetRoot(here, root)) {
            retropp::setAssetRoot(*devRoot);
            return;
        }
    }
#endif

    // The per-user directory. Resolving it creates it, so it is writable by the time the extractor
    // needs it. It can still fail — an identity the engine never published, or a platform that
    // cannot answer — and that is worth saying out loud rather than aborting on: the engine's own
    // default still resolves, so the run continues with the files beside the program instead.
    try {
        retropp::setAssetRoot(retropp::UserFiles{}.root());
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error(
            "Could not resolve the per-user data directory ({}). Falling back to the program's own "
            "directory, which works but is not where your files belong.",
            error.what());
    }
}

// What the window itself reports about being fullscreen.
//
// A player can leave fullscreen without going through the game — macOS lets a fullscreen window be
// dragged out of its Space, and every desktop has some equivalent. `Platform::fullscreen()` is the
// surface that should answer this, and it is where the answer belongs; it currently reports what the
// game last set rather than what the window is, so the game listens for SDL's own report instead.
//
// INTERIM, and only until the engine observes the change itself, which has been asked for upstream.
// When it lands, this watch and the reconciliation in the frame both come out, and the setting reads
// `platform.window().fullscreen()` instead.
struct FullscreenWatch {
    bool observed = false;  // what the window last reported
    bool changed  = false;  // set when a report has not been acted on yet
};

// Called by SDL as it pumps, on the pumping thread — the same thread the frame runs on, so the flags
// need no synchronisation. It records and returns; acting on the change is the frame's business.
bool watchFullscreen(void* user, SDL_Event* event) {
    auto& watch = *static_cast<FullscreenWatch*>(user);
    if (event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
        watch.observed = true;
        watch.changed  = true;
    } else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        watch.observed = false;
        watch.changed  = true;
    }
    return true;  // a watch observes; it never filters the event out
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    spdlog::info("kirpich 0.1.0 — Retro++ engine {}", retropp::version());

    const retropp::AppIdentity identity{.organization = std::string{kirpich::kSaveOrganization},
                                        .application  = std::string{kirpich::kSaveApplication}};

    // The player's own store, and the display choices they last made — read first, so the window can
    // be opened the way they left it rather than at the engine's default and then jumping to it. The
    // store is rooted at the directory the identity resolves to, which is the same directory a
    // default-constructed store finds once the config is published; naming it here is what lets the
    // config below be built complete and published once.
    retropp::SaveStore saves = retropp::SaveStore::atPath(retropp::userDataDir(identity));
    kirpich::Settings  settings;
    kirpich::loadSettings(saves, settings);

    const retropp::EngineConfig config{
        .identity     = identity,
        .window       = {.title = "Kirpich"},
        .viewport     = kViewport,
        .timing       = retropp::TimingProfile::GameBoy,
        .enhancements = {.windowScale = settings.windowScale,
                         .fullscreen  = settings.fullscreen},
    };
    retropp::EngineConfig::setActive(config);

    configureAssetRoot();

    // The graphics come out of the player's own cartridge, so a first launch has none. Ask for the
    // ROM and read them out of it before anything that would try to load one exists — a refusal ends
    // the run here, with nothing built.
    const bool ready = kirpich::assets::ensureAssetsPresent(
        [](const std::string& text) { spdlog::warn("{}", text); });
    if (!ready) {
        return EXIT_FAILURE;
    }

    retropp::SteadyClock clock;
    retropp::SdlPlatform platform;
    retropp::Renderer    renderer{platform.device(), platform.sdlWindow()};
    retropp::RunLoop     loop{clock};

    // ── The machine ──────────────────────────────────────────────────────────
    // ONE virtual machine, shared. The piece randomizer and the garbage fill both read the divider,
    // and a Type B round init draws its pieces and then fills its garbage in the same frame — so the
    // draws advance the divider the fill goes on to read. Registering both routines on one machine
    // reproduces that coupling; a machine each would give each its own divider and throw it away.
    // Nothing in the types enforces this, which is why it is said here as well as at both headers.
    retropp::Vm  vm{retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy};
    const auto   drawPiece = kirpich::vm::registerPieceRandom(vm);
    const auto   garbageFold = kirpich::vm::registerGarbageFold(vm);

    // ── The art ──────────────────────────────────────────────────────────────
    const kirpich::render::TileAtlas tiles = kirpich::render::uploadTileAtlas(renderer);

    // ── The game ─────────────────────────────────────────────────────────────
    kirpich::systems::GameContext game;

    // Put the display choices into effect. Fullscreen is asked for either way, so leaving it turns
    // the window back on; the size is applied only when windowed, where it is the only thing that
    // can be seen. The engine ignores a setter handed the value it already has, so this is also what
    // the startup config above did and repeating it costs nothing.
    const auto applySettings = [&platform](const kirpich::Settings& current) {
        retropp::Window& window = platform.window();
        window.fullscreen(current.fullscreen);
        if (!current.fullscreen) {
            window.size(retropp::PixelSize{kViewport.width * current.windowScale,
                                           kViewport.height * current.windowScale});
        }
    };

    kirpich::systems::GameStateDispatcher dispatcher;

    // The reset the four-button chord asks for. Both places that detect the chord fire this same
    // closure, because the original reaches one routine from both of its detection sites.
    //
    // The dispatcher's own reset goes with it: the original's startup clears the held-buttons byte, so
    // the frame after a reset derives its presses against nothing and every button still down reads as
    // freshly pressed. That is also what makes a chord held down keep resetting until it is released.
    const auto reset = [&game, &dispatcher] {
        kirpich::systems::softReset(game);
        dispatcher.reset();
    };
    dispatcher.softReset = reset;

    // Left alone, the title screen counts down and plays one of the two recorded demos.
    kirpich::systems::installTitleScreenHandlers(dispatcher, kirpich::systems::startDemo);

    // Each difficulty screen refreshes its own game type's table on the way in and on every move,
    // which is also where a just-finished round's score is compared against it and inserted.
    kirpich::systems::installMenuScreenHandlers(dispatcher,
                                                kirpich::systems::updateTypeATopScores,
                                                kirpich::systems::updateTypeBTopScores);

    // A submitted name is the point the table is worth keeping, so that is where it is written back.
    kirpich::systems::installHighScoreHandlers(
        dispatcher, [&saves](const kirpich::HighScoreState& scores) {
            kirpich::saveTopScores(scores, saves);
        });

    // The settings screen, reached from the title screen's third item and from a paused round. It
    // edits the same value the window was opened from, and writes every change out as it is made.
    kirpich::systems::installSettingsHandlers(
        dispatcher,
        kirpich::systems::SettingsWiring{
            .settings = &settings,
            .apply    = applySettings,
            .save     = [&saves](const kirpich::Settings& current) {
                kirpich::saveSettings(current, saves);
            },
            .saveScores =
                [&saves](const kirpich::HighScoreState& scores) {
                    kirpich::saveTopScores(scores, saves);
                },
            // Submitted rather than performed: the engine ends the run at the next frame boundary, so
            // the frame the player answered on finishes drawing first.
            .exit = [&loop] { loop.exitRequest(); },
        });

    kirpich::systems::SoundSystem sound;
    kirpich::systems::installSoundTick(dispatcher, sound, game);

    // The dance holds while its jingle plays, so the ending needs to ask the driver. Handing it the
    // real query is what ends the dance on a build that has sound; the default reports silence.
    kirpich::systems::installTypeBEndingHandlers(
        dispatcher, [&sound] { return sound.currentMusic().has_value(); });

    // The two bonus endings. Without these the dance's height-5 fork and the game-over chain's
    // 100 000-point fork both write a state nothing implements, and the game stops where it should
    // launch something.
    kirpich::systems::installLaunchSceneHandlers(dispatcher);

    // Both seams take the machine's raw byte source: the round's piece selection and the garbage
    // fill's per-cell pick each own their own logic and only ask the divider for a number.
    kirpich::systems::installGameplayHandlers(
        dispatcher, kirpich::systems::GameplayWiring{
                        .draw        = [&drawPiece] { return drawPiece(); },
                        .demo        = kirpich::systems::demoHooks(),
                        .initGarbage = kirpich::vm::makeInitGarbageHook(
                            [&garbageFold] { return garbageFold(); }),
                        .softReset = reset,
                    });

    // Start the machine: the boot path, then the player's saved top scores read back over the tables
    // it just cleared. bootGame owns that order — reversed, a launch would wipe the scores it had just
    // loaded — and it leaves the game at the copyright screen with the two menu selections the
    // following screens read.
    kirpich::systems::bootGame(game, saves);

    // ── Input ────────────────────────────────────────────────────────────────
    retropp::ActionMap actions = kirpich::systems::defaultActionMap();
    platform.actions(actions);

    // ── The loop ─────────────────────────────────────────────────────────────
    // Counted once per simulation tick and handed to the sprite bridge, where it goes into every
    // object's name. Objects here move a whole tile at a time and must arrive rather than glide, and
    // a name the renderer has not just seen is what tells it not to ease one frame's object into the
    // next's. See render/sprites.h.
    std::uint16_t simTicks = 0;

    // Alt+Enter, and Cmd+Enter on macOS — what people reach for without being told. It cannot be an
    // action binding: the engine's action map has no modifier concept, so the two keys are read from
    // SDL directly, which is what this port does where the engine has no opinion (the other place is
    // the first-start file dialog). Either modifier is accepted everywhere rather than one per
    // platform — nothing else in the game wants that combination. It sets the same setting the
    // screen's row sets and is written out the same way, so a player who goes fullscreen by chord and
    // quits comes back fullscreen. Held across ticks so the chord fires on the press, not every frame
    // the keys are down.
    bool fullscreenChordHeld = false;

    // Seeded with what the window was opened as, then kept current by SDL's own reports.
    FullscreenWatch fullscreen{.observed = settings.fullscreen};
    SDL_AddEventWatch(watchFullscreen, &fullscreen);

    loop.simTick([&](const retropp::InputState& in) {
        // Adopt whatever the window last reported, rather than trusting the setting to be whatever
        // the game last set it to. Adopted rather than re-applied — the window is already in this
        // state — and written out, because a player who left fullscreen by hand meant it as much as
        // one who used the settings row.
        if (fullscreen.changed) {
            fullscreen.changed = false;

            // Tell the window what it already is. The setter ignores a value equal to the last one
            // set through it, and a change made outside the game never went through it — so without
            // this the next toggle back is read as "no change" and swallowed, and the row stops
            // working until it is toggled twice. Asserting the observed value costs nothing at the
            // window (it is already in that state) and leaves the setter able to see the next change.
            platform.window().fullscreen(fullscreen.observed);

            if (settings.fullscreen != fullscreen.observed) {
                settings.fullscreen = fullscreen.observed;
                kirpich::saveSettings(settings, saves);
            }
        }

        const bool modifier = (SDL_GetModState() & (SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
        const bool chord = modifier && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RETURN];
        if (chord && !fullscreenChordHeld) {
            settings.fullscreen = !settings.fullscreen;
            applySettings(settings);
            kirpich::saveSettings(settings, saves);
        }
        fullscreenChordHeld = chord;

        // The divider free-runs with engine time: one tick's worth of cycles per sim tick keeps it
        // ticking between the draws and fills that read it. Without this it freezes and the piece
        // sequence degenerates into a counter.
        vm.advanceClock(config.timing.cpuCyclesPerTick());

        dispatcher.tick(game, kirpich::systems::heldActions(in));

        // The frame's last beat. The original runs these in its vertical-blank handler, after the
        // dispatch and the timers the dispatcher already ran (tetris.asm:214-249), and the line-clear
        // cadences are counted in that order: the flash advances a pass every ten frames from here,
        // and the field wipe steps one row per frame. Each one gates itself, so they are called every
        // frame and act only when they have something to do. Without this beat a round stops after its
        // first lock — the piece that landed never clears and the next one never spawns.
        const auto draw = [&drawPiece] { return drawPiece(); };
        kirpich::systems::animateLineClear(game, draw);
        kirpich::systems::playingFieldWipeTick(game, draw);
        kirpich::systems::updateScoreboard(game);
        kirpich::systems::redrawScore(game);
        kirpich::systems::drawTopScoresToVram(game);

        ++simTicks;
    });

    // Held across frames so each layer's borrowed span stays valid for the whole submission, and so
    // containers that never change size are not reallocated sixty times a second.
    std::vector<retropp::TileCell> cells;
    std::vector<retropp::Sprite>   sprites;

    loop.renderLoop([&] {
        kirpich::render::composeBackground(game.display, tiles, cells, settings.shadeRamp);
        kirpich::render::composeSprites(game.engine, game.oamSources, game.display.sheet, simTicks,
                                        tiles, sprites, settings.shadeRamp);

        retropp::FrameDrawState frame;
        frame.layers.push_back(kirpich::render::backgroundLayer(cells, kViewport));
        frame.layers.push_back(kirpich::render::spriteLayer(sprites, kViewport));

        // The settings screen's palette preview. It is colour rather than art, so it is drawn over
        // the composited frame instead of through a tile, and only while that screen is showing.
        if (game.flow.gameState == kirpich::GameState::SETTINGS) {
            frame.regions = kirpich::render::settingsOverlay(game.screens, settings.shadeRamp,
                                                             kViewport.width);
        }
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return EXIT_SUCCESS;
}
