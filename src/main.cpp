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
// ONE THING THIS DOES NOT DO, deliberate and visible.
//
// The real boot path. The original's startup routine is not ported, so this seeds the machine
// directly to the first screen the game shows. That is a substitution, not a port, and the state it
// skips over is named in docs/contracts/screen.md.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

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

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>

#include "assets/asset_root.h"
#include "assets/first_start.h"
#include "render/background.h"
#include "render/sprites.h"
#include "render/tile_atlas.h"
#include "state/high_score_persistence.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/high_scores.h"
#include "systems/input.h"
#include "systems/line_clear.h"
#include "systems/menu_screens.h"
#include "systems/readouts.h"
#include "systems/scoring.h"
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

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    spdlog::info("kirpich 0.1.0 — Retro++ engine {}", retropp::version());

    const retropp::EngineConfig config{
        .identity = {.organization = std::string{kirpich::kSaveOrganization},
                     .application  = std::string{kirpich::kSaveApplication}},
        .window   = {.title = "Kirpich"},
        .viewport = kViewport,
        .timing   = retropp::TimingProfile::GameBoy,
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

    // Top scores outlive a launch, so they load before the first screen reads them. An absent
    // document is an ordinary first run and leaves the boot zeros in place.
    retropp::SaveStore saves;
    kirpich::loadTopScores(saves, game.highScores);

    kirpich::systems::GameStateDispatcher dispatcher;
    kirpich::systems::installTitleScreenHandlers(dispatcher);

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

    kirpich::systems::SoundSystem sound;
    kirpich::systems::installSoundTick(dispatcher, sound, game);

    // The dance holds while its jingle plays, so the ending needs to ask the driver. Handing it the
    // real query is what ends the dance on a build that has sound; the default reports silence.
    kirpich::systems::installTypeBEndingHandlers(
        dispatcher, [&sound] { return sound.currentMusic().has_value(); });

    // Both seams take the machine's raw byte source: the round's piece selection and the garbage
    // fill's per-cell pick each own their own logic and only ask the divider for a number.
    kirpich::systems::installGameplayHandlers(
        dispatcher, kirpich::systems::GameplayWiring{
                        .draw        = [&drawPiece] { return drawPiece(); },
                        .initGarbage = kirpich::vm::makeInitGarbageHook(
                            [&garbageFold] { return garbageFold(); }),
                    });

    // The original's startup routine (_Start / Init) is not ported. Seeding the machine directly is a
    // stated substitution for it: it arrives at the copyright screen the way the game does, but
    // without having run the boot it runs to get there. Three values are what that boot leaves behind
    // and the screens that follow read (tetris.asm:371-376) — the first screen, and the two menu
    // selections whose stored values double as a cursor position and a sprite id.
    game.flow.gameType = kirpich::GameType::TYPE_A;
    game.flow.musicType = kirpich::MusicType::MUSIC_A;
    game.flow.gameState = kirpich::GameState::INIT_COPYRIGHT;

    // ── Input ────────────────────────────────────────────────────────────────
    retropp::ActionMap actions = kirpich::systems::defaultActionMap();
    platform.actions(actions);

    // ── The loop ─────────────────────────────────────────────────────────────
    // Counted once per simulation tick and handed to the sprite bridge, where it goes into every
    // object's name. Objects here move a whole tile at a time and must arrive rather than glide, and
    // a name the renderer has not just seen is what tells it not to ease one frame's object into the
    // next's. See render/sprites.h.
    std::uint16_t simTicks = 0;

    loop.simTick([&](const retropp::InputState& in) {
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
        kirpich::render::composeBackground(game.display, tiles, cells);
        kirpich::render::composeSprites(game.engine, game.oamSources, game.display.sheet, simTicks,
                                        tiles, sprites);

        retropp::FrameDrawState frame;
        frame.layers.push_back(kirpich::render::backgroundLayer(cells, kViewport));
        frame.layers.push_back(kirpich::render::spriteLayer(sprites, kViewport));
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return EXIT_SUCCESS;
}
