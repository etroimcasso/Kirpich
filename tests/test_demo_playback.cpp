// Attract-mode demo playback — behavioral tests against docs/contracts/demo-playback.md.
//
// Device-free: every routine is pure logic over the game-state aggregate. Each asserted value is traced
// to the tetris.asm lines the contract names. The frame-order and attract-countdown cases compose the
// real handlers through the gameplay frame and the state dispatcher rather than probing seams, because
// the ordering is what those cases are about.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/demo.h"      // kTypeADemoInputs, kTypeBDemoInputs
#include "data/misc.h"      // kDemoRecordingEnabledMagic
#include "data/music.h"     // MusicId
#include "data/tilemaps.h"  // kConfigScreenTilemap
#include "retropp/input.h"
#include "state/demo_state.h"
#include "state/display_state.h"
#include "state/engine_state.h"  // OamEntry
#include "state/sprite_renderer_state.h"  // kPreviewPieceSlot
#include "systems/boot.h"        // coldBoot
#include "systems/demo.h"
#include "systems/line_clear.h"  // the frame's vertical-blank beats
#include "systems/scoring.h"     // updateScoreboard
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/sound.h"  // gesturesFor
#include "systems/title_screens.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::Action;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::OamEntry;
using kirpich::TileSheet;
using kirpich::kDemoRecordingEnabledMagic;
using kirpich::kTypeADemoInputs;
using kirpich::kTypeBDemoInputs;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::checkForEndOfDemo;
using kirpich::systems::demoHooks;
using kirpich::systems::demoSimulateJoypad;
using kirpich::systems::recordDemo;
using kirpich::systems::restoreDemoSavedJoypad;
using kirpich::systems::startDemo;
using kirpich::systems::startRecordingDemo;

// Values the contract pins.
constexpr std::uint8_t kDemoLevel = 9;
constexpr std::uint8_t kTypeBStartHeight = 2;
constexpr std::uint8_t kTypeBFirstPiece = 17;
constexpr std::uint8_t kTypeAEndPieceCount = 16;
constexpr std::uint8_t kTypeBEndPieceCount = 29;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// A context with a demo already running, its cursor wherever the test needs it.
GameContext running(ActiveDemo which) {
    GameContext game;
    game.demo.activeDemo = which;
    return game;
}

}  // namespace

// ── Test 1: StartDemoAlternation ────────────────────────────────────────────────────────────────────
// StartDemo (tetris.asm:582-616): the two recordings alternate, and the branch reads the demo that ran
// LAST. From a cold start the sequence is Type A, Type B, Type A, ...
TEST(DemoPlayback, StartDemoAlternation) {
    // Cold start: nothing has run, so the Type A recording is next (:596-599).
    {
        GameContext game;
        startDemo(game);

        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_A);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
        EXPECT_EQ(game.flow.typeALevel, kDemoLevel);
        EXPECT_EQ(game.flow.numPiecesPlayed, 0);
        EXPECT_FALSE(game.multiplayer.isMultiplayer);
        EXPECT_EQ(game.demo.framesRemaining, 0);
        EXPECT_EQ(game.demo.nextRecord, 0);
        EXPECT_EQ(game.demo.demoHeld, retropp::ActionSet{});
        EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME);
    }

    // After the Type A recording, the Type B one runs, with its own level, height and piece count
    // (:600-611).
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        startDemo(game);

        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_B);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
        EXPECT_EQ(game.flow.typeBLevel, kDemoLevel);
        EXPECT_EQ(game.flow.typeBStartHeight, kTypeBStartHeight);
        EXPECT_EQ(game.flow.numPiecesPlayed, kTypeBFirstPiece);
        EXPECT_EQ(game.demo.nextRecord, 0);

        // The Type A level is written before the Type B branch and never undone, so it is still 9 on
        // a Type B demo (:586 with no counterpart in the branch).
        EXPECT_EQ(game.flow.typeALevel, kDemoLevel);
    }

    // After the Type B recording, back to Type A (:596-599 — the branch is taken only for Type A).
    {
        GameContext game = running(ActiveDemo::TYPE_B);
        startDemo(game);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_A);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
    }

    // The whole cycle from power-on, run through one object: 0 -> A -> B -> A -> B.
    {
        GameContext game;
        const ActiveDemo expected[] = {ActiveDemo::TYPE_A, ActiveDemo::TYPE_B, ActiveDemo::TYPE_A,
                                       ActiveDemo::TYPE_B};
        for (const ActiveDemo want : expected) {
            startDemo(game);
            EXPECT_EQ(game.demo.activeDemo, want);
        }
    }
}

// ── Test 2: StartDemoPresentation ───────────────────────────────────────────────────────────────────
// StartDemo's screen load (tetris.asm:617-623): the first four steps of the config screen's load and
// nothing more, plus the map select the LCD control byte carries.
TEST(DemoPlayback, StartDemoPresentation) {
    GameContext game;

    // Seed state the load must clear or replace.
    game.display.sheet = TileSheet::COPYRIGHT_TITLE;
    game.display.displayed = DisplayedMap::SECOND;
    game.engine.oam[0] = OamEntry{.y = 9, .x = 9, .tile = 9};
    game.engine.oam[39] = OamEntry{.y = 1, .x = 2, .tile = 3};

    startDemo(game);

    EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);  // LoadGameplayTiles (:618)

    // The config-screen backdrop, every visible cell (:619-620).
    for (std::size_t row = 0; row < kirpich::kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
            EXPECT_EQ(game.display.map[row][col], kirpich::kConfigScreenTilemap[row][col])
                << "row " << row << " col " << col;
        }
    }

    // ClearObjects (:621).
    EXPECT_EQ(game.engine.oam[0], OamEntry{});
    EXPECT_EQ(game.engine.oam[39], OamEntry{});

    // The LCD control byte written on the way out has the background-map bit clear, so the first map
    // is what shows (:622-623).
    EXPECT_EQ(game.display.displayed, DisplayedMap::FIRST);

    // The board is not touched — a backdrop goes to the map alone.
    EXPECT_EQ(game.field, kirpich::PlayingFieldState{});
}

// ── Test 3: ReplayEdgeLaw ───────────────────────────────────────────────────────────────────────────
// DemoSimulateJoypad (tetris.asm:780-810): a step's frame count runs down with nothing newly pressed,
// and the pressed set is derived only on the frames a step loads — against the recording's own previous
// held set, not the player's.
TEST(DemoPlayback, ReplayEdgeLaw) {
    GameContext game = running(ActiveDemo::TYPE_A);

    // Frame 1 loads record 0: no actions held, 0x2A frames to run (:787-806).
    demoSimulateJoypad(game);
    EXPECT_EQ(game.demo.nextRecord, 1);
    EXPECT_EQ(game.demo.framesRemaining, kTypeADemoInputs[0].frames);
    EXPECT_EQ(game.joypad.pressed, retropp::ActionSet{});
    EXPECT_EQ(game.joypad.held, retropp::ActionSet{});

    // The step's frames run down, one per call, and nothing is newly pressed on any of them
    // (:780-785, :808-810).
    for (std::uint8_t left = kTypeADemoInputs[0].frames; left > 0; --left) {
        demoSimulateJoypad(game);
        EXPECT_EQ(game.demo.framesRemaining, left - 1);
        EXPECT_EQ(game.joypad.pressed, retropp::ActionSet{});
        EXPECT_EQ(game.demo.nextRecord, 1) << "the cursor must not move while a step runs";
    }

    // The next call loads record 1, which presses left: a newly-held action reads as pressed.
    demoSimulateJoypad(game);
    EXPECT_EQ(game.demo.nextRecord, 2);
    EXPECT_EQ(game.joypad.pressed, actionSet({Action::MoveLeft}));
    EXPECT_EQ(game.demo.demoHeld, actionSet({Action::MoveLeft}));
    EXPECT_EQ(game.joypad.held, actionSet({Action::MoveLeft}));
    EXPECT_EQ(game.demo.framesRemaining, kTypeADemoInputs[1].frames);

    // Record 1 runs its single frame, then record 2 releases left. A release is not a press.
    demoSimulateJoypad(game);
    EXPECT_EQ(game.demo.framesRemaining, 0);
    demoSimulateJoypad(game);
    EXPECT_EQ(game.demo.nextRecord, 3);
    EXPECT_EQ(game.joypad.pressed, retropp::ActionSet{});
    EXPECT_EQ(game.demo.demoHeld, retropp::ActionSet{});

    // The baseline is the recording's previous step, not the player's buttons. Record 22 adds a
    // rotation to a held right; with right already held by the recording, only the rotation is newly
    // pressed — and that stays true no matter what the player happens to be holding.
    {
        GameContext g = running(ActiveDemo::TYPE_A);
        g.demo.nextRecord = 22;
        g.demo.framesRemaining = 0;
        g.demo.demoHeld = actionSet({Action::MoveRight});
        g.joypad.held = actionSet({Action::MoveRight, Action::RotateClockwise});

        ASSERT_EQ(kTypeADemoInputs[22].held,
                  actionSet({Action::MoveRight, Action::RotateClockwise}))
            << "the vector this case is built on";

        demoSimulateJoypad(g);
        EXPECT_EQ(g.joypad.pressed, actionSet({Action::RotateClockwise}));
    }
}

// ── Test 4: ReplayGates ─────────────────────────────────────────────────────────────────────────────
// DemoSimulateJoypad's two early-outs (tetris.asm:774-779): no demo running, or the recording path
// armed. Either one leaves the whole game untouched.
TEST(DemoPlayback, ReplayGates) {
    // No demo running.
    {
        GameContext game;
        const GameContext before = game;
        demoSimulateJoypad(game);
        EXPECT_TRUE(game == before);
    }

    // A demo running, but the recording flag is at its enable value.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.demo.recording = kDemoRecordingEnabledMagic;
        const GameContext before = game;
        demoSimulateJoypad(game);
        EXPECT_TRUE(game == before);
    }

    // A non-zero flag that is not the enable value does NOT gate replay — this routine tests for the
    // enable value exactly (:777-779), unlike the restore below.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.demo.recording = 1;
        demoSimulateJoypad(game);
        EXPECT_EQ(game.demo.nextRecord, 1) << "replay ran";
    }
}

// ── Test 5: SaveSubstituteRestore ───────────────────────────────────────────────────────────────────
// The substitution round trip (tetris.asm:811-816, :863-872): the player's held set is parked while the
// demo drives, and comes back afterwards.
TEST(DemoPlayback, SaveSubstituteRestore) {
    GameContext game = running(ActiveDemo::TYPE_A);
    game.demo.demoHeld = actionSet({Action::MoveLeft});
    game.demo.framesRemaining = 5;
    game.joypad.held = actionSet({Action::Start});

    demoSimulateJoypad(game);
    EXPECT_EQ(game.demo.savedHeld, actionSet({Action::Start})) << "the player's input is parked";
    EXPECT_EQ(game.joypad.held, actionSet({Action::MoveLeft})) << "the demo's input drives the frame";
    EXPECT_EQ(game.demo.framesRemaining, 4);

    restoreDemoSavedJoypad(game);
    EXPECT_EQ(game.joypad.held, actionSet({Action::Start})) << "the player's input comes back";

    // The restore's own gates. It tests the recording flag for ANY non-zero value, where replay tests
    // it against the enable value exactly (:867-869).
    for (const std::uint8_t flag : {std::uint8_t{1}, kDemoRecordingEnabledMagic}) {
        GameContext g = running(ActiveDemo::TYPE_A);
        g.demo.recording = flag;
        g.demo.savedHeld = actionSet({Action::Start});
        g.joypad.held = actionSet({Action::MoveLeft});
        restoreDemoSavedJoypad(g);
        EXPECT_EQ(g.joypad.held, actionSet({Action::MoveLeft})) << "flag " << int{flag};
    }

    // No demo running: nothing to restore.
    {
        GameContext g;
        g.demo.savedHeld = actionSet({Action::Start});
        g.joypad.held = actionSet({Action::MoveLeft});
        restoreDemoSavedJoypad(g);
        EXPECT_EQ(g.joypad.held, actionSet({Action::MoveLeft}));
    }
}

// ── Test 6: EndOfDemoTerminals ──────────────────────────────────────────────────────────────────────
// CheckForEndOfDemo (tetris.asm:734-767): the player's Start, or the recording's last piece.
TEST(DemoPlayback, EndOfDemoTerminals) {
    // Each recording ends as its own piece count is reached (:754-766).
    struct Vector {
        ActiveDemo demo;
        std::uint8_t target;
    };
    for (const Vector v : {Vector{ActiveDemo::TYPE_A, kTypeAEndPieceCount},
                           Vector{ActiveDemo::TYPE_B, kTypeBEndPieceCount}}) {
        GameContext before = running(v.demo);
        before.flow.numPiecesPlayed = static_cast<std::uint8_t>(v.target - 1);
        checkForEndOfDemo(before);
        EXPECT_EQ(before.flow.gameState, GameState::NORMAL_GAMEPLAY) << "one piece short";

        GameContext at = running(v.demo);
        at.flow.numPiecesPlayed = v.target;
        checkForEndOfDemo(at);
        EXPECT_EQ(at.flow.gameState, GameState::INIT_TITLE_SCREEN);

        // The test is for equality, not "at least": a count that has stepped past the target does not
        // end the demo this way (:762-764).
        GameContext past = running(v.demo);
        past.flow.numPiecesPlayed = static_cast<std::uint8_t>(v.target + 1);
        checkForEndOfDemo(past);
        EXPECT_EQ(past.flow.gameState, GameState::NORMAL_GAMEPLAY) << "past the target";
    }

    // Start ends the demo whatever the piece count (:743-751).
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.joypad.pressed = actionSet({Action::Start});
        checkForEndOfDemo(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);
    }

    // No demo running: the routine does nothing at all, whatever is pressed (:735-737).
    {
        GameContext game;
        game.joypad.pressed = actionSet({Action::Start});
        game.flow.numPiecesPlayed = kTypeAEndPieceCount;
        const GameContext expected = game;
        checkForEndOfDemo(game);
        EXPECT_TRUE(game == expected);
    }
}

// ── Test 7: AttractSeam ─────────────────────────────────────────────────────────────────────────────
// The title screen's attract countdown reaching zero launches a demo (tetris.asm:633-640), through the
// installed handler rather than a probe.
TEST(DemoPlayback, AttractSeam) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installTitleScreenHandlers(dispatcher, startDemo);

    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    game.flow.timer1 = 0;
    game.flow.coarseCountdown = 1;  // this tick's decrement reaches zero

    dispatcher.tick(game, retropp::ActionSet{});

    EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_A);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME);

    // One short of zero, the countdown just steps and no demo starts.
    {
        GameStateDispatcher d;
        kirpich::systems::installTitleScreenHandlers(d, startDemo);
        GameContext g;
        g.flow.gameState = GameState::TITLE_SCREEN;
        g.flow.timer1 = 0;
        g.flow.coarseCountdown = 2;

        d.tick(g, retropp::ActionSet{});

        EXPECT_EQ(g.demo.activeDemo, ActiveDemo::NONE);
        EXPECT_EQ(g.flow.coarseCountdown, 1);
    }
}

// ── Test 8: FrameIntegration ────────────────────────────────────────────────────────────────────────
// The four routines composed through the real gameplay frame (tetris.asm:4406-4421). The order is the
// point: the end check runs before the substitution, so it reads the player's own Start; the restore
// runs after everything that consumes input, so the buttons survive the frame.
TEST(DemoPlayback, FrameIntegration) {
    GameContext game = running(ActiveDemo::TYPE_A);
    game.flow.gameState = GameState::NORMAL_GAMEPLAY;
    game.joypad.held = actionSet({Action::Start});
    game.joypad.pressed = actionSet({Action::Start});

    kirpich::systems::normalGameplay(game, demoHooks());

    // The end check saw the player's real press, which it could only do by running before the
    // substitution replaced it.
    EXPECT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);

    // The restore ran after the frame's input consumers.
    EXPECT_EQ(game.joypad.held, actionSet({Action::Start}));

    // Start cannot pause a demo (:4445-4447), so the frame ran rather than stopping at the pause.
    EXPECT_FALSE(game.flow.paused);

    // The replay still advanced this frame.
    EXPECT_EQ(game.demo.nextRecord, 1);
}

// ── Test 9: RecordingIsDead ─────────────────────────────────────────────────────────────────────────
// RecordDemo (tetris.asm:824-860) runs every gameplay frame and never does anything, because nothing
// reaches the routine that arms it (StartRecordingDemo, :627-630, has no caller).
TEST(DemoPlayback, RecordingIsDead) {
    // The reachable state of the flag is zero, and the gate is shut there even with a demo running and
    // the input differing from the recording's (:828-830).
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.joypad.held = actionSet({Action::MoveLeft});
        const GameContext before = game;
        recordDemo(game);
        EXPECT_TRUE(game == before);
    }

    // No demo running is the other gate (:825-827).
    {
        GameContext game;
        game.demo.recording = kDemoRecordingEnabledMagic;
        const GameContext before = game;
        recordDemo(game);
        EXPECT_TRUE(game == before);
    }

    // Armed by hand, the path does what the original does. Input unchanged extends the current step
    // (:856-859).
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        startRecordingDemo(game);
        EXPECT_EQ(game.demo.recording, kDemoRecordingEnabledMagic);

        game.demo.demoHeld = actionSet({Action::MoveLeft});
        game.joypad.held = actionSet({Action::MoveLeft});
        game.demo.framesRemaining = 3;

        recordDemo(game);
        EXPECT_EQ(game.demo.framesRemaining, 4);
        EXPECT_EQ(game.demo.nextRecord, 0) << "the cursor holds while the step grows";
    }

    // Input changed closes the step and opens a new one (:836-851). The held set recorded comes from
    // the player, not from the timeline — this path writes, it does not replay.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        startRecordingDemo(game);
        game.demo.demoHeld = actionSet({Action::MoveLeft});
        game.demo.framesRemaining = 3;
        game.joypad.held = actionSet({Action::MoveRight});

        recordDemo(game);
        EXPECT_EQ(game.demo.nextRecord, 1);
        EXPECT_EQ(game.demo.demoHeld, actionSet({Action::MoveRight}));
        EXPECT_EQ(game.demo.framesRemaining, 0);

        // Not the timeline's record 0 — that is the replay path's business, and its data is untouched.
        EXPECT_NE(game.demo.demoHeld, kTypeADemoInputs[0].held);
        EXPECT_EQ(kTypeBDemoInputs[0].frames, 0x4D) << "the recordings are read-only";
    }
}

// ── Test 10: DemoGateSurvivesTheReturnToTitle ───────────────────────────────────────────────────────
// The running-demo byte is what the sound driver reads to blank every cue while a demo plays
// (audio.asm:73-80). A demo ends by entering the title-screen init, which re-cues the title song — and
// the byte is STILL SET at that point, because only starting a real game clears it (tetris.asm:695).
// So the driver eats that cue and the title song does not come back after a demo, though it does after
// a real round.
//
// This looks like a defect and is the original's behavior — the DEFAULT behavior, which this test
// pins. Clearing the byte on the way back to the title screen looks like tidying up and restores
// the music, and that is exactly what the audio fix does when a player asks for it
// (Settings::fixAudio, the fixes screen; the test below this one). Off, the quirk stands, and the
// state it depends on is pinned here.
TEST(DemoPlayback, DemoGateSurvivesTheReturnToTitle) {
    // A demo ends on its piece count. The state changes; the running-demo byte does not.
    GameContext game = running(ActiveDemo::TYPE_A);
    game.flow.numPiecesPlayed = kTypeAEndPieceCount;
    checkForEndOfDemo(game);

    ASSERT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);
    EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_A)
        << "the running-demo byte must survive the return — the alternation and the attract countdown "
           "both read it, and the sound driver still gates on it";

    // The title init runs and cues the title song.
    kirpich::systems::initTitleScreen(game);
    EXPECT_EQ(game.audioCues.music, kirpich::MusicId::TITLE);

    // And the demo gate is still set on that same frame, so the driver blanks the cue it just saw.
    const auto gestures = kirpich::systems::gesturesFor(game, /*alreadyPublished=*/std::nullopt);
    EXPECT_EQ(gestures.demoGate.demoNumber,
              std::optional<std::uint8_t>{static_cast<std::uint8_t>(ActiveDemo::TYPE_A)});

    // The contrast: starting a real game is the one thing that clears it (:690-696), so the title
    // screen entered after a round has the gate clear and its music is heard.
    GameContext afterRealGame = running(ActiveDemo::TYPE_A);
    afterRealGame.flow.gameState = GameState::TITLE_SCREEN;
    afterRealGame.joypad.pressed = actionSet({Action::Start});
    afterRealGame.joypad.held = actionSet({Action::Start});
    kirpich::systems::titleScreen(afterRealGame);
    ASSERT_EQ(afterRealGame.demo.activeDemo, ActiveDemo::NONE);

    kirpich::systems::initTitleScreen(afterRealGame);
    const auto clear = kirpich::systems::gesturesFor(afterRealGame, /*alreadyPublished=*/std::nullopt);
    EXPECT_EQ(clear.demoGate.demoNumber, std::optional<std::uint8_t>{0});
}

// ── Test 10b: the whole demo, replayed ──────────────────────────────────────────────────────────────
// Both recordings, end to end through the installed machine: boot, the copyright chain, the title
// screen's attract countdown, and every frame of both demos with the real handlers and the frame's
// vertical-blank beats — the way the shipped host runs them. Three laws, asserted over the whole run:
// every piece a demo spawns comes from the shared list in order, the randomizer is never consulted
// while a demo is playing, and each demo ends at its own terminal with the next one alternating.
//
// This is the composition test the per-routine cases above cannot substitute for: each seam can be
// right in isolation while the assembled machine plays a different game than the recordings were
// made against, and only a test that watches the pieces across a whole demo can say so.
TEST(DemoPlayback, TheWholeDemoReplaysTheSharedList) {
    // Both postures: the cartridge's own (fix off) and the audio fix on, whose end-of-demo clear
    // must not change a single piece of any demo.
    for (const bool fixAudio : {false, true}) {
    SCOPED_TRACE(fixAudio ? "audio fix on" : "audio fix off");
    GameStateDispatcher dispatcher;
    kirpich::systems::installTitleScreenHandlers(dispatcher, startDemo);

    // The randomizer, poisoned: a demo's pieces come from the shared list, so a single consultation
    // while a demo is running is itself a failure — and if one slipped through anyway, the constant
    // piece it returns would desynchronise the piece assertions below.
    bool drawConsulted = false;
    kirpich::systems::installGameplayHandlers(
        dispatcher, kirpich::systems::GameplayWiring{
                        .draw = [&drawConsulted] {
                            drawConsulted = true;
                            return std::uint8_t{0};
                        },
                        .demo = demoHooks([fixAudio] { return fixAudio; }),
                    });

    GameContext game;
    kirpich::systems::coldBoot(game);
    ASSERT_EQ(game.flow.gameState, GameState::INIT_COPYRIGHT);

    // One frame, the way the shipped host runs one: the dispatcher's five beats, then the
    // vertical-blank beats — which a line clear cannot finish without.
    const auto noDraw = [] { return std::uint8_t{0}; };
    const auto frame  = [&](retropp::ActionSet held) {
        dispatcher.tick(game, held);
        kirpich::systems::animateLineClear(game, noDraw, {});
        kirpich::systems::playingFieldWipeTick(game, noDraw, {});
        kirpich::systems::updateScoreboard(game);
    };

    // Through the copyright chain to the title screen.
    for (int i = 0; i < 600 && game.flow.gameState != GameState::TITLE_SCREEN; ++i) {
        frame({});
    }
    ASSERT_EQ(game.flow.gameState, GameState::TITLE_SCREEN) << "the boot chain must reach the title";

    // Run one demo to its end, watching every piece it spawns. nextPiece writes the shared list's
    // entry at the OLD count into the preview slot as it bumps the count, so each bump is checked
    // against kDemoPieceList at that index.
    const auto runDemo = [&](ActiveDemo expectDemo, std::uint8_t firstPiece,
                             std::uint8_t endCount) {
        // Force the attract countdown's last tick rather than idling through it.
        game.flow.timer1 = 0;
        game.flow.coarseCountdown = 1;
        frame({});
        ASSERT_EQ(game.demo.activeDemo, expectDemo);
        ASSERT_EQ(game.flow.gameState, GameState::INIT_GAME);
        ASSERT_EQ(game.flow.numPiecesPlayed, firstPiece);

        std::uint8_t seen = firstPiece;
        for (int i = 0; i < 60000 && game.flow.gameState != GameState::INIT_TITLE_SCREEN; ++i) {
            const std::uint8_t before = game.flow.numPiecesPlayed;
            frame({});
            const std::uint8_t after = game.flow.numPiecesPlayed;
            if (after != before) {
                // The round-init frame consumes three entries at once (the pipeline fill); every
                // frame after that consumes one. Either way the preview slot now holds the LAST
                // entry consumed, which is the shared list at the count just before the bump ended.
                ASSERT_GT(after, before);
                ASSERT_LE(static_cast<std::size_t>(after), kirpich::kDemoPieceList.size());
                const auto preview = static_cast<std::uint8_t>(
                    game.spriteRenderer.slots[kirpich::kPreviewPieceSlot].spriteId);
                EXPECT_EQ(preview, kirpich::kDemoPieceList[after - 1].raw)
                    << "piece " << (after - 1) << " of the "
                    << (expectDemo == ActiveDemo::TYPE_A ? "Type A" : "Type B") << " demo";
                seen = after;
            }
        }
        ASSERT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN)
            << "the demo must reach its terminal";
        EXPECT_EQ(seen, endCount) << "the demo ends on its own piece count";
        EXPECT_FALSE(drawConsulted) << "a demo's pieces never come from the randomizer";

        // Back onto the title screen for the next launch.
        for (int i = 0; i < 600 && game.flow.gameState != GameState::TITLE_SCREEN; ++i) {
            frame({});
        }
        ASSERT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
    };

    runDemo(ActiveDemo::TYPE_A, 0, kTypeAEndPieceCount);
    if (::testing::Test::HasFatalFailure()) return;
    runDemo(ActiveDemo::TYPE_B, kTypeBFirstPiece, kTypeBEndPieceCount);
    if (::testing::Test::HasFatalFailure()) return;
    runDemo(ActiveDemo::TYPE_A, 0, kTypeAEndPieceCount);
    }
}

// ── Test 11: the audio fix ──────────────────────────────────────────────────────────────────────────
// With Settings::fixAudio on, a demo that ends stops being one: both terminals clear the running-demo
// byte, so the title init's music cue reaches a driver whose mute gate is open and the title song
// comes back. What the byte's second duty needs — which recording ran, for the next launch's
// alternation — is parked on lastDemo, and the alternation reads it there.
TEST(DemoPlayback, AudioFixEndsTheDemoAtItsEnd) {
    // The piece-count terminal, fix on: the byte clears, the value parks, and the title init's cue
    // is published to the driver rather than gated.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.flow.numPiecesPlayed = kTypeAEndPieceCount;
        checkForEndOfDemo(game, /*fixAudio=*/true);

        ASSERT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::NONE);
        EXPECT_EQ(game.demo.lastDemo, ActiveDemo::TYPE_A);

        kirpich::systems::initTitleScreen(game);
        EXPECT_EQ(game.audioCues.music, kirpich::MusicId::TITLE);
        const auto gestures = kirpich::systems::gesturesFor(game, /*alreadyPublished=*/std::nullopt);
        EXPECT_EQ(gestures.demoGate.demoNumber, std::optional<std::uint8_t>{0})
            << "the driver's gate must be open, or the cue above is blanked before it plays";
    }

    // The player-Start terminal does the same.
    {
        GameContext game = running(ActiveDemo::TYPE_B);
        game.joypad.pressed = actionSet({Action::Start});
        checkForEndOfDemo(game, /*fixAudio=*/true);
        ASSERT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::NONE);
        EXPECT_EQ(game.demo.lastDemo, ActiveDemo::TYPE_B);
    }

    // The alternation is not lost with the byte: after a Type A demo ends under the fix, the next
    // launch runs the Type B recording, and the one after that Type A again - the same 0 -> A -> B
    // -> A cycle the cartridge keeps in the byte itself.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.flow.numPiecesPlayed = kTypeAEndPieceCount;
        checkForEndOfDemo(game, /*fixAudio=*/true);
        ASSERT_EQ(game.demo.activeDemo, ActiveDemo::NONE);

        startDemo(game);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_B)
            << "the alternation must read the parked value, or the fix resets the cycle to Type A";

        game.flow.numPiecesPlayed = kTypeBEndPieceCount;
        checkForEndOfDemo(game, /*fixAudio=*/true);
        ASSERT_EQ(game.demo.activeDemo, ActiveDemo::NONE);
        startDemo(game);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::TYPE_A);
    }

    // The hook wiring carries the setting: a hooks bundle built over a live query applies the fix,
    // and the default bundle does not.
    {
        GameContext game = running(ActiveDemo::TYPE_A);
        game.flow.numPiecesPlayed = kTypeAEndPieceCount;
        bool fix = true;
        const auto hooks = kirpich::systems::demoHooks([&fix] { return fix; });
        hooks.checkForEndOfDemo(game);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::NONE);

        GameContext untouched = running(ActiveDemo::TYPE_A);
        untouched.flow.numPiecesPlayed = kTypeAEndPieceCount;
        kirpich::systems::demoHooks().checkForEndOfDemo(untouched);
        EXPECT_EQ(untouched.demo.activeDemo, ActiveDemo::TYPE_A)
            << "an unwired bundle is the cartridge's behavior";
    }
}
