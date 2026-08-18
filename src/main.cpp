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
// TWO THINGS THIS DOES NOT DO, both deliberate and both visible.
//
// Sprites. The falling piece, the next-piece preview, every menu cursor and the ending's dancers are
// object-layer art, and the object layer is not bridged yet. Screens draw; the pieces you control do
// not. A round is playable in the sense that it runs, not in the sense that you can see what you are
// doing.
//
// The real boot path. The original's startup routine is not ported, so this seeds the machine
// directly to the first screen the game shows. That is a substitution, not a port, and the state it
// skips over is named in docs/contracts/screen.md.

#include <cstdlib>
#include <filesystem>
#include <string>
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
#include <retropp/version.h>
#include <retropp/viewport.h>
#include <retropp/vm.h>
#include <retropp/windowed_host.h>

#include <kirpich/game_state.h>

#include "assets/first_start.h"
#include "render/background.h"
#include "render/tile_atlas.h"
#include "state/high_score_persistence.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/input.h"
#include "systems/menu_screens.h"
#include "systems/sound.h"
#include "systems/title_screens.h"
#include "systems/type_b_ending.h"
#include "vm/garbage_fill.h"
#include "vm/piece_random.h"

namespace {

// The faithful internal resolution, and the one the tile bridge's 20x18 window assumes.
constexpr retropp::ViewportResolution kViewport = retropp::ViewportResolution::GameBoy;

// Where LoadFromPath assets resolve from. A development build points this at the project tree so the
// files scripts/setup-dev-assets writes are the ones the engine reads; a distributable leaves the
// engine's own default, the executable's directory, which is where the extractor writes.
void configureAssetRoot() {
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
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
    kirpich::systems::installMenuScreenHandlers(dispatcher);

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

    // The original's startup routine (_Start / Init) is not ported. Seeding the first screen directly
    // is a stated substitution for it: the machine arrives at the copyright screen the way the game
    // does, but without having run the boot it runs to get there.
    game.flow.gameState = kirpich::GameState::INIT_COPYRIGHT;

    // ── Input ────────────────────────────────────────────────────────────────
    retropp::ActionMap actions = kirpich::systems::defaultActionMap();
    platform.actions(actions);

    // ── The loop ─────────────────────────────────────────────────────────────
    loop.simTick([&](const retropp::InputState& in) {
        // The divider free-runs with engine time: one tick's worth of cycles per sim tick keeps it
        // ticking between the draws and fills that read it. Without this it freezes and the piece
        // sequence degenerates into a counter.
        vm.advanceClock(config.timing.cpuCyclesPerTick());

        dispatcher.tick(game, kirpich::systems::heldActions(in));
    });

    // Held across frames so the layer's borrowed span stays valid for the whole submission, and so a
    // grid that never changes size is not reallocated sixty times a second.
    std::vector<retropp::TileCell> cells;

    loop.renderLoop([&] {
        kirpich::render::composeBackground(game.field, game.display.sheet, tiles, cells);

        retropp::FrameDrawState frame;
        frame.layers.push_back(kirpich::render::backgroundLayer(cells, kViewport));
        renderer.renderFrame(frame);
    });

    retropp::WindowedHost{loop, platform}.run();
    return EXIT_SUCCESS;
}
