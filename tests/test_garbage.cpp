// Garbage-fill data: the fixed Type B demo garbage table and the constants the fill consumes.
//
// The composed 4 x 10 table (src/data/garbage.h) is swept in full against the flat byte array in
// tests/fixtures/garbage_expected.h, which holds the table's serialization exactly as the original
// stores it, independent of the composed grid so a defect in the header cannot mask the sweep. The
// six constants are pinned against docs/contracts/garbage-init.md, the cell domain and the
// one-hole-per-row invariant are checked across the whole table, and the empty tile is tied to the
// character map's space glyph.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/char_tile.h>

#include "data/garbage.h"
#include "data/playing_field.h"
#include "fixtures/garbage_expected.h"

namespace {

using kirpich::kGarbageBlockTileBase;
using kirpich::kGarbageBlockTileCount;
using kirpich::kGarbageEmptyTile;
using kirpich::kMultiplayerRoundStartGarbageRows;
using kirpich::kPlayingFieldCols;
using kirpich::kTypeBDemoGarbage;
using kirpich::kTypeBDemoGarbageRows;
using kirpich::kTypeBGarbageRowsPerHeight;
using kirpich::fixtures::kExpectedTypeBDemoGarbageBytes;

// 1. The six constants match the contract; the grid's declared dimensions are rows x cols, and the
//    flat fixture spans exactly that many cells.
TEST(Garbage, ConstantsMatchContract) {
    EXPECT_EQ(kTypeBDemoGarbageRows, 4);
    EXPECT_EQ(kTypeBGarbageRowsPerHeight, 2);
    EXPECT_EQ(kMultiplayerRoundStartGarbageRows, 6);
    EXPECT_EQ(kGarbageBlockTileBase, 0x80);
    EXPECT_EQ(kGarbageBlockTileCount, 8);
    EXPECT_EQ(kGarbageEmptyTile, 0x2F);

    EXPECT_EQ(kTypeBDemoGarbage.size(), std::size_t{kTypeBDemoGarbageRows});
    EXPECT_EQ(kTypeBDemoGarbage[0].size(), std::size_t{kPlayingFieldCols});
    EXPECT_EQ(kExpectedTypeBDemoGarbageBytes.size(),
              std::size_t{kTypeBDemoGarbageRows} * std::size_t{kPlayingFieldCols});
}

// 2. Full-corpus sweep: every one of the 40 cells equals the flat fixture at its row-major offset.
TEST(Garbage, TypeBDemoGarbageFullCorpusSweep) {
    const std::size_t cols = kTypeBDemoGarbage[0].size();
    for (std::size_t r = 0; r < kTypeBDemoGarbage.size(); ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            EXPECT_EQ(kTypeBDemoGarbage[r][c], kExpectedTypeBDemoGarbageBytes[r * cols + c])
                << "cell [" << r << "][" << c << "]";
        }
    }
}

// 3. Every cell is the empty tile or a block tile, and every row leaves at least one gap (the
//    data-side mirror of the fill's ensure-one-hole rule).
TEST(Garbage, CellDomainAndHoleInvariants) {
    for (std::size_t r = 0; r < kTypeBDemoGarbage.size(); ++r) {
        bool has_hole = false;
        for (const std::uint8_t cell : kTypeBDemoGarbage[r]) {
            const bool is_empty = cell == kGarbageEmptyTile;
            const bool is_block = cell >= kGarbageBlockTileBase &&
                                  cell < kGarbageBlockTileBase + kGarbageBlockTileCount;
            EXPECT_TRUE(is_empty || is_block)
                << "cell [" << r << "] = 0x" << std::hex << static_cast<int>(cell)
                << " is neither empty nor a block tile";
            has_hole = has_hole || is_empty;
        }
        EXPECT_TRUE(has_hole) << "row " << r << " has no empty cell";
    }
}

// 4. The empty tile IS the character map's space glyph - the fill's `ld a, " "` resolves here.
TEST(Garbage, EmptyTileIsCharmapSpace) {
    EXPECT_EQ(kGarbageEmptyTile, static_cast<std::uint8_t>(kirpich::CharTile::SPACE));
}

// 5. Hand-pinned corners, traced to tetris.asm:4317-4320.
TEST(Garbage, BoundaryPins) {
    EXPECT_EQ(kTypeBDemoGarbage[0][0], 0x85);
    EXPECT_EQ(kTypeBDemoGarbage[0][9], 0x85);
    EXPECT_EQ(kTypeBDemoGarbage[3][0], 0x83);
    EXPECT_EQ(kTypeBDemoGarbage[3][9], 0x2F);
}

}  // namespace

