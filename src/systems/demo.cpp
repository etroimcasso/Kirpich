#include "systems/demo.h"

#include <cstdint>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/demo.h"      // kTypeADemoInputs, kTypeBDemoInputs
#include "data/misc.h"      // kDemoRecordingEnabledMagic
#include "data/tilemaps.h"  // kConfigScreenTilemap
#include "retropp/input.h"  // ActionSet, actionId, kMaxActions
#include "state/display_state.h"
#include "systems/game_context.h"
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/screen.h"        // loadScreenTilemap, loadTileSheet

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// The newly-pressed actions between two held sets: set in `now`, clear in `before`.
//
// This is the same relation the live input path derives each tick, but taken against a different
// baseline. The live path compares against the previous tick's real buttons; a demo compares against
// the previous *step of the recording*, which is the set stored on DemoState. Comparing against the
// player's buttons instead would report presses the recording never made, and would leave the live
// input path's own history holding the demo's input for the following tick.
retropp::ActionSet risingEdge(retropp::ActionSet now, retropp::ActionSet before) {
    retropp::ActionSet edge;
    for (int a = 0; a < retropp::kMaxActions; ++a) {
        const auto id = static_cast<retropp::ActionId>(a);
        edge.set(id, now.test(id) && !before.test(id));
    }
    return edge;
}

}  // namespace

std::span<const DemoInputRecord> demoTimeline(ActiveDemo demo) {
    switch (demo) {
        case ActiveDemo::TYPE_A:
            return kTypeADemoInputs;
        case ActiveDemo::TYPE_B:
            return kTypeBDemoInputs;
        case ActiveDemo::NONE:
            break;
    }
    return {};
}

void startDemo(GameContext& game) {
    GameFlowState& flow = game.flow;
    DemoState& demo = game.demo;

    // The Type A configuration, written first and unconditionally (tetris.asm:583-595). The Type B
    // branch below overwrites the parts that differ; the level stays 9 either way, because the
    // original never undoes it.
    flow.gameType = GameType::TYPE_A;
    flow.typeALevel = kDemoLevel;
    game.multiplayer.isMultiplayer = false;
    flow.numPiecesPlayed = 0;

    demo.demoHeld = retropp::ActionSet{};
    demo.framesRemaining = 0;

    // Rewinding the cursor covers both recordings: the original points it at the chosen recording's
    // base address, and here the recording is chosen by which demo is running (demoTimeline).
    demo.nextRecord = 0;

    // The alternation reads the demo that ran last and stores the one about to run (:596-614).
    ActiveDemo next = ActiveDemo::TYPE_A;
    if (demo.activeDemo == ActiveDemo::TYPE_A) {
        flow.gameType = GameType::TYPE_B;
        flow.typeBLevel = kDemoLevel;
        flow.typeBStartHeight = kTypeBDemoStartHeight;
        flow.numPiecesPlayed = kTypeBDemoFirstPiece;
        next = ActiveDemo::TYPE_B;
    }
    demo.activeDemo = next;

    flow.gameState = GameState::INIT_GAME;  // (:615-616)

    // The screen the demo starts from (:617-623). These are the first four steps of the config
    // screen's own load and nothing more — the config screen goes on to place its cursors, cue its
    // music and enter game-type selection, none of which happens here. The round init overwrites both
    // maps on the next frame, so this backdrop is on screen for exactly one frame, as it is on
    // hardware.
    loadTileSheet(game.display, TileSheet::GAMEPLAY);
    loadScreenTilemap(game.display, kConfigScreenTilemap);
    clearOamObjects(game);
    game.display.displayed = DisplayedMap::FIRST;
}

void checkForEndOfDemo(GameContext& game) {
    if (game.demo.activeDemo == ActiveDemo::NONE) {  // (:735-737)
        return;
    }

    // This runs before the input substitution, so Start here is the player's own press (:743-745).
    // The link-cable writes that bracket it announce the exit to a connected second console; they
    // have no effect on this machine's simulation and belong to the serial system.
    if (pressed(game, Action::Start)) {
        game.flow.gameState = GameState::INIT_TITLE_SCREEN;  // (:750-751)
        return;
    }

    // The recording ends when the piece count reaches the one it was made to (:754-766). The test is
    // for equality, not "at least": a count that steps past its target never ends the demo this way.
    const std::uint8_t target = (game.demo.activeDemo == ActiveDemo::TYPE_A)
                                    ? kTypeADemoEndPieceCount
                                    : kTypeBDemoEndPieceCount;
    if (game.flow.numPiecesPlayed != target) {
        return;
    }
    game.flow.gameState = GameState::INIT_TITLE_SCREEN;
}

void demoSimulateJoypad(GameContext& game) {
    DemoState& demo = game.demo;

    if (demo.activeDemo == ActiveDemo::NONE) {  // (:774-776)
        return;
    }
    if (demo.recording == kDemoRecordingEnabledMagic) {  // (:777-779)
        return;
    }

    const std::span<const DemoInputRecord> timeline = demoTimeline(demo.activeDemo);

    if (demo.framesRemaining != 0) {
        // The current step still has frames to run (:780-785). Nothing is newly pressed on these
        // frames, and the tick's pressed set says so (:808-810) — a demo's presses land only on the
        // frames a step loads.
        --demo.framesRemaining;
        game.joypad.pressed = retropp::ActionSet{};
    } else if (demo.nextRecord < timeline.size()) {
        // Load the next step (:787-806).
        const DemoInputRecord& record = timeline[demo.nextRecord];
        game.joypad.pressed = risingEdge(record.held, demo.demoHeld);
        demo.demoHeld = record.held;
        demo.framesRemaining = record.frames;
        ++demo.nextRecord;
    } else {
        // Past the end of the recording. Both demos stop on their piece count long before the cursor
        // gets here, so this is a bound rather than a behavior: hold the last input rather than read
        // past the timeline.
        game.joypad.pressed = retropp::ActionSet{};
    }

    // Park the player's real input and put the demo's in its place (:811-816).
    demo.savedHeld = game.joypad.held;
    game.joypad.held = demo.demoHeld;
}

void recordDemo(GameContext& game) {
    DemoState& demo = game.demo;

    if (demo.activeDemo == ActiveDemo::NONE) {  // (:825-827)
        return;
    }
    if (demo.recording != kDemoRecordingEnabledMagic) {  // (:828-830)
        return;
    }

    // Input unchanged: the current step simply gets longer (:856-859).
    if (demo.demoHeld == game.joypad.held) {
        ++demo.framesRemaining;
        return;
    }

    // Input changed: close the current step and open a new one (:836-851). The original writes the
    // closed step through its cursor, which addresses cartridge ROM — those two stores land nowhere,
    // so advancing the cursor is the whole of what they do.
    ++demo.nextRecord;
    demo.demoHeld = game.joypad.held;
    demo.framesRemaining = 0;
}

void restoreDemoSavedJoypad(GameContext& game) {
    if (game.demo.activeDemo == ActiveDemo::NONE) {  // (:864-866)
        return;
    }
    // Any non-zero recording flag suppresses the restore, where the two routines above test it
    // against the enable value exactly. Both readings are the original's (:867-869).
    if (game.demo.recording != 0) {
        return;
    }
    game.joypad.held = game.demo.savedHeld;  // (:870-871)
}

void startRecordingDemo(GameContext& game) {
    game.demo.recording = kDemoRecordingEnabledMagic;  // (:628-629)
}

GameplayDemoHooks demoHooks() {
    return GameplayDemoHooks{
        .checkForEndOfDemo = checkForEndOfDemo,
        .simulateJoypad = demoSimulateJoypad,
        .recordDemo = recordDemo,
        .restoreSavedJoypad = restoreDemoSavedJoypad,
    };
}

}  // namespace kirpich::systems
