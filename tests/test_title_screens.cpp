// Title and copyright screens — behavioral tests against docs/contracts/title-screens.md.
//
// Device-free: the five state handlers are pure logic over the game-state aggregate. Every asserted value
// is traced to the tetris.asm lines named in the contract. Timer-law composition is exercised through the
// frame dispatcher (the same harness the menu screens use); the seam-firing and single-frame effects are
// asserted by calling the handlers directly.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/piece.h>

#include "data/demo.h"      // kDemoPieceList
#include "data/music.h"     // MusicId
#include "data/tilemaps.h"  // kTitleScreenTilemap
#include "retropp/input.h"
#include "state/demo_state.h"    // ActiveDemo
#include "state/engine_state.h"  // OamEntry
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/title_screens.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::Action;
using kirpich::BoundedVec;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::MusicId;
using kirpich::OamEntry;
using kirpich::Piece;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;

// Values pinned by the contract.
constexpr std::uint8_t kCopyrightTimer = 250;
constexpr std::uint8_t kTitleTimer = 125;
constexpr std::uint8_t kBorderTile = 0x8E;
constexpr std::uint8_t kCursorTile = 0x58;
constexpr std::uint8_t kCursorY = 0x80;
constexpr std::uint8_t kCursorX1P = 0x10;
constexpr std::uint8_t kCursorX2P = 0x60;
constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kAttractBetweenDemos = 4;
constexpr std::uint8_t kAttractColdEntry = 19;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// Present a pressed set to a handler; held mirrors it unless a test sets it separately.
void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held = game.joypad.pressed;
}

// The expected tile at a board cell after the title init.
//
// The board paint alone: walls at columns 1 and 12 on every row, floor across columns 1-12 at row 18,
// empty everywhere else (tetris.asm:538-555). The title screen that follows (:556-557) goes to the
// displayed map, not here, so nothing covers the paint — the two destinations are separate and the
// board keeps what it was given.
std::uint8_t expectedBoardCell(std::size_t row, std::size_t col) {
    if (col == 1 || col == 12) return kBorderTile;
    if (row == 18 && col >= 1 && col <= 12) return kBorderTile;
    return kSpace;
}

}  // namespace

// ── Test 1: CopyrightFlowVectors ────────────────────────────────────────────────────────────────────
// GameState_24 / _25 / _35 (tetris.asm:479-522): the init timer, the hold + re-arm, the skip matrix, and
// the timer law composing through the frame dispatcher.
TEST(TitleScreens, CopyrightFlowVectors) {
    // $24 init: arms the display timer and enters the hold.
    {
        GameContext game;
        game.flow.gameState = GameState::INIT_COPYRIGHT;
        kirpich::systems::initCopyrightScreen(game);
        EXPECT_EQ(game.flow.timer1, kCopyrightTimer);
        EXPECT_EQ(game.flow.gameState, GameState::COPYRIGHT_SCREEN);
    }
    // $25 hold: holds while the timer counts, re-arms and advances when it reaches zero.
    {
        GameContext holding;
        holding.flow.timer1 = 5;
        holding.flow.gameState = GameState::COPYRIGHT_SCREEN;
        kirpich::systems::copyrightHold(holding);
        EXPECT_EQ(holding.flow.gameState, GameState::COPYRIGHT_SCREEN);  // still counting
        EXPECT_EQ(holding.flow.timer1, 5);                              // untouched

        GameContext expired;
        expired.flow.timer1 = 0;
        expired.flow.gameState = GameState::COPYRIGHT_SCREEN;
        kirpich::systems::copyrightHold(expired);
        EXPECT_EQ(expired.flow.timer1, kCopyrightTimer);  // re-armed
        EXPECT_EQ(expired.flow.gameState, GameState::COPYRIGHT_SCREEN_SKIPPABLE);
    }
    // $35 skip matrix: any single pressed action fires; held-but-not-pressed does not; timer expiry fires;
    // otherwise inert.
    {
        for (const Action a : {Action::Start, Action::Select, Action::Confirm, Action::Back,
                               Action::MenuUp, Action::MenuDown, Action::MenuLeft, Action::MenuRight}) {
            GameContext game;
            game.flow.timer1 = 50;  // timer not expired — only the press can fire
            game.flow.gameState = GameState::COPYRIGHT_SCREEN_SKIPPABLE;
            game.joypad.pressed = actionSet({a});
            kirpich::systems::copyrightSkippable(game);
            EXPECT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN) << "pressed " << int(a);
        }
        // Held but not newly pressed: no skip.
        GameContext heldOnly;
        heldOnly.flow.timer1 = 50;
        heldOnly.flow.gameState = GameState::COPYRIGHT_SCREEN_SKIPPABLE;
        heldOnly.joypad.held = actionSet({Action::Start});  // pressed stays empty
        kirpich::systems::copyrightSkippable(heldOnly);
        EXPECT_EQ(heldOnly.flow.gameState, GameState::COPYRIGHT_SCREEN_SKIPPABLE);
        // Timer expiry with no input: skip.
        GameContext expired;
        expired.flow.timer1 = 0;
        expired.flow.gameState = GameState::COPYRIGHT_SCREEN_SKIPPABLE;
        kirpich::systems::copyrightSkippable(expired);
        EXPECT_EQ(expired.flow.gameState, GameState::INIT_TITLE_SCREEN);
        // Neither: inert.
        GameContext inert;
        inert.flow.timer1 = 50;
        inert.flow.gameState = GameState::COPYRIGHT_SCREEN_SKIPPABLE;
        kirpich::systems::copyrightSkippable(inert);
        EXPECT_EQ(inert.flow.gameState, GameState::COPYRIGHT_SCREEN_SKIPPABLE);
    }
    // Through the dispatcher: the per-frame timer decrement drives $25's expiry, and a pressed input drives
    // $35's skip.
    {
        GameStateDispatcher dispatcher;
        kirpich::systems::installTitleScreenHandlers(dispatcher);
        const retropp::ActionSet none;

        GameContext game;
        game.flow.gameState = GameState::COPYRIGHT_SCREEN;
        game.flow.timer1 = 2;
        dispatcher.tick(game, none);  // 2 != 0 -> hold; timer 2 -> 1
        EXPECT_EQ(game.flow.gameState, GameState::COPYRIGHT_SCREEN);
        dispatcher.tick(game, none);  // 1 != 0 -> hold; timer 1 -> 0
        EXPECT_EQ(game.flow.gameState, GameState::COPYRIGHT_SCREEN);
        dispatcher.tick(game, none);  // 0 -> re-arm 250, advance; timer 250 -> 249
        EXPECT_EQ(game.flow.gameState, GameState::COPYRIGHT_SCREEN_SKIPPABLE);

        dispatcher.tick(game, actionSet({Action::Start}));  // pressed edge -> skip
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TITLE_SCREEN);
    }
}

// ── Test 2: RingFillVectors ─────────────────────────────────────────────────────────────────────────
// GameState_24 (tetris.asm:485-493): the 48 real demo entries are copied into the piece ring; the rest of
// the ring is untouched (the original's over-copy is dropped).
TEST(TitleScreens, RingFillVectors) {
    GameContext game;
    constexpr Piece kSentinel{0xEE};
    for (std::size_t i = kirpich::kDemoPieceCount; i < game.engine.pieceList.size(); ++i) {
        game.engine.pieceList[i] = kSentinel;
    }

    kirpich::systems::initCopyrightScreen(game);

    for (std::size_t i = 0; i < kirpich::kDemoPieceCount; ++i) {
        EXPECT_EQ(game.engine.pieceList[i], kirpich::kDemoPieceList[i]) << "ring " << i;
    }
    for (std::size_t i = kirpich::kDemoPieceCount; i < game.engine.pieceList.size(); ++i) {
        EXPECT_EQ(game.engine.pieceList[i], kSentinel) << "over-copy tail " << i;
    }
}

// ── Test 3: TitleInitVectors ────────────────────────────────────────────────────────────────────────
// GameState_06 (tetris.asm:524-580): the field clears, the board paint, the cursor, the music cue, the
// timer, the transition, and the attract-seed fork.
TEST(TitleScreens, TitleInitVectors) {
    auto seeded = []() {
        GameContext game;
        // Dirty every field the init clears, to prove the clear.
        game.demo.recording = 0xFF;
        game.flow.pieceLockStage = 3;
        game.flow.blinkCounter = 9;
        game.flow.topOutLockCount = 2;
        game.flow.lines = 1234;
        game.flow.wipeCounter = 7;
        game.highScores.newTopScore = true;
        game.engine.score = 5000;
        game.engine.stats.singles = 4;
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5, 6};
        game.engine.oam[7] = OamEntry{.y = 0x22};  // dirty a slot to prove the object-buffer clear
        return game;
    };

    // A demo just ended (activeDemo non-zero): the attract seed is 4.
    {
        GameContext game = seeded();
        game.demo.activeDemo = ActiveDemo::TYPE_A;
        kirpich::systems::initTitleScreen(game);

        // Field clears.
        EXPECT_EQ(game.demo.recording, 0);
        EXPECT_EQ(game.flow.pieceLockStage, 0);
        EXPECT_EQ(game.flow.blinkCounter, 0);
        EXPECT_EQ(game.flow.topOutLockCount, 0);
        EXPECT_EQ(game.flow.lines, 0);
        EXPECT_EQ(game.flow.wipeCounter, 0);
        EXPECT_FALSE(game.highScores.newTopScore);
        EXPECT_EQ(game.engine.score, 0u);                 // clearScoreAndStats
        EXPECT_EQ(game.engine.stats.singles, 0);          // clearScoreAndStats
        EXPECT_EQ(game.engine.lineClears.size(), 0u);     // clearLineClearsList

        // Board paint: every cell empty except the walls and floor.
        for (std::size_t row = 0; row < kirpich::kBoardRows; ++row) {
            for (std::size_t col = 0; col < kirpich::kBoardCols; ++col) {
                EXPECT_EQ(game.field.board[row][col], expectedBoardCell(row, col))
                    << "cell " << row << "," << col;
            }
        }

        // Cursor object, cleared buffer, music, timer, transition.
        EXPECT_EQ(game.engine.oam[7], OamEntry{});  // buffer cleared
        EXPECT_EQ(game.engine.oam[0], (OamEntry{.y = kCursorY, .x = kCursorX1P, .tile = kCursorTile}));
        EXPECT_EQ(game.audioCues.music, MusicId::TITLE);
        EXPECT_EQ(game.flow.timer1, kTitleTimer);
        EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
        EXPECT_EQ(game.flow.coarseCountdown, kAttractBetweenDemos);
    }
    // Cold entry (no demo has run): the attract seed is 19.
    {
        GameContext game = seeded();
        game.demo.activeDemo = ActiveDemo::NONE;
        kirpich::systems::initTitleScreen(game);
        EXPECT_EQ(game.flow.coarseCountdown, kAttractColdEntry);
    }
}

// ── Test 4: TitleCursorVectors ──────────────────────────────────────────────────────────────────────
// GameState_07 cursor input (tetris.asm:657-731): Select toggles the 1P/2P cursor; Right moves 1P->2P
// only; Left moves 2P->1P only; the OAM X is placed accordingly; nothing else changes.
TEST(TitleScreens, TitleCursorVectors) {
    auto titleContext = [](bool multiplayer) {
        GameContext game;
        game.flow.gameState = GameState::TITLE_SCREEN;
        game.flow.timer1 = 5;  // non-zero: the attract countdown is skipped, isolating the input
        game.multiplayer.isMultiplayer = multiplayer;
        return game;
    };

    // Select toggles both ways and places the cursor.
    {
        GameContext game = titleContext(false);
        press(game, {Action::Select});
        kirpich::systems::titleScreen(game);
        EXPECT_TRUE(game.multiplayer.isMultiplayer);
        EXPECT_EQ(game.engine.oam[0].x, kCursorX2P);

        GameContext back = titleContext(true);
        press(back, {Action::Select});
        kirpich::systems::titleScreen(back);
        EXPECT_FALSE(back.multiplayer.isMultiplayer);
        EXPECT_EQ(back.engine.oam[0].x, kCursorX1P);
    }
    // Right: 1P -> 2P; a no-op (with no cursor write) when already 2P.
    {
        GameContext game = titleContext(false);
        press(game, {Action::MenuRight});
        kirpich::systems::titleScreen(game);
        EXPECT_TRUE(game.multiplayer.isMultiplayer);
        EXPECT_EQ(game.engine.oam[0].x, kCursorX2P);

        GameContext already = titleContext(true);
        already.engine.oam[0].x = 0xAB;  // sentinel — must be untouched
        press(already, {Action::MenuRight});
        kirpich::systems::titleScreen(already);
        EXPECT_TRUE(already.multiplayer.isMultiplayer);
        EXPECT_EQ(already.engine.oam[0].x, 0xAB);
    }
    // Left: 2P -> 1P; a no-op (with no cursor write) when already 1P.
    {
        GameContext game = titleContext(true);
        press(game, {Action::MenuLeft});
        kirpich::systems::titleScreen(game);
        EXPECT_FALSE(game.multiplayer.isMultiplayer);
        EXPECT_EQ(game.engine.oam[0].x, kCursorX1P);

        GameContext already = titleContext(false);
        already.engine.oam[0].x = 0xAB;
        press(already, {Action::MenuLeft});
        kirpich::systems::titleScreen(already);
        EXPECT_FALSE(already.multiplayer.isMultiplayer);
        EXPECT_EQ(already.engine.oam[0].x, 0xAB);
    }
    // A cursor move does not transition state.
    {
        GameContext game = titleContext(false);
        press(game, {Action::Select});
        kirpich::systems::titleScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
    }
}

// ── Test 5: TitleStartAndAttractVectors ─────────────────────────────────────────────────────────────
// GameState_07 Start + attract countdown (tetris.asm:632-706): the 1P Start transition and its zeroing,
// the heart-mode held-fork, the deferred 2P Start, and the countdown / demo-launch seam.
TEST(TitleScreens, TitleStartAndAttractVectors) {
    auto startContext = []() {
        GameContext game;
        game.flow.gameState = GameState::TITLE_SCREEN;
        game.flow.timer1 = 5;  // non-zero: skip the countdown, reach the input
        game.multiplayer.isMultiplayer = false;
        // Dirty the fields the entry zeroing clears.
        game.flow.typeALevel = 7;
        game.flow.typeBLevel = 8;
        game.flow.typeBStartHeight = 3;
        game.demo.activeDemo = ActiveDemo::TYPE_A;
        return game;
    };

    // 1P Start (Down not held): enter the config screen with the entry zeroing; heart mode stays off.
    {
        GameContext game = startContext();
        press(game, {Action::Start});
        kirpich::systems::titleScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_SELECTION);
        EXPECT_EQ(game.flow.timer1, 0);
        EXPECT_EQ(game.flow.typeALevel, 0);
        EXPECT_EQ(game.flow.typeBLevel, 0);
        EXPECT_EQ(game.flow.typeBStartHeight, 0);
        EXPECT_EQ(game.demo.activeDemo, ActiveDemo::NONE);
        EXPECT_EQ(game.flow.heartMode, 0);
    }
    // 1P Start with Down held: heart mode latches non-zero.
    {
        GameContext game = startContext();
        game.joypad.pressed = actionSet({Action::Start});
        game.joypad.held = actionSet({Action::Start, Action::SoftDrop});  // Down held
        kirpich::systems::titleScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_SELECTION);
        EXPECT_NE(game.flow.heartMode, 0);
    }
    // 2P Start: the serial handshake is deferred, so the port takes no action.
    {
        GameContext game = startContext();
        game.multiplayer.isMultiplayer = true;
        press(game, {Action::Start});
        kirpich::systems::titleScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);  // no transition
    }
    // Attract countdown: fires the demo seam exactly when it reaches zero, re-arms otherwise.
    {
        int demos = 0;
        auto hook = [&](GameContext&) { ++demos; };

        GameContext atZero;
        atZero.flow.gameState = GameState::TITLE_SCREEN;
        atZero.flow.timer1 = 0;
        atZero.flow.coarseCountdown = 1;
        kirpich::systems::titleScreen(atZero, hook);  // 1 -> 0 -> launch
        EXPECT_EQ(demos, 1);
        EXPECT_EQ(atZero.flow.gameState, GameState::TITLE_SCREEN);  // the launch does not transition state

        GameContext notYet;
        notYet.flow.gameState = GameState::TITLE_SCREEN;
        notYet.flow.timer1 = 0;
        notYet.flow.coarseCountdown = 3;
        kirpich::systems::titleScreen(notYet, hook);  // 3 -> 2, re-arm timer
        EXPECT_EQ(demos, 1);  // unchanged
        EXPECT_EQ(notYet.flow.coarseCountdown, 2);
        EXPECT_EQ(notYet.flow.timer1, kTitleTimer);

        // Default (no hook installed): a build without the demo system idles at the title.
        GameContext idle;
        idle.flow.gameState = GameState::TITLE_SCREEN;
        idle.flow.timer1 = 0;
        idle.flow.coarseCountdown = 1;
        kirpich::systems::titleScreen(idle);
        EXPECT_EQ(idle.flow.gameState, GameState::TITLE_SCREEN);
    }
    // Through the dispatcher: the timer reload composes with the per-frame decrement.
    {
        GameStateDispatcher dispatcher;
        kirpich::systems::installTitleScreenHandlers(dispatcher);
        GameContext game;
        game.flow.gameState = GameState::TITLE_SCREEN;
        game.flow.timer1 = 0;
        game.flow.coarseCountdown = 5;
        dispatcher.tick(game, retropp::ActionSet{});  // 0 -> countdown 5->4, timer 125 -> 124
        EXPECT_EQ(game.flow.coarseCountdown, 4);
        EXPECT_EQ(game.flow.timer1, kTitleTimer - 1);
    }
}
