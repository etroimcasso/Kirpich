// The displayed map — behavioral tests against docs/contracts/screen.md.
//
// Device-free. The hardware keeps two grids: the board, which is the game's own copy of the playing
// field and what collision and locking read, and the displayed map, which is what reaches the screen.
// These tests pin which writes reach which grid, and the three effects that exist only in the gap
// between them. Every asserted address is traced to the tetris.asm line that names it.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/line_clear_kind.h>
#include <kirpich/sprite_id.h>

#include "data/playing_field.h"
#include "data/scoring.h"
#include "state/display_state.h"
#include "systems/game_context.h"
#include "systems/line_clear.h"
#include "systems/piece.h"
#include "systems/scoring.h"
#include "vm/garbage_fill.h"

using kirpich::CharTile;
using kirpich::GameType;
using kirpich::LineClearKind;
using kirpich::kPlayingFieldCols;
using kirpich::kPlayingFieldOriginCol;
using kirpich::kPlayingFieldWipeCounterFirst;
using kirpich::kPlayingFieldWipeCounterLast;
using kirpich::playingFieldRowForWipeCounter;
using kirpich::SpriteId;
using kirpich::systems::GameContext;

namespace {

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);

// A draw source the pipeline can call; none of these tests reach a path that spawns from it.
std::function<std::uint8_t()> noDraw() {
    return [] { return std::uint8_t{0}; };
}

// Fill the board's field region with a value that encodes its own cell, so a row carried to the wrong
// place is visible rather than merely different.
void seedBoard(GameContext& game) {
    for (std::size_t row = 0; row < kirpich::kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            game.field.fieldCell(row, col) = static_cast<std::uint8_t>(0x40 + row * 16 + col);
        }
    }
}

}  // namespace

// ── Test 1: TheWipeCarriesOneRowPerFrame ──────────────────────────────────────────────────────────
// WipePlayingFieldRow (tetris.asm:5896-5908) copies ten cells from the board to the map; each of the
// eighteen dispatchers passes the row's map address and the board address $3000 above it (:5567-5568
// for the first). The counter runs bottom row first. This is the whole of the wipe: the board already
// holds the new contents, and the animation is the map catching up.
TEST(DisplayMap, TheWipeCarriesOneRowPerFrame) {
    GameContext game;
    seedBoard(game);
    game.flow.wipeCounter = kPlayingFieldWipeCounterFirst;

    // Step by step, the map gains exactly one field row per call, in the counter's own order.
    for (std::uint8_t step = kPlayingFieldWipeCounterFirst; step <= kPlayingFieldWipeCounterLast;
         ++step) {
        const std::size_t carried = playingFieldRowForWipeCounter(step);
        kirpich::systems::playingFieldWipeTick(game, noDraw());

        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_EQ(game.display.map[carried][kPlayingFieldOriginCol + col],
                      game.field.fieldCell(carried, col))
                << "step " << int{step} << " column " << col;
        }
    }

    // The first step carries the bottom row and the last the top: the sweep climbs.
    EXPECT_EQ(playingFieldRowForWipeCounter(kPlayingFieldWipeCounterFirst),
              kirpich::kPlayingFieldRows - 1);
    EXPECT_EQ(playingFieldRowForWipeCounter(kPlayingFieldWipeCounterLast), 0u);

    // A counter outside the wipe's range carries nothing.
    GameContext idle;
    seedBoard(idle);
    idle.flow.wipeCounter = 0;
    const auto blank = idle.display.map;
    kirpich::systems::playingFieldWipeTick(idle, noDraw());
    EXPECT_EQ(idle.display.map, blank) << "no wipe is running, so nothing is carried";
}

// ── Test 2: TheFlashCoversTheMapAndRestoresFromTheBoard ───────────────────────────────────────────
// AnimateLineClear (tetris.asm:5419-5447) derives its destination by subtracting $30 from the board's
// high byte, so it paints the map alone. An even pass covers each clearing row with a solid block; an
// odd pass puts the row's real contents back, read out of the board — which is why the board must not
// change. The seventh pass covers with empty space, leaving the rows blank.
TEST(DisplayMap, TheFlashCoversTheMapAndRestoresFromTheBoard) {
    constexpr std::uint8_t kFlashBlock = 0x8C;
    constexpr std::uint8_t kClearedRow = 9;

    GameContext game;
    seedBoard(game);
    game.flow.pieceLockStage = 3;
    game.flow.timer1 = 0;
    game.engine.lineClears = kirpich::BoundedVec<std::uint8_t, 4>{kClearedRow};

    const auto boardBefore = game.field.board;

    // Pass 0 covers.
    kirpich::systems::animateLineClear(game, noDraw());
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        EXPECT_EQ(game.display.map[kClearedRow][kPlayingFieldOriginCol + col], kFlashBlock)
            << "column " << col;
    }
    EXPECT_EQ(game.field.board, boardBefore) << "the flash never writes the board";

    // Pass 1 restores from the board.
    game.flow.timer1 = 0;
    kirpich::systems::animateLineClear(game, noDraw());
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        EXPECT_EQ(game.display.map[kClearedRow][kPlayingFieldOriginCol + col],
                  game.field.fieldCell(kClearedRow, col))
            << "column " << col;
    }
    EXPECT_EQ(game.field.board, boardBefore);

    // The final covering pass uses empty space rather than the block, so the row ends blank.
    while (game.flow.blinkCounter != 6) {
        game.flow.timer1 = 0;
        kirpich::systems::animateLineClear(game, noDraw());
        ASSERT_EQ(game.flow.pieceLockStage, 3) << "the flash is still running";
    }
    game.flow.timer1 = 0;
    kirpich::systems::animateLineClear(game, noDraw());
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        EXPECT_EQ(game.display.map[kClearedRow][kPlayingFieldOriginCol + col], kSpace)
            << "column " << col;
    }
    EXPECT_EQ(game.field.board, boardBefore) << "seven passes and the board is untouched";
}

// ── Test 3: GarbageReachesBothGrids ───────────────────────────────────────────────────────────────
// InitGarbage writes each cell twice — once where it is walking, then again $3000 above it
// (tetris.asm:4371-4381) — so the garbage appears the moment it is generated rather than waiting for a
// wipe. The second write is skipped in a link-cable game (:4374-4376), where the rows are staged.
TEST(DisplayMap, GarbageReachesBothGrids) {
    const std::uint8_t height = 5;

    GameContext solo;
    solo.flow.typeBStartHeight = height;
    const std::size_t top = kirpich::vm::typeBGarbageTopRow(height);
    kirpich::vm::initGarbage(solo, [] { return std::uint8_t{0x84}; }, top);
    bool anyGarbage = false;
    for (std::size_t row = top; row < kirpich::kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_EQ(solo.display.map[row][kPlayingFieldOriginCol + col],
                      solo.field.fieldCell(row, col))
                << "row " << row << " column " << col;
            if (solo.field.fieldCell(row, col) != kirpich::kGarbageEmptyTile) {
                anyGarbage = true;
            }
        }
    }
    EXPECT_TRUE(anyGarbage) << "the fill should have written something";

    // A link-cable game stages the rows in the board and shows nothing.
    GameContext linked;
    linked.multiplayer.isMultiplayer = true;
    linked.flow.typeBStartHeight = height;
    kirpich::vm::initGarbage(linked, [] { return std::uint8_t{0x84}; },
                             kirpich::vm::typeBGarbageTopRow(height));
    for (const auto& row : linked.display.map) {
        for (const std::uint8_t cell : row) {
            ASSERT_EQ(cell, 0u) << "a link-cable fill shows nothing";
        }
    }
    EXPECT_NE(linked.field.board, GameContext{}.field.board) << "but it does stage the board";
}

// ── Test 4: LockingShowsAtOnce ────────────────────────────────────────────────────────────────────
// The lock writes the board and then the same tiles into video memory under an HBlank wait
// (tetris.asm:6072-6098): a piece that comes to rest has to appear immediately, with no wipe to carry
// it.
TEST(DisplayMap, LockingShowsAtOnce) {
    GameContext game;
    game.flow.pieceLockStage = 1;
    game.spriteRenderer.slots[kirpich::kActivePieceSlot].spriteId = SpriteId::L_0;
    game.spriteRenderer.slots[kirpich::kActivePieceSlot].y = 0x40;
    game.spriteRenderer.slots[kirpich::kActivePieceSlot].x = 0x3F;

    // Captured before the lock: locking hides the piece, and a hidden descriptor's cells resolve to
    // the off-screen row rather than the ones that were just written.
    const auto cells = kirpich::systems::activePieceCells(game);

    kirpich::systems::lockPieceIntoBackground(game);

    for (const auto& cell : cells) {
        EXPECT_EQ(game.field.board[cell.row][cell.col], cell.tile);
        EXPECT_EQ(game.display.map[cell.row][cell.col], cell.tile)
            << "cell " << int{cell.row} << "," << int{cell.col} << " must show at once";
    }
}

// ── Test 5: TheResultsScreenPrintsToTheMap ────────────────────────────────────────────────────────
// The end-of-round tally prints into the displayed screen, never the board: no wipe runs during a
// tally, so a print into the board could never reach the screen. The four line-clear lines are the
// addresses the tally selects per kind (tetris.asm:4888, :4893, :4898, :4903), the drop count has its
// own line (:4866), and the running score is at :6167.
TEST(DisplayMap, TheResultsScreenPrintsToTheMap) {
    struct Row {
        LineClearKind kind;
        std::uint8_t state;
        std::size_t row;  // ($98xx - $9800) / 32
    };
    // $9823 / $9883 / $98E3 / $9943.
    const Row rows[] = {
        {LineClearKind::SINGLE, 0, 1},
        {LineClearKind::DOUBLE, 1, 4},
        {LineClearKind::TRIPLE, 2, 7},
        {LineClearKind::TETRIS, 3, 10},
    };
    constexpr std::size_t kCountCol = 3;   // ($9823 - $9800) % 32
    constexpr std::size_t kScoreCol = 6;   // the kind's score sits at bc + $23: next row, column 6

    for (const Row& r : rows) {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.typeBLevel = 0;
        game.engine.scoreboardState = r.state;
        game.engine.scoreboardTallyPhase = 1;
        // Seed the kind's pending count directly; the tally moves one unit of it per pass.
        switch (r.kind) {
            case LineClearKind::SINGLE: game.engine.stats.singles = 3; break;
            case LineClearKind::DOUBLE: game.engine.stats.doubles = 3; break;
            case LineClearKind::TRIPLE: game.engine.stats.triples = 3; break;
            case LineClearKind::TETRIS: game.engine.stats.tetrises = 3; break;
        }

        // One unit moves, then the print pass draws it.
        kirpich::systems::updateScoreboard(game);
        game.engine.scoreboardTallyPhase = 2;
        kirpich::systems::updateScoreboard(game);

        EXPECT_EQ(game.display.map[r.row][kCountCol], 1u) << "one unit counted, units digit";
        // The tens digit is left alone while it is zero, so a blank stays blank (:6124-6130).
        EXPECT_EQ(game.display.map[r.row][kCountCol - 1], 0u);

        // The kind's running score, six digits on the next row.
        const std::uint32_t base = kirpich::kLineClearScores[static_cast<std::size_t>(r.kind)].points;
        std::uint32_t value = base;
        for (std::size_t i = 0; i < 6; ++i) {
            EXPECT_EQ(game.display.map[r.row + 1][kScoreCol + 5 - i],
                      static_cast<std::uint8_t>(value % 10))
                << "kind score digit " << i;
            value /= 10;
        }

        // The board is not where any of this goes.
        EXPECT_EQ(game.field.board, GameContext{}.field.board) << "the tally never writes the board";
    }

    // The drop count has its own line at $99A5 — row 13, column 5.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.engine.scoreboardState = 4;
        game.engine.scoreboardTallyPhase = 1;
        game.engine.softDropPoints = 7;

        kirpich::systems::updateScoreboard(game);

        EXPECT_EQ(game.engine.softDropPointsTallied, 1u);
        EXPECT_EQ(game.display.map[13][5 + 5], 1u) << "the units digit of the drop count";
        EXPECT_EQ(game.field.board, GameContext{}.field.board);
    }
}
