// Starting garbage — behavioral tests against docs/contracts/garbage-init.md.
//
// The per-cell pick runs on a ROM-less, headless SM83 VM; these tests construct one the same way the
// piece randomizer's tests do. The divider cannot be written from this surface, so the exact
// byte -> cell map is pinned on a mirror of the pick and the VM side is checked against that
// relation's image, its determinism, and the intra-call advancement the pick exists to preserve.
//
// The fill itself takes the pick as a plain callable, so the walk, the row extents and the
// one-gap-per-row rule are driven with stub picks that make each case exact.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "data/garbage.h"
#include "data/playing_field.h"
#include "retropp/asset_registry.h"
#include "retropp/timing.h"
#include "retropp/vm.h"
#include "state/game_flow_state.h"
#include "systems/game_context.h"
#include "vm/garbage_fill.h"
#include "vm/piece_random.h"

namespace {

using kirpich::kGarbageBlockTileBase;
using kirpich::kGarbageBlockTileCount;
using kirpich::kGarbageEmptyTile;
using kirpich::kPlayingFieldCols;
using kirpich::kPlayingFieldRows;
using kirpich::kTypeBDemoGarbage;
using kirpich::kTypeBDemoGarbageRows;
using kirpich::systems::GameContext;
using kirpich::vm::initDemoGarbage;
using kirpich::vm::initGarbage;
using kirpich::vm::kDemoGarbageTopRow;
using kirpich::vm::kMultiplayerGarbageTopRow;
using kirpich::vm::makeInitGarbageHook;
using kirpich::vm::registerGarbageFold;
using kirpich::vm::typeBGarbageTopRow;

// A DMG-timed, ROM-less VM — the machine the pick is hosted on. The asset root is pointed at the
// project tree so registerGarbageFold's routine load resolves src/vm/garbage.asm during the test
// (development builds define KIRPICH_PROJECT_ROOT). The routine bytes are assembled from that source
// by the same assembler that bakes the shipped copy, so the behavior under test is identical.
retropp::Vm makeVm() {
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
    return retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
}

constexpr std::uint64_t kCyclesPerTick = retropp::TimingProfile::GameBoy.cpuCyclesPerTick();

// The pick's gap-or-block choice as docs/contracts/garbage-init.md derives it
// (tetris.asm:4332-4343): the countdown starts at the divider byte and toggles the answer once per
// step, so the answer is the byte's parity. A read of 0 runs 256 steps (it decrements to 255 before
// the byte reaches 0), which is an even count and so answers a gap.
constexpr bool pickIsBlock(std::uint8_t divider) { return (divider & 1u) != 0u; }

constexpr std::uint8_t kBlockTileLast =
    static_cast<std::uint8_t>(kGarbageBlockTileBase + kGarbageBlockTileCount - 1);

bool isBlockTile(std::uint8_t cell) {
    return cell >= kGarbageBlockTileBase && cell <= kBlockTileLast;
}

// A pick that always answers the same block tile: every forced gap in the result is the
// one-gap-per-row rule firing, never the pick's own choice.
std::function<std::uint8_t()> alwaysBlock(std::uint8_t tile = kGarbageBlockTileBase + 1) {
    return [tile] { return tile; };
}

// A pick that answers a gap only at column `gapCol` of every row, and a block everywhere else.
std::function<std::uint8_t()> gapAtColumn(std::size_t gapCol) {
    auto col = std::make_shared<std::size_t>(0);
    return [col, gapCol] {
        const std::size_t here = (*col)++ % kPlayingFieldCols;
        return here == gapCol ? kGarbageEmptyTile
                              : static_cast<std::uint8_t>(kGarbageBlockTileBase + 1);
    };
}

// Every board cell outside the given field-row range, as a flat snapshot, for asserting that a fill
// touched nothing else on the board.
std::vector<std::uint8_t> boardOutsideRows(const GameContext& game, std::size_t firstRow,
                                           std::size_t lastRow) {
    std::vector<std::uint8_t> out;
    for (std::size_t row = 0; row < kirpich::kBoardRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kBoardCols; ++col) {
            const bool insideRows = row >= firstRow && row <= lastRow;
            const bool insideCols = col >= kirpich::kPlayingFieldOriginCol &&
                                    col < kirpich::kPlayingFieldOriginCol + kPlayingFieldCols;
            if (!(insideRows && insideCols)) {
                out.push_back(game.field.board[row][col]);
            }
        }
    }
    return out;
}

}  // namespace

// The pick — the routine registers, every answer is a legal cell, both outcomes occur, and the
// parity relation the contract derives is pinned against hand-traced vectors.
TEST(GarbageFill, PickAnswersLegalCells) {
    // The relation, from the contract's trace of the countdown.
    EXPECT_TRUE(pickIsBlock(1));    // one step, odd -> the block arm
    EXPECT_FALSE(pickIsBlock(2));   // two steps, even -> the gap arm
    EXPECT_TRUE(pickIsBlock(255));
    EXPECT_FALSE(pickIsBlock(0));   // the 256-step path, even -> a gap

    auto vm = makeVm();
    const auto fold = registerGarbageFold(vm);

    int gaps = 0;
    int blocks = 0;
    for (int i = 0; i < 512; ++i) {
        const std::uint8_t cell = fold();
        const bool gap = cell == kGarbageEmptyTile;
        ASSERT_TRUE(gap || isBlockTile(cell)) << "answer " << i << " outside the cell domain: "
                                              << int(cell);
        gap ? ++gaps : ++blocks;
        vm.advanceClock(kCyclesPerTick);
    }
    EXPECT_GT(gaps, 0) << "the gap arm never answered — the parity branch is stuck";
    EXPECT_GT(blocks, 0) << "the block arm never answered — the parity branch is stuck";
}

// Determinism and the intra-call advancement quirk: a reset machine on a fixed schedule repeats its
// answers, and two picks with no host advance between them can differ, because the countdown's own
// cycles tick the divider. A frozen byte source would make every back-to-back pair equal.
TEST(GarbageFill, PickIsDeterministicAndAdvancesWithinACall) {
    auto vm = makeVm();
    const auto fold = registerGarbageFold(vm);

    auto capture = [&] {
        vm.reset();
        std::vector<std::uint8_t> seq;
        for (int i = 0; i < 64; ++i) {
            seq.push_back(fold());
            vm.advanceClock(kCyclesPerTick);
        }
        return seq;
    };
    const std::vector<std::uint8_t> a = capture();
    const std::vector<std::uint8_t> b = capture();
    EXPECT_EQ(a, b);
    EXPECT_GT(std::set<std::uint8_t>(a.begin(), a.end()).size(), 1u)
        << "the answer never varied — the divider is not feeding the pick";

    vm.reset();
    int differ = 0;
    for (int i = 0; i < 64; ++i) {
        const std::uint8_t first = fold();
        const std::uint8_t second = fold();  // immediately, no advanceClock
        if (first != second) {
            ++differ;
        }
        vm.advanceClock(kCyclesPerTick);
    }
    EXPECT_GT(differ, 0) << "back-to-back picks never differed — the divider froze within the call";
}

// The demo stamp — all 40 cells match the table at the bottom of the field, and nothing else on the
// board is touched.
TEST(GarbageFill, DemoStampMatchesTheTable) {
    GameContext game;
    const std::vector<std::uint8_t> before =
        boardOutsideRows(game, kDemoGarbageTopRow, kPlayingFieldRows - 1);

    initDemoGarbage(game);

    for (std::size_t row = 0; row < kTypeBDemoGarbageRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_EQ(game.field.fieldCell(kDemoGarbageTopRow + row, col),
                      kTypeBDemoGarbage[row][col])
                << "demo cell (" << row << "," << col << ")";
        }
    }
    EXPECT_EQ(boardOutsideRows(game, kDemoGarbageTopRow, kPlayingFieldRows - 1), before);

    // The table sits exactly where a Type B height of two would fill.
    EXPECT_EQ(kDemoGarbageTopRow, typeBGarbageTopRow(2));
    EXPECT_EQ(kDemoGarbageTopRow, 14u);
}

// Type B extents — every height fills from its top row to the bottom of the field, and nothing above
// it or outside the field columns moves.
TEST(GarbageFill, TypeBGeometryCoversItsRows) {
    for (std::uint8_t height = 1; height <= 5; ++height) {
        GameContext game;
        const std::size_t topRow = typeBGarbageTopRow(height);
        EXPECT_EQ(topRow, kPlayingFieldRows - 2u * height) << "height " << int(height);

        const std::vector<std::uint8_t> before =
            boardOutsideRows(game, topRow, kPlayingFieldRows - 1);
        initGarbage(game, alwaysBlock(), topRow);

        for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
            for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
                EXPECT_NE(game.field.fieldCell(row, col), 0u)
                    << "height " << int(height) << " left (" << row << "," << col << ") unwritten";
            }
        }
        EXPECT_EQ(boardOutsideRows(game, topRow, kPlayingFieldRows - 1), before)
            << "height " << int(height) << " wrote outside its rows";
    }

    // A height that would bury the whole field stops at the top row; a height of zero writes nothing.
    EXPECT_EQ(typeBGarbageTopRow(9), 0u);
    EXPECT_EQ(typeBGarbageTopRow(20), 0u);
    GameContext empty;
    const GameContext untouched;
    initGarbage(empty, alwaysBlock(), typeBGarbageTopRow(0));
    EXPECT_EQ(empty.field, untouched.field);
}

// Multiplayer extents — the round-start fill covers its top row to the bottom of the field. The row
// count the original passes chooses where the fill starts, not how many rows it writes: six rows
// counted from row 13 climbs five and then fills ten.
TEST(GarbageFill, MultiplayerGeometryCoversTenRows) {
    EXPECT_EQ(kMultiplayerGarbageTopRow, 8u);
    EXPECT_EQ(kPlayingFieldRows - kMultiplayerGarbageTopRow, 10u);
    EXPECT_EQ(kMultiplayerGarbageTopRow, typeBGarbageTopRow(5))
        << "a multiplayer round start covers the same rows as a Type B height of five";

    GameContext game;
    const std::vector<std::uint8_t> before =
        boardOutsideRows(game, kMultiplayerGarbageTopRow, kPlayingFieldRows - 1);
    initGarbage(game, alwaysBlock(), kMultiplayerGarbageTopRow);

    for (std::size_t row = kMultiplayerGarbageTopRow; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_NE(game.field.fieldCell(row, col), 0u) << "(" << row << "," << col << ")";
        }
    }
    EXPECT_EQ(boardOutsideRows(game, kMultiplayerGarbageTopRow, kPlayingFieldRows - 1), before);
}

// One gap per row — forced when the picks would have filled a row solid, and not forced when a gap
// already landed, at every column it could land in.
TEST(GarbageFill, EveryRowKeepsAGap) {
    const std::size_t topRow = typeBGarbageTopRow(5);

    // Solid picks: the rightmost cell of every row is forced empty, and it is the only gap.
    {
        GameContext game;
        initGarbage(game, alwaysBlock(), topRow);
        for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
            EXPECT_EQ(game.field.fieldCell(row, kPlayingFieldCols - 1), kGarbageEmptyTile)
                << "row " << row << " was left solid";
            for (std::size_t col = 0; col + 1 < kPlayingFieldCols; ++col) {
                EXPECT_NE(game.field.fieldCell(row, col), kGarbageEmptyTile)
                    << "row " << row << " gained a gap the picks did not ask for";
            }
        }
    }

    // A gap at any column suppresses the force: the rightmost cell keeps the picked block, except
    // when the gap IS the rightmost cell.
    for (std::size_t gapCol = 0; gapCol < kPlayingFieldCols; ++gapCol) {
        GameContext game;
        initGarbage(game, gapAtColumn(gapCol), topRow);
        for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
            EXPECT_EQ(game.field.fieldCell(row, gapCol), kGarbageEmptyTile)
                << "gap column " << gapCol << ", row " << row;
            for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
                if (col != gapCol) {
                    EXPECT_NE(game.field.fieldCell(row, col), kGarbageEmptyTile)
                        << "gap column " << gapCol << " forced an extra gap at column " << col;
                }
            }
        }
    }

    // The same invariant over the real pick, across every row a full-height start fills.
    auto vm = makeVm();
    const auto fold = registerGarbageFold(vm);
    GameContext game;
    initGarbage(game, fold, topRow);
    for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
        int gaps = 0;
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            if (game.field.fieldCell(row, col) == kGarbageEmptyTile) {
                ++gaps;
            }
        }
        EXPECT_GT(gaps, 0) << "row " << row << " has no gap and cannot be cleared";
    }
}

// One machine, one divider. The piece randomizer and the garbage fill are separate routines but the
// original reads a single divider, and a round init draws pieces and then fills garbage in the same
// frame — so the draws advance the divider the fill goes on to read. Registering both routines on one
// VM reproduces that coupling: an extra draw before a fill changes the field the fill produces.
// Registering them on separate VMs would give each its own divider and lose it.
TEST(GarbageFill, SharesOneDividerWithThePieceRandomizer) {
    auto vm = makeVm();
    const auto fold = registerGarbageFold(vm);
    const auto draw = kirpich::vm::registerPieceRandom(vm);
    const std::size_t topRow = typeBGarbageTopRow(5);

    auto fillAfterDraws = [&](int draws) {
        vm.reset();
        kirpich::GameFlowState flow;
        for (int i = 0; i < draws; ++i) {
            (void)kirpich::vm::pickRandomPiece(draw, flow);
        }
        GameContext game;
        initGarbage(game, fold, topRow);
        return game.field.board;
    };

    // Same machine state, same number of draws -> the same field. The coupling is deterministic, not
    // noise.
    EXPECT_EQ(fillAfterDraws(3), fillAfterDraws(3));

    // Draws before the fill move the divider the fill reads, so the field differs. This is the whole
    // reason both routines belong on one machine.
    EXPECT_NE(fillAfterDraws(0), fillAfterDraws(3))
        << "piece draws did not affect the garbage fill — the routines are not sharing a divider";
}

// Cell domain and determinism over a whole fill, plus the hook's two paths.
TEST(GarbageFill, FilledCellsAreLegalAndTheHookPicksItsPath) {
    auto vm = makeVm();
    const auto fold = registerGarbageFold(vm);
    const std::size_t topRow = typeBGarbageTopRow(5);

    auto fill = [&] {
        vm.reset();
        GameContext game;
        initGarbage(game, fold, topRow);
        return game.field.board;
    };
    const auto first = fill();
    EXPECT_EQ(first, fill()) << "the same machine state produced a different field";

    for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            const std::uint8_t cell = first[row][col + kirpich::kPlayingFieldOriginCol];
            EXPECT_TRUE(cell == kGarbageEmptyTile || isBlockTile(cell))
                << "(" << row << "," << col << ") = " << int(cell);
        }
    }

    // The seam the round init calls: the demo table when the round is a demo, the procedural fill
    // otherwise, at the height it is handed.
    const auto hook = makeInitGarbageHook(fold);

    GameContext demo;
    hook(demo, 5, /*useDemoTable=*/true);
    for (std::size_t row = 0; row < kTypeBDemoGarbageRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            EXPECT_EQ(demo.field.fieldCell(kDemoGarbageTopRow + row, col),
                      kTypeBDemoGarbage[row][col]);
        }
    }

    GameContext procedural;
    hook(procedural, 1, /*useDemoTable=*/false);
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        EXPECT_NE(procedural.field.fieldCell(typeBGarbageTopRow(1), col), 0u);
        EXPECT_EQ(procedural.field.fieldCell(typeBGarbageTopRow(1) - 1, col), 0u)
            << "a height of one wrote above its rows";
    }
}
