// The rising floor — behavioral tests for Type C.
//
// Device-free. The rise takes its arriving cells from a callable the caller supplies, so every test
// here injects a deterministic one and no virtual machine is built; the shipped wiring registers that
// callable on the same machine as the piece randomizer, which is a property of main.cpp, not of this
// logic.
//
// A file-local frame harness reproduces the two-context frame the rise sits in: the handler beat
// (scan and compaction) runs first, then the saturating frame-timer decrement, then the vertical-blank
// beat (flash and wipe), with the rise seam wired into the two points a lock can spawn from.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/garbage.h"          // kGarbageEmptyTile, kGarbageBlockTileBase
#include "data/playing_field.h"
#include "data/sfx.h"
#include "data/tilemaps.h"         // kTypeAGameplayTilemap
#include "data/type_c_tilemap.h"   // kTypeCGameplayTilemap
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/line_clear.h"
#include "systems/readouts.h"
#include "systems/rising_floor.h"
#include "systems/scoring.h"

namespace {

using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::kGarbageBlockTileBase;
using kirpich::kGarbageEmptyTile;
using kirpich::kPlayingFieldCols;
using kirpich::kPlayingFieldOriginCol;
using kirpich::kPlayingFieldRows;
using kirpich::kTypeCGameplayTilemap;
using kirpich::SquareSfxId;
using kirpich::systems::animateLineClear;
using kirpich::systems::armRiseCounter;
using kirpich::systems::checkForCompletedRows;
using kirpich::systems::recordLock;
using kirpich::systems::GameContext;
using kirpich::systems::kRiseCountShown;
using kirpich::systems::kTypeCRiseInterval;
using kirpich::systems::makeRiseFloorHook;
using kirpich::systems::moveBlocksDownAfterLineClear;
using kirpich::systems::playingFieldWipeTick;
using kirpich::systems::riseFloor;
using kirpich::systems::RiseFloorHook;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kBrick = 0x28;   // any non-space fills a cell
constexpr std::uint8_t kMarker = 0x29;  // a second non-space, so a moved cell is identifiable

// The bottom row of the visible field.
constexpr std::size_t kBottomRow = kPlayingFieldRows - 1;

// Fill the whole board with the empty-space tile — the playable state (boot is all-zero, which is not
// "empty" for the scan).
void spaceField(GameContext& game) {
    for (auto& row : game.field.board) {
        row.fill(kSpace);
    }
    for (auto& row : game.display.map) {
        row.fill(kSpace);
    }
}

// Complete one field row: fill its ten visible cells with a brick.
void fillFieldRow(GameContext& game, std::size_t row) {
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        game.field.fieldCell(row, col) = kBrick;
    }
}

// A cell source that answers the same tile every call.
std::function<std::uint8_t()> constantFold(std::uint8_t tile) {
    return [tile]() -> std::uint8_t { return tile; };
}

// A cell source that walks a fixed list and then repeats its last entry.
std::function<std::uint8_t()> scriptedFold(std::vector<std::uint8_t> cells) {
    auto at = std::make_shared<std::size_t>(0);
    auto seq = std::make_shared<std::vector<std::uint8_t>>(std::move(cells));
    return [at, seq]() -> std::uint8_t {
        const std::uint8_t v = (*seq)[*at];
        if (*at + 1 < seq->size()) {
            ++*at;
        }
        return v;
    };
}

// Put a context into a live Type C round with an empty field.
void typeCRound(GameContext& game) {
    game.flow.gameType = GameType::TYPE_C;
    game.flow.gameState = GameState::NORMAL_GAMEPLAY;
    spaceField(game);
}

const std::function<std::uint8_t()> noDraw = []() -> std::uint8_t { return 0; };

// A randomizer stand-in that walks the seven pieces, so a round init fills its pipeline with valid
// pieces rather than seven copies of one.
std::function<std::uint8_t()> cyclingDraw() {
    auto counter = std::make_shared<std::uint8_t>(0);
    return [counter]() -> std::uint8_t {
        const std::uint8_t v = static_cast<std::uint8_t>((*counter % 7) * 4);
        ++*counter;
        return v;
    };
}

// The frame anatomy: handler beat → saturating timer decrement → vertical-blank beat, with the rise
// seam wired where the shipped host wires it.
void frameTick(GameContext& game, const RiseFloorHook& rise) {
    checkForCompletedRows(game);
    moveBlocksDownAfterLineClear(game);
    if (game.flow.timer1 > 0) --game.flow.timer1;
    if (game.flow.timer2 > 0) --game.flow.timer2;
    animateLineClear(game, noDraw, rise);
    playingFieldWipeTick(game, noDraw, rise);
}

// Run frames until the lock sequence finishes and the wipe has run out, or the budget is spent.
void runOutTheLock(GameContext& game, const RiseFloorHook& rise, int frames = 260) {
    for (int i = 0; i < frames; ++i) {
        frameTick(game, rise);
    }
}

}  // namespace

// 1. ShiftLaw — every field row takes the row below it, row 0's contents are discarded, and nothing
// outside the visible field extent is touched.
TEST(RisingFloor, ShiftLaw) {
    GameContext game;
    typeCRound(game);

    // A distinct marker in each field row, so a row that moves is identifiable by its value.
    for (std::size_t row = 0; row < kPlayingFieldRows; ++row) {
        game.field.fieldCell(row, 0) = static_cast<std::uint8_t>(0x40 + row);
    }

    // Landmarks outside the field: the wall columns either side, and a row below the field.
    game.field.board[5][kPlayingFieldOriginCol - 1] = kMarker;
    game.field.board[5][kPlayingFieldOriginCol + kPlayingFieldCols] = kMarker;
    game.field.board[kPlayingFieldRows][kPlayingFieldOriginCol] = kMarker;
    game.field.attackRow.fill(kMarker);

    riseFloor(game, constantFold(kGarbageEmptyTile));

    // Each row now carries what the row below it carried.
    for (std::size_t row = 0; row + 1 < kPlayingFieldRows; ++row) {
        EXPECT_EQ(game.field.fieldCell(row, 0), static_cast<std::uint8_t>(0x40 + row + 1))
            << "field row " << row << " should have taken the row below it";
    }

    // Row 0's own contents are gone: nothing anywhere in the field carries the top row's marker.
    for (std::size_t row = 0; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_NE(game.field.fieldCell(row, col), static_cast<std::uint8_t>(0x40))
                << "the row pushed past the top of the field should be gone";
        }
    }

    // The landmarks outside the field extent are untouched.
    EXPECT_EQ(game.field.board[5][kPlayingFieldOriginCol - 1], kMarker);
    EXPECT_EQ(game.field.board[5][kPlayingFieldOriginCol + kPlayingFieldCols], kMarker);
    EXPECT_EQ(game.field.board[kPlayingFieldRows][kPlayingFieldOriginCol], kMarker);
    for (const std::uint8_t cell : game.field.attackRow) {
        EXPECT_EQ(cell, kMarker) << "the multiplayer attack row is not the rising floor's business";
    }
}

// 2. ArrivingRowAlwaysHasAGap — the new bottom row comes from the cell source, and a source that would
// have filled it solid gets its rightmost cell forced empty. Without that rule the row could never be
// cleared and the stack could only ever climb.
TEST(RisingFloor, ArrivingRowAlwaysHasAGap) {
    // Every cell a block: the last one is forced empty.
    {
        GameContext game;
        typeCRound(game);
        riseFloor(game, constantFold(kGarbageBlockTileBase));

        for (std::size_t col = 0; col + 1 < kPlayingFieldCols; ++col) {
            EXPECT_EQ(game.field.fieldCell(kBottomRow, col), kGarbageBlockTileBase);
        }
        EXPECT_EQ(game.field.fieldCell(kBottomRow, kPlayingFieldCols - 1), kGarbageEmptyTile)
            << "a row with no gap of its own has its last cell forced empty";
    }

    // A gap anywhere earlier and the last cell is left as the source gave it.
    for (std::size_t gapCol = 0; gapCol + 1 < kPlayingFieldCols; ++gapCol) {
        GameContext game;
        typeCRound(game);

        std::vector<std::uint8_t> cells(kPlayingFieldCols, kGarbageBlockTileBase);
        cells[gapCol] = kGarbageEmptyTile;
        riseFloor(game, scriptedFold(cells));

        EXPECT_EQ(game.field.fieldCell(kBottomRow, gapCol), kGarbageEmptyTile);
        EXPECT_EQ(game.field.fieldCell(kBottomRow, kPlayingFieldCols - 1), kGarbageBlockTileBase)
            << "the forcing rule fires only when the row would otherwise be solid (gap at " << gapCol
            << ")";
    }
}

// 3. RiseWritesBothGrids — the board and the displayed map both carry the risen field, because the
// rise appears at once rather than being carried in by a wipe.
TEST(RisingFloor, RiseWritesBothGrids) {
    GameContext game;
    typeCRound(game);

    game.field.fieldCell(10, 3) = kMarker;
    game.display.map[10][kPlayingFieldOriginCol + 3] = kMarker;

    riseFloor(game, constantFold(kGarbageBlockTileBase));

    // The marker moved up one row in both grids.
    EXPECT_EQ(game.field.fieldCell(9, 3), kMarker);
    EXPECT_EQ(game.display.map[9][kPlayingFieldOriginCol + 3], kMarker);

    // The arriving row reached both grids too.
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        EXPECT_EQ(game.display.map[kBottomRow][kPlayingFieldOriginCol + col],
                  game.field.fieldCell(kBottomRow, col))
            << "displayed map and board disagree at column " << col;
    }
}

// 4. CounterArming — a Type C round arms the counter to the interval; every other mode leaves it at
// zero, which is what keeps the seam inert for them.
TEST(RisingFloor, CounterArming) {
    for (const GameType type : {GameType::TYPE_A, GameType::TYPE_B, GameType::TYPE_C}) {
        GameContext game;
        game.flow.gameType = type;
        game.flow.riseCounter = 99;
        armRiseCounter(game);

        EXPECT_EQ(game.flow.riseCounter, type == GameType::TYPE_C ? kTypeCRiseInterval : 0);
    }
}

// 5. TheCountLosesOneAndGainsEveryClearedRow — the mode's whole difficulty curve, in one law. A lock
// costs a drop off the count; every row it cleared is credited straight back, held at a full interval.
//
// So a single line breaks even, a double gains one, and a tetris gains three. Clearing is not a
// reprieve from the floor - it is the only thing holding the floor off.
TEST(RisingFloor, TheCountLosesOneAndGainsEveryClearedRow) {
    struct Vector {
        std::uint8_t before;
        std::uint8_t cleared;
        std::uint8_t after;
        const char*  why;
    };
    for (const Vector v : {
             Vector{5, 0, 4, "a drop that cleared nothing brings the floor closer"},
             Vector{5, 1, 5, "one line a drop is breaking even"},
             Vector{5, 2, 6, "a double gains one"},
             Vector{5, 3, 7, "a triple gains two"},
             Vector{5, 4, 8, "a tetris gains three"},
             // The starting count is not a ceiling: a player who clears faster than they drop banks
             // the difference and keeps it.
             Vector{kTypeCRiseInterval, 2, kTypeCRiseInterval + 1, "a double past the start banks one"},
             Vector{kTypeCRiseInterval, 4, kTypeCRiseInterval + 3, "and a tetris banks three"},
             Vector{kRiseCountShown, 4, kRiseCountShown, "the panel's two digits are the only limit"},
             Vector{kRiseCountShown - 1, 4, kRiseCountShown, "and the count stops there, not past it"},
             Vector{0, 0, 0, "the count stops at zero rather than wrapping"},
             Vector{1, 0, 0, "the drop that empties it"},
         }) {
        GameContext game;
        typeCRound(game);
        game.flow.riseCounter = v.before;

        recordLock(game, v.cleared);

        EXPECT_EQ(game.flow.riseCounter, v.after)
            << v.why << " (from " << int{v.before} << " clearing " << int{v.cleared} << ")";
    }

    // Through the real scan, so the count the law is given is the one the scan found.
    {
        GameContext game;
        typeCRound(game);
        game.flow.riseCounter = 5;
        game.flow.pieceLockStage = 2;
        fillFieldRow(game, 16);
        fillFieldRow(game, 17);

        checkForCompletedRows(game);

        EXPECT_EQ(game.flow.completedRowCount, 2);
        EXPECT_EQ(game.flow.riseCounter, 6) << "two rows cleared: one off for the drop, two back on";
    }

    // And it is inert outside a Type C round.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.riseCounter = 5;
        recordLock(game, 0);
        EXPECT_EQ(game.flow.riseCounter, 5);

        armRiseCounter(game);
        EXPECT_EQ(game.flow.riseCounter, 0) << "no other mode carries a rise";
    }
}

// 6. RiseFiresAtTheSpawnPointAndReloads — with the counter at zero the floor comes up at the next
// spawn, and the counter goes back to a full interval.
TEST(RisingFloor, RiseFiresAtTheSpawnPointAndReloads) {
    GameContext game;
    typeCRound(game);
    game.flow.riseCounter = 1;   // this lock empties it
    game.flow.pieceLockStage = 2;
    game.field.fieldCell(10, 3) = kMarker;

    const auto rise = makeRiseFloorHook(constantFold(kGarbageBlockTileBase));
    runOutTheLock(game, rise);

    EXPECT_EQ(game.field.fieldCell(9, 3), kMarker) << "the stack moved up a row";
    EXPECT_EQ(game.flow.riseCounter, kTypeCRiseInterval) << "the counter reloaded";
    EXPECT_NE(game.field.fieldCell(kBottomRow, 0), kSpace) << "a row arrived at the bottom";
}

// 7. ASingleLineOnlyBreaksEven — the correction to a rule that made the floor unreachable. A lock that
// clears one row leaves the count exactly where it was, so the drop is paid for and nothing is gained;
// the floor still arrives on schedule for a player clearing one line at a time.
TEST(RisingFloor, ASingleLineOnlyBreaksEven) {
    GameContext game;
    typeCRound(game);
    game.flow.riseCounter = 1;   // the next drop empties the count
    game.flow.pieceLockStage = 2;
    fillFieldRow(game, 17);
    game.field.fieldCell(5, 0) = kMarker;

    const auto rise = makeRiseFloorHook(constantFold(kGarbageBlockTileBase));
    runOutTheLock(game, rise);

    EXPECT_EQ(game.flow.lines, 1) << "the row was cleared and counted";
    EXPECT_EQ(game.flow.riseCounter, 1) << "one line bought back exactly the drop it cost";

    // The stack settled downward into the row that cleared, which is the compaction doing its job, and
    // no floor came up: the count never reached zero.
    EXPECT_EQ(game.field.fieldCell(6, 0), kMarker) << "the stack fell into the cleared row";
    EXPECT_EQ(game.field.fieldCell(kBottomRow, 0), kSpace) << "no row arrived at the bottom";
}

// 7b. TheCountReachesZeroThroughEmptyDrops — drops that clear nothing do bring the floor, and when the
// count runs out it comes up at the next spawn.
TEST(RisingFloor, TheCountReachesZeroThroughEmptyDrops) {
    GameContext game;
    typeCRound(game);
    armRiseCounter(game);
    game.field.fieldCell(5, 0) = kMarker;

    const auto rise = makeRiseFloorHook(constantFold(kGarbageBlockTileBase));

    // One drop short of the interval: the floor has not moved.
    for (std::uint8_t i = 0; i < kTypeCRiseInterval - 1; ++i) {
        game.flow.pieceLockStage = 2;
        runOutTheLock(game, rise, 4);
    }
    EXPECT_EQ(game.field.fieldCell(5, 0), kMarker) << "not yet";

    // The drop that empties the count, and the spawn that acts on it.
    game.flow.pieceLockStage = 2;
    runOutTheLock(game, rise);

    EXPECT_EQ(game.field.fieldCell(4, 0), kMarker) << "the stack moved up a row";
    EXPECT_NE(game.field.fieldCell(kBottomRow, 0), kSpace) << "and a row arrived below";
    EXPECT_EQ(game.flow.riseCounter, kTypeCRiseInterval);
}

// 8. NoSeamNoRise — the seam defaults to empty, and an empty seam raises no floor. This is what every
// other mode and every test that predates Type C gets.
TEST(RisingFloor, NoSeamNoRise) {
    GameContext game;
    typeCRound(game);
    game.flow.riseCounter = 1;
    game.flow.pieceLockStage = 2;
    game.field.fieldCell(10, 3) = kMarker;

    runOutTheLock(game, RiseFloorHook{});

    EXPECT_EQ(game.field.fieldCell(10, 3), kMarker) << "nothing moved";
    EXPECT_EQ(game.field.fieldCell(kBottomRow, 0), kSpace) << "no row arrived";
}

// 9. RiseGates — the seam fires only in a Type C round, only with the counter at zero, and only during
// normal gameplay.
TEST(RisingFloor, RiseGates) {
    const auto rise = makeRiseFloorHook(constantFold(kGarbageBlockTileBase));

    // Wrong mode.
    {
        GameContext game;
        typeCRound(game);
        game.flow.gameType = GameType::TYPE_A;
        game.flow.riseCounter = 0;
        rise(game);
        EXPECT_EQ(game.field.fieldCell(kBottomRow, 0), kSpace);
    }

    // Counter not yet empty.
    {
        GameContext game;
        typeCRound(game);
        game.flow.riseCounter = 1;
        rise(game);
        EXPECT_EQ(game.field.fieldCell(kBottomRow, 0), kSpace);
        EXPECT_EQ(game.flow.riseCounter, 1) << "and it was not reloaded either";
    }

    // Not in play.
    {
        GameContext game;
        typeCRound(game);
        game.flow.gameState = GameState::GAME_OVER_SCREEN;
        game.flow.riseCounter = 0;
        rise(game);
        EXPECT_EQ(game.field.fieldCell(kBottomRow, 0), kSpace);
    }

    // All three met.
    {
        GameContext game;
        typeCRound(game);
        game.flow.riseCounter = 0;
        rise(game);
        EXPECT_NE(game.field.fieldCell(kBottomRow, 0), kSpace);
    }
}

// 10. RiseCuesTheArrivingRow — the floor coming up makes the same noise garbage arriving makes.
TEST(RisingFloor, RiseCuesTheArrivingRow) {
    GameContext game;
    typeCRound(game);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    riseFloor(game, constantFold(kGarbageBlockTileBase));

    EXPECT_EQ(game.audioCues.square, SquareSfxId::GARBAGE_ATTACK);
}

// 11. ReadoutCountsDown — the panel's RISE cells carry the counter, two digits, and follow it down.
TEST(RisingFloor, ReadoutCountsDown) {
    GameContext game;
    typeCRound(game);

    // The interval, as two digits.
    game.flow.riseCounter = kTypeCRiseInterval;
    kirpich::systems::printRise(game, game.display.map);
    EXPECT_EQ(game.display.map[8][16], kTypeCRiseInterval / 10);
    EXPECT_EQ(game.display.map[8][17], kTypeCRiseInterval % 10);

    // A single digit blanks its leading cell rather than printing a zero there.
    game.flow.riseCounter = 7;
    kirpich::systems::printRise(game, game.display.map);
    EXPECT_EQ(game.display.map[8][16], kSpace);
    EXPECT_EQ(game.display.map[8][17], 7);

    // Zero still reads as a zero, not as a blank.
    game.flow.riseCounter = 0;
    kirpich::systems::printRise(game, game.display.map);
    EXPECT_EQ(game.display.map[8][16], kSpace);
    EXPECT_EQ(game.display.map[8][17], 0);

    // The rise itself puts a full interval back on the panel.
    game.flow.riseCounter = 0;
    makeRiseFloorHook(constantFold(kGarbageBlockTileBase))(game);
    EXPECT_EQ(game.display.map[8][16], kTypeCRiseInterval / 10);
    EXPECT_EQ(game.display.map[8][17], kTypeCRiseInterval % 10);
}

// 11b. ThePanelFollowsTheCountDown — every lock that moves the counter moves the number on the panel.
//
// The regression guard for a defect the suite was green over: the counter was only ever drawn by the
// round init and by the rise that reloads it, so it read as a fixed ten for the whole interval and the
// player was given no warning at all.
TEST(RisingFloor, ThePanelFollowsTheCountDown) {
    GameContext game;
    typeCRound(game);
    armRiseCounter(game);
    printRise(game, game.display.map);

    const auto shown = [&] {
        const std::uint8_t tens = game.display.map[8][16];
        const std::uint8_t ones = game.display.map[8][17];
        return tens == kSpace ? ones : static_cast<std::uint8_t>(tens * 10 + ones);
    };
    ASSERT_EQ(shown(), kTypeCRiseInterval);

    // Lock by lock, all the way down to the last piece before the floor comes up.
    for (std::uint8_t remaining = kTypeCRiseInterval; remaining > 0; --remaining) {
        game.flow.pieceLockStage = 2;
        checkForCompletedRows(game);

        EXPECT_EQ(game.flow.riseCounter, remaining - 1);
        EXPECT_EQ(shown(), game.flow.riseCounter)
            << "the panel is showing " << int{shown()} << " with " << int{game.flow.riseCounter}
            << " pieces to go";
    }
}

// 12. RoundInit — a Type C round opens on its own backdrop with its own starting level, an empty
// field, and the counter armed after the pipeline draws rather than before them.
TEST(RisingFloor, RoundInit) {
    GameContext game;
    game.flow.gameType = GameType::TYPE_C;
    game.flow.typeALevel = 3;   // the other modes' choices must not leak in
    game.flow.typeBLevel = 5;
    game.flow.typeCLevel = 8;

    kirpich::systems::initGame(game, cyclingDraw());

    EXPECT_EQ(game.flow.level, 8) << "the level comes from Type C's own choice";
    EXPECT_EQ(game.flow.lines, 0) << "a marathon counts up from none";
    EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);

    // Type C's backdrop, not Type A's: the RISE label exists on this screen and on no other.
    EXPECT_EQ(game.display.map[7][14], kTypeCGameplayTilemap[7][14]);
    EXPECT_EQ(game.display.secondMap[7][14], kTypeCGameplayTilemap[7][14]);
    EXPECT_NE(game.display.map[7][14], kirpich::kTypeAGameplayTilemap[7][14])
        << "the Type A backdrop has nothing at the RISE label's cells";

    // The level landed in Type C's cell, not Type A's.
    EXPECT_EQ(game.display.map[6][16], 8);

    // The counter is armed, and to a whole interval — the three pipeline draws above did not spend any
    // of the player's first one.
    EXPECT_EQ(game.flow.riseCounter, kTypeCRiseInterval);

    // And it is on the panel in both maps.
    EXPECT_EQ(game.display.map[8][17], kTypeCRiseInterval % 10);
    EXPECT_EQ(game.display.secondMap[8][17], kTypeCRiseInterval % 10);

    // The field is empty: the floor comes to the player rather than being there to begin with.
    for (std::size_t row = 0; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_EQ(game.field.fieldCell(row, col), kSpace)
                << "a Type C round starts on an empty field (" << row << ", " << col << ")";
        }
    }
}

// 13. ScoringForksTakeTheMarathonArm — every place the code asks which mode is running, Type C is
// asserted from its own side: it accumulates lines rather than counting them down, it is awarded as
// the round runs, it climbs levels, and it never reaches a line goal.
TEST(RisingFloor, ScoringForksTakeTheMarathonArm) {
    // Lines accumulate.
    {
        GameContext game;
        typeCRound(game);
        game.flow.pieceLockStage = 2;
        game.flow.lines = 40;
        fillFieldRow(game, 17);

        checkForCompletedRows(game);

        EXPECT_EQ(game.flow.lines, 41) << "Type C counts lines up, as Type A does";
    }

    // The award lands on the score while the round runs.
    {
        GameContext game;
        typeCRound(game);
        game.flow.wipeCounter = 5;
        game.flow.level = 0;
        game.engine.stats.singles = 1;
        game.engine.score = 0;

        kirpich::systems::addLineClearScore(game);

        EXPECT_GT(game.engine.score, 0u) << "Type C is awarded live, as Type A is";
        EXPECT_EQ(game.engine.stats.singles, 0) << "and the pending clear was consumed";
    }

    // The level climbs.
    {
        GameContext game;
        typeCRound(game);
        game.flow.level = 0;
        game.flow.lines = 10;

        kirpich::systems::checkForLevelUp(game);

        EXPECT_EQ(game.flow.level, 1) << "Type C levels up under the Type A law";
    }

    // And the spawn point has no line goal to stop at: the round runs on with the lines at zero, which
    // is exactly where a Type B round would end.
    {
        GameContext game;
        typeCRound(game);
        game.flow.lines = 0;
        game.flow.wipeCounter = 19;

        playingFieldWipeTick(game, noDraw);

        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY)
            << "a marathon does not finish when its line count reads zero";
        EXPECT_NE(game.flow.timer1, 0x64) << "and it does not start a Type B victory";
    }
}
