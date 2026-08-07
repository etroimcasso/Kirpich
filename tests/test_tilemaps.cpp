// Background tilemaps: the 22 static-screen grids, swept in full against the parser-emitted fixture.
//
// The composed grids (src/data/tilemaps.h) are checked cell for cell against the flat byte arrays in
// tests/fixtures/tilemaps_expected.h, which hold each screen's serialization exactly as the original
// stores it ($FF field-overlay sentinels kept), independent of the composed surface so a defect in
// the header cannot mask the sweep. The text rows are additionally re-encoded through the charmap
// (src/data/charmap.h) to tie this unit to the character map it was decoded with. Non-ASCII glyphs
// are written as \xHH byte escapes so this file is pure ASCII. Expectations: docs/contracts/tilemaps.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "data/charmap.h"
#include "data/playing_field.h"
#include "data/tilemaps.h"
#include "fixtures/tilemaps_expected.h"

namespace {

using kirpich::CharTile;
using kirpich::encodeCharmapText;

// Every cell of a 2-D grid equals the flat fixture at row-major offset. ASSERT_LE (not EQ) on the
// span so a C3 overlay's trailing sentinel byte in the fixture is tolerated here and pinned separately.
template <typename Grid, typename Flat>
void expectGridMatchesFlat(const Grid& grid, const Flat& flat, const char* name) {
    const std::size_t rows = grid.size();
    const std::size_t cols = grid[0].size();
    ASSERT_LE(rows * cols, flat.size()) << name;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            EXPECT_EQ(grid[r][c], flat[r * cols + c]) << name << " cell [" << r << "][" << c << "]";
        }
    }
}

// Every element of a 1-D array equals the flat fixture (exact length).
template <typename Arr, typename Flat>
void expect1dMatchesFlat(const Arr& arr, const Flat& flat, const char* name) {
    ASSERT_EQ(arr.size(), flat.size()) << name;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        EXPECT_EQ(arr[i], flat[i]) << name << " [" << i << "]";
    }
}

// 1. The four constants match the contract; the field overlays share the playing-field extents;
//    every array's declared dimensions match the contract's dimensions table.
TEST(Tilemaps, ConstantsMatchContract) {
    using namespace kirpich;

    EXPECT_EQ(kTilemapScreenCols, 20);
    EXPECT_EQ(kTilemapScreenRows, 18);
    EXPECT_EQ(kTilemapWindowCols, 8);
    EXPECT_EQ(kTowerTilemapRows, 7);

    // Field overlays are field-shaped: their extents ARE the playing-field constants.
    EXPECT_EQ(kScoreboardTilemap.size(), std::size_t{kPlayingFieldRows});
    EXPECT_EQ(kScoreboardTilemap[0].size(), std::size_t{kPlayingFieldCols});
    EXPECT_EQ(kDancersTilemap.size(), std::size_t{kPlayingFieldRows});
    EXPECT_EQ(kDancersTilemap[0].size(), std::size_t{kPlayingFieldCols});

    // Full screens.
    EXPECT_EQ(kTypeAGameplayTilemap.size(), std::size_t{kTilemapScreenRows});
    EXPECT_EQ(kTypeAGameplayTilemap[0].size(), std::size_t{kTilemapScreenCols});
    EXPECT_EQ(kMultiplayerGameplayTilemap.size(), std::size_t{kTilemapScreenRows});

    // Banners.
    EXPECT_EQ(kMultiplayerVictoryTopTilemap.size(), std::size_t{4});
    EXPECT_EQ(kMultiplayerVictoryBottomTilemap.size(), std::size_t{6});
    EXPECT_EQ(kBuranBackdropTilemap.size(), std::size_t{4});
    EXPECT_EQ(kMultiplayerVictoryTopTilemap[0].size(), std::size_t{kTilemapScreenCols});

    // Window blocks.
    EXPECT_EQ(kPauseMessageTilemap.size(), std::size_t{10});
    EXPECT_EQ(kPauseMessageTilemap[0].size(), std::size_t{kTilemapWindowCols});
    EXPECT_EQ(kGameOverTilemap.size(), std::size_t{7});
    EXPECT_EQ(kTryAgainTilemap.size(), std::size_t{6});
    EXPECT_EQ(kTryAgainTilemap[0].size(), std::size_t{kTilemapWindowCols});

    // Tower columns and the congratulations strip.
    EXPECT_EQ(kLeftTowerLeftSideTilemap.size(), std::size_t{kTowerTilemapRows});
    EXPECT_EQ(kCongratulationsTilemap.size(), std::size_t{16});
}

// 2. Full-corpus sweep of all nine 20x18 screens (3240 cells).
TEST(Tilemaps, FullScreenTilemapSweep) {
    using namespace kirpich;
    using namespace kirpich::fixtures;
    expectGridMatchesFlat(kTypeAGameplayTilemap, kExpectedTypeAGameplayTilemapBytes, "TypeAGameplay");
    expectGridMatchesFlat(kTypeBGameplayTilemap, kExpectedTypeBGameplayTilemapBytes, "TypeBGameplay");
    expectGridMatchesFlat(kCopyrightScreenTilemap, kExpectedCopyrightScreenTilemapBytes, "Copyright");
    expectGridMatchesFlat(kTitleScreenTilemap, kExpectedTitleScreenTilemapBytes, "Title");
    expectGridMatchesFlat(kConfigScreenTilemap, kExpectedConfigScreenTilemapBytes, "Config");
    expectGridMatchesFlat(kTypeADifficultyTilemap, kExpectedTypeADifficultyTilemapBytes, "TypeADiff");
    expectGridMatchesFlat(kTypeBDifficultyTilemap, kExpectedTypeBDifficultyTilemapBytes, "TypeBDiff");
    expectGridMatchesFlat(kMultiplayerDifficultyTilemap, kExpectedMultiplayerDifficultyTilemapBytes,
                          "MultiDiff");
    expectGridMatchesFlat(kMultiplayerGameplayTilemap, kExpectedMultiplayerGameplayTilemapBytes,
                          "MultiGameplay");
}

// 3. Banners (C2) and window blocks (C4), same composed-vs-flat rule.
TEST(Tilemaps, BannerAndWindowTilemapSweep) {
    using namespace kirpich;
    using namespace kirpich::fixtures;
    expectGridMatchesFlat(kMultiplayerVictoryTopTilemap, kExpectedMultiplayerVictoryTopTilemapBytes,
                          "VictoryTop");
    expectGridMatchesFlat(kMultiplayerVictoryBottomTilemap,
                          kExpectedMultiplayerVictoryBottomTilemapBytes, "VictoryBottom");
    expectGridMatchesFlat(kBuranBackdropTilemap, kExpectedBuranBackdropTilemapBytes, "Buran");
    expectGridMatchesFlat(kPauseMessageTilemap, kExpectedPauseMessageTilemapBytes, "Pause");
    expectGridMatchesFlat(kGameOverTilemap, kExpectedGameOverTilemapBytes, "GameOver");
    expectGridMatchesFlat(kTryAgainTilemap, kExpectedTryAgainTilemapBytes, "TryAgain");
}

// 4. Field overlays (C3): 180 cells each match the fixture, and the $FF terminator lives in the
//    serialization (fixture[180]) but not in the composed grid.
TEST(Tilemaps, FieldOverlayTilemapSweep) {
    using namespace kirpich;
    using namespace kirpich::fixtures;

    expectGridMatchesFlat(kScoreboardTilemap, kExpectedScoreboardTilemapBytes, "Scoreboard");
    expectGridMatchesFlat(kDancersTilemap, kExpectedDancersTilemapBytes, "Dancers");

    ASSERT_EQ(kExpectedScoreboardTilemapBytes.size(), std::size_t{181});
    EXPECT_EQ(kExpectedScoreboardTilemapBytes[180], 0xFF);
    ASSERT_EQ(kExpectedDancersTilemapBytes.size(), std::size_t{181});
    EXPECT_EQ(kExpectedDancersTilemapBytes[180], 0xFF);

    for (const auto& row : kScoreboardTilemap) {
        for (const auto cell : row) {
            EXPECT_NE(cell, 0xFF) << "sentinel leaked into the composed scoreboard grid";
        }
    }
}

// 5. Tower columns (C5) and the congratulations strip (C6), one-dimensional.
TEST(Tilemaps, TowerAndCongratulationsSweep) {
    using namespace kirpich;
    using namespace kirpich::fixtures;
    expect1dMatchesFlat(kLeftTowerLeftSideTilemap, kExpectedLeftTowerLeftSideTilemapBytes, "LTowerL");
    expect1dMatchesFlat(kLeftTowerRightSideTilemap, kExpectedLeftTowerRightSideTilemapBytes, "LTowerR");
    expect1dMatchesFlat(kRightTowerLeftSideTilemap, kExpectedRightTowerLeftSideTilemapBytes, "RTowerL");
    expect1dMatchesFlat(kRightTowerRightSideTilemap, kExpectedRightTowerRightSideTilemapBytes, "RTowerR");
    expect1dMatchesFlat(kCongratulationsTilemap, kExpectedCongratulationsTilemapBytes, "Congrats");
}

// 6. Cross-unit tie to the character map: selected text rows re-encode through encodeCharmapText to
//    exactly the composed grid row bytes - including the ".<U+201D>" ligature as a single tile.
TEST(Tilemaps, TextRowsRoundTripThroughCharmap) {
    using namespace kirpich;

    const auto expectRow = [](std::string_view text, const auto& grid_row, std::size_t start_col,
                              const char* name) {
        const std::optional<std::vector<CharTile>> encoded = encodeCharmapText(text);
        ASSERT_TRUE(encoded.has_value()) << name << ": text did not encode";
        for (std::size_t i = 0; i < encoded->size(); ++i) {
            EXPECT_EQ(static_cast<std::uint8_t>((*encoded)[i]), grid_row[start_col + i])
                << name << " glyph " << i;
        }
    };

    expectRow("  hit   ", kPauseMessageTilemap[0], 0, "pause");
    expectRow("single    ", kScoreboardTilemap[0], 0, "scoreboard");
    expectRow("  again\xE2\x99\xA5", kTryAgainTilemap[4], 0, "tryagain-heart");
    expectRow("by alexey pazhitnov.\xE2\x80\x9D", kCopyrightScreenTilemap[16], 0, "copyright-ligature");
}

// 7. Hand-pinned boundary cells, traced to tetris.asm.
TEST(Tilemaps, TilemapBoundaryPins) {
    using namespace kirpich;

    EXPECT_EQ(kTypeAGameplayTilemap[0][0], 0x2A);   // tetris.asm:6939
    EXPECT_EQ(kTitleScreenTilemap[0][0], 0x8E);     // tetris.asm:7006
    EXPECT_EQ(kCongratulationsTilemap[0], 0xB3);    // tetris.asm:2916
    EXPECT_EQ(kGameOverTilemap[0][0], 0x61);        // tetris.asm:6511
    EXPECT_EQ(kDancersTilemap[17][9], 0x7D);        // tetris.asm:7103

    const std::array<std::uint8_t, kTowerTilemapRows> expected_left_tower{
        {0xC2, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA}};  // tetris.asm:3089
    EXPECT_EQ(kLeftTowerLeftSideTilemap, expected_left_tower);
}

}  // namespace
