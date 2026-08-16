// Line-clear pipeline — behavioral tests against docs/contracts/line-clear.md.
//
// Device-free: the five functions are pure logic over the game-state aggregate. Every asserted value
// is traced to the tetris.asm lines named in the contract (CheckForCompletedRows :5301,
// AnimateLineClear :5412, MoveBlocksDownAfterLineClear :5498, ClearLineClearsList :5552, the wipe
// dispatchers PlayingFieldWipe02..19 :5563 + WipePlayingFieldRow :5896, and the frame anatomy at
// :214 / :386). No ROM read, no virtual machine.
//
// A file-local frame harness reproduces the two-context frame the cadences depend on: the handler
// beat (scan and compaction) runs first, then the saturating frame-timer decrement, then the
// vertical-blank beat (flash and wipe). Only the line-clear functions are wired; piece movement is
// set up directly by each test.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/piece.h>
#include <kirpich/sprite_id.h>

#include "data/bounded_vec.h"
#include "data/music.h"
#include "data/playing_field.h"
#include "data/sfx.h"
#include "systems/game_context.h"
#include "systems/line_clear.h"

namespace {

using kirpich::BoundedVec;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::kActivePieceSlot;
using kirpich::kPlayingFieldCols;
using kirpich::kPreviewPieceSlot;
using kirpich::MusicId;
using kirpich::NoiseSfxId;
using kirpich::Piece;
using kirpich::playingFieldRowForWipeCounter;
using kirpich::SpriteId;
using kirpich::SquareSfxId;
using kirpich::systems::animateLineClear;
using kirpich::systems::checkForCompletedRows;
using kirpich::systems::clearLineClearsList;
using kirpich::systems::GameContext;
using kirpich::systems::moveBlocksDownAfterLineClear;
using kirpich::systems::playingFieldWipeTick;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);  // 0x2F
constexpr std::uint8_t kBrick = 0x28;                                       // any non-space fills a cell

// Fill the whole board with the empty-space tile — the playable state (boot is all-zero, which is not
// "empty" for the scan). Tests place completed rows and markers on top of this.
void spaceField(GameContext& game) {
    for (auto& row : game.field.board) {
        row.fill(kSpace);
    }
}

// Complete one field row: fill its ten visible cells with a brick.
void fillFieldRow(GameContext& game, std::uint8_t row) {
    for (std::uint8_t col = 0; col < kPlayingFieldCols; ++col) {
        game.field.fieldCell(row, col) = kBrick;
    }
}

// The frame anatomy: handler beat → saturating timer decrement → vertical-blank beat.
void frameTick(GameContext& game, const std::function<std::uint8_t()>& draw) {
    checkForCompletedRows(game);
    moveBlocksDownAfterLineClear(game);
    if (game.flow.timer1 > 0) --game.flow.timer1;
    if (game.flow.timer2 > 0) --game.flow.timer2;
    animateLineClear(game, draw);
    playingFieldWipeTick(game, draw);
}

const std::function<std::uint8_t()> noDraw = []() -> std::uint8_t { return 0; };

}  // namespace

// 1. ScanVectors — the stage-2 gate (tetris.asm:5301-5304), the 16-of-18-rows scan window that skips
// the top two rows (:5310-5311), completeness by the CharTile::SPACE tie (:5316-5318), the top-to-
// bottom list of field-row indices and the count (:5321-5337), the stage/timer advance (:5339-5342),
// and the unconditional lock-sound cue (:5305-5306).
TEST(LineClear, ScanVectors) {
    // Stage gate: only stage 2 runs the scan.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 1;
        const GameContext snap = game;
        checkForCompletedRows(game);
        EXPECT_EQ(game, snap);
    }

    // The 18-row detection domain: a single complete row placed at each field row. Rows 0 and 1 are
    // never scanned; rows 2..17 are detected and listed by their field-row index.
    for (std::uint8_t r = 0; r < 18; ++r) {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, r);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        checkForCompletedRows(game);
        if (r < 2) {
            EXPECT_EQ(game.flow.completedRowCount, 0) << "row " << int(r);
            EXPECT_TRUE(game.engine.lineClears.empty()) << "row " << int(r);
        } else {
            ASSERT_EQ(game.flow.completedRowCount, 1) << "row " << int(r);
            ASSERT_EQ(game.engine.lineClears.size(), 1u) << "row " << int(r);
            EXPECT_EQ(game.engine.lineClears[0], r) << "row " << int(r);
        }
    }

    // A single empty cell anywhere in the row leaves it incomplete — swept across all ten columns.
    for (std::uint8_t hole = 0; hole < kPlayingFieldCols; ++hole) {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.field.fieldCell(5, hole) = kSpace;
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.completedRowCount, 0) << "hole col " << int(hole);
        EXPECT_TRUE(game.engine.lineClears.empty()) << "hole col " << int(hole);
    }

    // Multi-clear: rows are listed in scan order (top to bottom), not the order they were filled.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 10);  // filled first, but lower on screen
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        checkForCompletedRows(game);
        ASSERT_EQ(game.engine.lineClears.size(), 2u);
        EXPECT_EQ(game.engine.lineClears[0], 5);
        EXPECT_EQ(game.engine.lineClears[1], 10);
        EXPECT_EQ(game.flow.completedRowCount, 2);
    }

    // The lock sound cues, and the stage advances, on every stage-2 entry — even with nothing to clear.
    {
        GameContext game;
        spaceField(game);
        game.flow.pieceLockStage = 2;
        checkForCompletedRows(game);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::LOCK_PIECE);
        EXPECT_EQ(game.flow.completedRowCount, 0);
        EXPECT_TRUE(game.engine.lineClears.empty());
        EXPECT_EQ(game.flow.pieceLockStage, 3);
        EXPECT_EQ(game.flow.timer1, 2);
    }
}

// 2. LinesTallyVectors — the Type A accumulate + 9999 clamp (tetris.asm:5351-5364), the Type B count-
// down with its <= floor (:5366-5371, :5407-5410) and the preserved tens-digit-9 guard (:5374-5376),
// the per-kind stat increment and rows-to-send-as-garbage map (:5377-5401), the clear-vs-Tetris cue,
// and the no-clear early-out (:5343-5345).
TEST(LineClear, LinesTallyVectors) {
    // Type A: a single adds one line.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.lines = 42;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 43);
    }
    // Type A: the count saturates at 9999.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        fillFieldRow(game, 6);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.lines = 9998;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 9999);
    }

    // Type B: a normal subtract.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 25;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 24);
    }
    // Type B floor, equal case (lines == n): to zero.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 1;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 0);
    }
    // Type B floor, borrow case (lines < n): to zero.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        fillFieldRow(game, 6);
        fillFieldRow(game, 7);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 2;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 0);
    }
    // Type B tens-digit-9 guard, asserted directly on an out-of-range state the original preserves:
    // a subtract that lands the tens digit on 9 zeroes the count.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 95;  // 95 - 1 = 94 → tens digit 9 → zeroed
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 0);
    }
    // …and the guard's boundary: a tens digit of 8 is left alone.
    {
        GameContext game;
        spaceField(game);
        fillFieldRow(game, 5);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 85;  // 85 - 1 = 84 → tens digit 8 → kept
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 84);
    }

    // Per-kind: the matching stat increments, the rows-to-send map is {1->0, 2->1, 3->2, 4->4}, and the
    // square cue is the clear sound (Tetris for four).
    const std::array<std::uint8_t, 5> garbageForN = {0, 0, 1, 2, 4};
    for (std::uint8_t n = 1; n <= 4; ++n) {
        GameContext game;
        spaceField(game);
        for (std::uint8_t k = 0; k < n; ++k) {
            fillFieldRow(game, static_cast<std::uint8_t>(5 + k));
        }
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.completedRowCount, n) << "n=" << int(n);
        EXPECT_EQ(game.engine.stats.singles, n == 1 ? 1 : 0) << "n=" << int(n);
        EXPECT_EQ(game.engine.stats.doubles, n == 2 ? 1 : 0) << "n=" << int(n);
        EXPECT_EQ(game.engine.stats.triples, n == 3 ? 1 : 0) << "n=" << int(n);
        EXPECT_EQ(game.engine.stats.tetrises, n == 4 ? 1 : 0) << "n=" << int(n);
        EXPECT_EQ(game.multiplayer.garbageRowsToSend, garbageForN[n]) << "n=" << int(n);
        EXPECT_EQ(game.audioCues.square, n == 4 ? SquareSfxId::TETRIS : SquareSfxId::LINE_CLEAR)
            << "n=" << int(n);
    }

    // No clear: lines, stats, the garbage count, and the square cue are all left untouched (the lock
    // noise still cues and the stage still advances).
    {
        GameContext game;
        spaceField(game);
        game.flow.pieceLockStage = 2;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.lines = 7;
        game.engine.stats.singles = 3;
        game.multiplayer.garbageRowsToSend = 9;
        checkForCompletedRows(game);
        EXPECT_EQ(game.flow.lines, 7);
        EXPECT_EQ(game.engine.stats.singles, 3);
        EXPECT_EQ(game.multiplayer.garbageRowsToSend, 9);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::LOCK_PIECE);
        EXPECT_EQ(game.flow.pieceLockStage, 3);
    }
}

// 3. AnimateCadence — through the frame harness: the scan arms a two-frame delay, then seven flash
// passes step the phase on a ten-frame cadence (tetris.asm:5448-5456), and the seventh resets the
// phase, holds 13 frames, and starts the wipe (:5458-5468). The board is never touched by the flash.
TEST(LineClear, AnimateCadence) {
    GameContext game;
    spaceField(game);
    fillFieldRow(game, 5);
    const auto boardBefore = game.field.board;
    game.flow.pieceLockStage = 2;
    game.flow.gameType = GameType::TYPE_A;

    std::vector<int> fireTicks;
    std::uint8_t lastBlink = 0;
    int terminalTick = -1;
    for (int t = 1; t <= 200; ++t) {
        frameTick(game, noDraw);
        if (game.flow.pieceLockStage == 0) {  // the seventh pass ended the flash
            terminalTick = t;
            break;
        }
        if (game.flow.blinkCounter != lastBlink) {  // a flash pass advanced the phase
            fireTicks.push_back(t);
            lastBlink = game.flow.blinkCounter;
        }
    }

    // Six visible phase advances (phases 1..6); the seventh is the terminal tick.
    ASSERT_EQ(fireTicks.size(), 6u);
    EXPECT_EQ(fireTicks.front(), 2);  // scan on tick 1 (timer 2->1), first flash on tick 2 (timer ->0)
    for (std::size_t i = 1; i < fireTicks.size(); ++i) {
        EXPECT_EQ(fireTicks[i] - fireTicks[i - 1], 10) << "pass " << i;  // ten-frame cadence
    }
    ASSERT_GT(terminalTick, 0);
    EXPECT_EQ(terminalTick, fireTicks.back() + 10);

    // Terminal state: phase reset, the pre-wipe hold, the wipe armed, the lock ended.
    EXPECT_EQ(game.flow.blinkCounter, 0);
    EXPECT_EQ(game.flow.timer1, 13);
    EXPECT_EQ(game.flow.wipeCounter, 1);
    EXPECT_EQ(game.flow.pieceLockStage, 0);

    // The flash writes only video memory, never the board.
    EXPECT_EQ(game.field.board, boardBefore);

    // Gates: wrong stage, or a non-zero timer, is a no-op.
    {
        game.flow.pieceLockStage = 2;
        game.flow.timer1 = 0;
        const GameContext snap = game;
        animateLineClear(game, noDraw);
        EXPECT_EQ(game, snap);
    }
    {
        game.flow.pieceLockStage = 3;
        game.flow.timer1 = 5;
        const GameContext snap = game;
        animateLineClear(game, noDraw);
        EXPECT_EQ(game, snap);
    }
}

// 4. NoClearSpawnPath — the even-phase / empty-list shortcut spawns the next piece and ends the lock,
// leaving the flash and wipe counters untouched (tetris.asm:5420-5426, :5494-5496). Through the
// harness it fires two frames after a clear-less scan.
TEST(LineClear, NoClearSpawnPath) {
    // Direct: post-scan state with an empty list and even phase → spawn, stage 0, counters untouched.
    {
        GameContext game;
        game.flow.pieceLockStage = 3;
        game.flow.blinkCounter = 0;
        game.flow.timer1 = 0;
        game.flow.wipeCounter = 0;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;  // 0x18
        game.flow.nextPreviewPiece = Piece{0x08};                               // I_0
        game.flow.framesPerDrop = 25;
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x04; };
        animateLineClear(game, draw);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::T_0);   // promoted
        EXPECT_EQ(game.spriteRenderer.slots[kPreviewPieceSlot].spriteId, SpriteId::I_0);  // old preview
        EXPECT_EQ(game.flow.pieceLockStage, 0);
        EXPECT_EQ(game.flow.wipeCounter, 0);
        EXPECT_EQ(game.flow.blinkCounter, 0);
        EXPECT_EQ(game.flow.timer1, 0);
        EXPECT_EQ(calls, 1);
    }

    // Harness: a clear-less scan on tick 1, the spawn fires on tick 2 (the timer having reached 0).
    {
        GameContext game;
        spaceField(game);  // nothing completes
        game.flow.pieceLockStage = 2;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;
        game.flow.nextPreviewPiece = Piece{0x08};
        game.flow.framesPerDrop = 25;
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x04; };

        frameTick(game, draw);
        EXPECT_EQ(game.flow.pieceLockStage, 3);
        EXPECT_EQ(calls, 0);

        frameTick(game, draw);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(game.flow.pieceLockStage, 0);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::T_0);
    }
}

// 5. CompactionVectors — the gates (tetris.asm:5498-5504), the single-clear shift with the top row
// cleared and the non-field columns byte-identical (:5505-5546), the multi-clear top-row duplicate
// quirk (row 0 cleared once, after every listed row), and the list clear + wipe advance (:5547-5549).
TEST(LineClear, CompactionVectors) {
    // Gate: timer1 != 0 is a no-op.
    {
        GameContext game;
        spaceField(game);
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5};
        game.flow.timer1 = 1;
        game.flow.wipeCounter = 1;
        const GameContext snap = game;
        moveBlocksDownAfterLineClear(game);
        EXPECT_EQ(game, snap);
    }
    // Gate: wipeCounter != 1 is a no-op.
    {
        GameContext game;
        spaceField(game);
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5};
        game.flow.timer1 = 0;
        game.flow.wipeCounter = 2;
        const GameContext snap = game;
        moveBlocksDownAfterLineClear(game);
        EXPECT_EQ(game, snap);
    }

    // Single clear at row 5: every row above descends by one, the top row is cleared, and cells
    // outside the ten field columns are untouched.
    {
        GameContext game;
        spaceField(game);
        for (std::uint8_t r = 0; r <= 5; ++r) {
            game.field.fieldCell(r, 0) = static_cast<std::uint8_t>(0x40 + r);  // per-row marker
        }
        game.field.board[3][0] = 0xEE;   // left wall column (outside the field)
        game.field.board[3][15] = 0xDD;  // right of the field
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5};
        game.flow.completedRowCount = 1;
        game.flow.timer1 = 0;
        game.flow.wipeCounter = 1;
        moveBlocksDownAfterLineClear(game);

        EXPECT_EQ(game.field.fieldCell(5, 0), 0x44);   // old row 4
        EXPECT_EQ(game.field.fieldCell(4, 0), 0x43);
        EXPECT_EQ(game.field.fieldCell(3, 0), 0x42);
        EXPECT_EQ(game.field.fieldCell(2, 0), 0x41);
        EXPECT_EQ(game.field.fieldCell(1, 0), 0x40);   // old row 0
        EXPECT_EQ(game.field.fieldCell(0, 0), kSpace);  // top row cleared
        EXPECT_EQ(game.field.board[3][0], 0xEE);
        EXPECT_EQ(game.field.board[3][15], 0xDD);
        EXPECT_TRUE(game.engine.lineClears.empty());
        EXPECT_EQ(game.flow.wipeCounter, 2);
    }

    // Multi-clear top-row duplicate quirk: with two rows cleared and content in the top rows, the top
    // row is only cleared once (after both), so the old top row lands in two rows.
    {
        GameContext game;
        spaceField(game);
        game.field.fieldCell(0, 0) = 0xA0;  // old row 0
        game.field.fieldCell(1, 0) = 0xB0;  // old row 1
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5, 10};
        game.flow.completedRowCount = 2;
        game.flow.timer1 = 0;
        game.flow.wipeCounter = 1;
        moveBlocksDownAfterLineClear(game);

        EXPECT_EQ(game.field.fieldCell(0, 0), kSpace);
        EXPECT_EQ(game.field.fieldCell(1, 0), 0xA0);  // old row 0
        EXPECT_EQ(game.field.fieldCell(2, 0), 0xA0);  // old row 0 again — the duplicate quirk
        EXPECT_EQ(game.field.fieldCell(3, 0), 0xB0);  // old row 1
        EXPECT_TRUE(game.engine.lineClears.empty());
        EXPECT_EQ(game.flow.wipeCounter, 2);
    }

    // clearLineClearsList on its own empties the list and touches nothing else.
    {
        GameContext game;
        game.engine.lineClears = BoundedVec<std::uint8_t, 4>{5, 10, 15};
        game.flow.completedRowCount = 3;
        clearLineClearsList(game);
        EXPECT_TRUE(game.engine.lineClears.empty());
        EXPECT_EQ(game.flow.completedRowCount, 3);  // untouched
    }
}

// 6. WipeStepperWalk — out-of-range no-ops, the one-step-per-call 2->19->0 walk (tetris.asm:5563-5751
// + :5896-5908), the bottom-row-first row identity, the wipe-8 stack-fall / garbage cue gating
// (:5619-5645), and the soft-drop latch re-armed at the final step (:5748).
TEST(LineClear, WipeStepperWalk) {
    // Out-of-range: nothing runs.
    for (int c : {0, 1, 20, 21, 255}) {
        GameContext game;
        game.flow.wipeCounter = static_cast<std::uint8_t>(c);
        const GameContext snap = game;
        playingFieldWipeTick(game, noDraw);
        EXPECT_EQ(game, snap) << "counter " << c;
    }

    // The wipe redraws the bottom row first: counter 2 -> field row 17, counter 19 -> field row 0, one
    // row higher per step.
    EXPECT_EQ(playingFieldRowForWipeCounter(2), 17);
    EXPECT_EQ(playingFieldRowForWipeCounter(19), 0);
    for (std::uint8_t c = 2; c < 19; ++c) {
        EXPECT_EQ(playingFieldRowForWipeCounter(c) - playingFieldRowForWipeCounter(c + 1), 1)
            << "counter " << int(c);
    }

    // The walk: each call advances the counter by one, and the final step resets it to zero.
    {
        GameContext game;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_A;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;
        game.flow.nextPreviewPiece = Piece{0x08};
        game.flow.wipeCounter = 2;
        auto draw = []() -> std::uint8_t { return 0x04; };
        for (std::uint8_t step = 2; step <= 18; ++step) {
            EXPECT_EQ(game.flow.wipeCounter, step);
            playingFieldWipeTick(game, draw);
            EXPECT_EQ(game.flow.wipeCounter, step + 1);
        }
        EXPECT_EQ(game.flow.wipeCounter, 19);
        playingFieldWipeTick(game, draw);  // terminal
        EXPECT_EQ(game.flow.wipeCounter, 0);
    }

    // Wipe-8 cue gating.
    auto wipe8 = [](GameContext& game) {
        game.flow.wipeCounter = 8;
        playingFieldWipeTick(game, noDraw);
    };
    {  // solo, normal gameplay → stack-fall noise
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        wipe8(game);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::STACK_FALL);
        EXPECT_EQ(game.flow.wipeCounter, 9);
    }
    {  // solo, any other state → no cue
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::TITLE_SCREEN;
        wipe8(game);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::NONE);
    }
    {  // two-player, garbage wipe → garbage-attack square cue
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::TWO_PLAYER_GAME;
        game.multiplayer.garbageWipeActive = true;
        wipe8(game);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::GARBAGE_ATTACK);
    }
    {  // two-player, ordinary wipe → stack-fall noise
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::TWO_PLAYER_GAME;
        game.multiplayer.garbageWipeActive = false;
        wipe8(game);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::STACK_FALL);
    }
    {  // multiplayer, not the two-player game state → no cue
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        wipe8(game);
        EXPECT_EQ(game.audioCues.noise, NoiseSfxId::NONE);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }

    // The final step re-arms the post-lock soft-drop latch.
    {
        GameContext game;
        game.flow.wipeCounter = 19;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_A;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;
        game.flow.nextPreviewPiece = Piece{0x08};
        game.engine.blockSoftDropAfterLock = false;
        playingFieldWipeTick(game, [] { return std::uint8_t{0x04}; });
        EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
        EXPECT_EQ(game.flow.wipeCounter, 0);
    }
}

// 7. Wipe19TerminalVectors — the final wipe step's branches (tetris.asm:5744-5810): the wrong-state
// returns (counter still reset, latch still armed, no spawn), the multiplayer garbage-wipe consume
// path, the Type A and lines-remaining Type B spawns, and the Type B win (hold, stage-clear music, the
// solo level-9 fork, and the multiplayer lines-goal flag with the state left alone).
TEST(LineClear, Wipe19TerminalVectors) {
    auto terminal = [](GameContext& game, int& calls) {
        game.flow.wipeCounter = 19;
        game.engine.blockSoftDropAfterLock = false;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;
        game.flow.nextPreviewPiece = Piece{0x08};
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x04; };
        playingFieldWipeTick(game, draw);
    };

    // Solo, wrong state: counter reset, latch armed, no spawn.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::TITLE_SCREEN;
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.flow.wipeCounter, 0);
        EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
        EXPECT_EQ(calls, 0);
    }
    // Multiplayer, wrong state: same.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;  // not the two-player state
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.flow.wipeCounter, 0);
        EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
        EXPECT_EQ(calls, 0);
    }
    // Multiplayer garbage-wipe: the flag is consumed and no piece spawns.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::TWO_PLAYER_GAME;
        game.multiplayer.garbageWipeActive = true;
        int calls = 0;
        terminal(game, calls);
        EXPECT_FALSE(game.multiplayer.garbageWipeActive);
        EXPECT_EQ(calls, 0);
        EXPECT_EQ(game.flow.wipeCounter, 0);
        EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
    }
    // Type A: spawns the next piece.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_A;
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::T_0);
        EXPECT_EQ(calls, 1);
    }
    // Type B with lines still to clear: spawns the next piece, no win.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 5;
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::T_0);
        EXPECT_EQ(calls, 1);
        EXPECT_NE(game.audioCues.music, MusicId::STAGE_CLEAR);
    }
    // Type B win, solo, level != 9 → the victory jingle.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 0;
        game.flow.typeBLevel = 3;
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.flow.timer1, 0x64);
        EXPECT_EQ(game.audioCues.music, MusicId::STAGE_CLEAR);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_B_VICTORY_JINGLE);
        EXPECT_EQ(calls, 0);
    }
    // Type B win, solo, level 9 → the bonus ending.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = false;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 0;
        game.flow.typeBLevel = 9;
        int calls = 0;
        terminal(game, calls);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_B_BONUS);
        EXPECT_EQ(game.flow.timer1, 0x64);
        EXPECT_EQ(game.audioCues.music, MusicId::STAGE_CLEAR);
    }
    // Type B win, multiplayer → the lines-goal flag, and the game state is left alone.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::TWO_PLAYER_GAME;
        game.multiplayer.garbageWipeActive = false;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 0;
        int calls = 0;
        terminal(game, calls);
        EXPECT_TRUE(game.multiplayer.linesGoalReached);
        EXPECT_EQ(game.flow.gameState, GameState::TWO_PLAYER_GAME);
        EXPECT_EQ(game.flow.timer1, 0x64);
        EXPECT_EQ(game.audioCues.music, MusicId::STAGE_CLEAR);
        EXPECT_EQ(calls, 0);
    }
}
