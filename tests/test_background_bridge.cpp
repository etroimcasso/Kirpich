// The background bridge — behavioral tests against docs/contracts/screen.md.
//
// Device-free. Uploading art needs a renderer, but nothing the bridge DECIDES does: a TileAtlas is a
// bundle of handles, so a test can build one with distinct values and assert that each index is
// routed to the right sheet, the right cell and the right palette. What is not covered here is the
// upload itself and the picture on the glass; those are verified by hand on a development machine,
// because a runner has neither a display nor the extracted art (the assets are never placed on one).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/tilemaps.h"
#include "render/background.h"
#include "render/tile_atlas.h"
#include "state/display_state.h"
#include "state/playing_field_state.h"
#include "systems/game_context.h"
#include "systems/title_screens.h"

namespace {

using kirpich::PlayingFieldState;
using kirpich::TileSheet;
using kirpich::render::ResolvedTile;
using kirpich::render::TileAtlas;
using kirpich::render::TileLocation;
using kirpich::render::TileSource;
using kirpich::systems::GameContext;

// Handles chosen to be distinct and recognisable, so a mis-routed cell names the wrong one loudly.
constexpr TileAtlas kAtlas{
    .font             = static_cast<retropp::AtlasId>(11),
    .copyrightTitle   = static_cast<retropp::AtlasId>(22),
    .gameplay         = static_cast<retropp::AtlasId>(33),
    .multiplayerBuran = static_cast<retropp::AtlasId>(44),
    .fontPalette      = static_cast<retropp::PaletteId>(55),
    .contentPalette   = static_cast<retropp::PaletteId>(66),
};

// Where the empty cell resolves. Both regimes agree, which is the property test 2 of test_screen.cpp
// pins from the data side.
constexpr std::uint8_t kEmpty = static_cast<std::uint8_t>(kirpich::CharTile::SPACE);

// retropp::TileCell carries no equality operator, so compare it by its fields. Every one of them is
// named here on purpose: a field added to the engine's cell should make this stop compiling rather
// than silently stop being checked.
bool sameCell(const retropp::TileCell& a, const retropp::TileCell& b) {
    return a.atlas == b.atlas && a.tile == b.tile && a.palette == b.palette && a.flipX == b.flipX &&
           a.flipY == b.flipY && a.rotation == b.rotation;
}

bool sameCells(const std::vector<retropp::TileCell>& a, const std::vector<retropp::TileCell>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameCell);
}

}  // namespace

// ── Test 1: TileIndexResolvesOverTheWholeDomain ─────────────────────────────────────────────────────
// locateTile over all 256 indices in both regimes, against the relation read out of the two loaders
// (tetris.asm:6368-6398). Full domain, not a sample.
TEST(BackgroundBridge, TileIndexResolvesOverTheWholeDomain) {
    using kirpich::render::kCarriedCopyrightTiles;
    using kirpich::render::kContentTileBase;
    using kirpich::render::kCopyrightTitleTileCount;
    using kirpich::render::kFontTileCount;
    using kirpich::render::kGameplayTileBase;
    using kirpich::render::kGameplayTileCount;

    // The fallback both regimes use for an index past the art that was actually loaded.
    const TileLocation fallback{.source = TileSource::COPYRIGHT_TITLE,
                                .cell   = kEmpty - kContentTileBase};

    for (int i = 0; i <= 0xFF; ++i) {
        const auto index = static_cast<std::uint8_t>(i);

        // The font is the first 39 slots under either regime (LoadFontTiles, :6378-6392).
        if (index < kFontTileCount) {
            const TileLocation want{.source = TileSource::FONT, .cell = index};
            ASSERT_EQ(kirpich::render::locateTile(index, TileSheet::COPYRIGHT_TITLE), want)
                << "index " << i;
            ASSERT_EQ(kirpich::render::locateTile(index, TileSheet::GAMEPLAY), want) << "index " << i;
            continue;
        }

        // Copyright-and-title regime: the art runs from $27 up (:6394-6398), then falls back.
        {
            const std::uint16_t cell = static_cast<std::uint16_t>(index - kContentTileBase);
            const TileLocation want =
                cell < kCopyrightTitleTileCount
                    ? TileLocation{.source = TileSource::COPYRIGHT_TITLE, .cell = cell}
                    : fallback;
            ASSERT_EQ(kirpich::render::locateTile(index, TileSheet::COPYRIGHT_TITLE), want)
                << "index " << i;
        }

        // Gameplay regime: nine carried-over tiles, then the gameplay art from $30 (:6368-6376).
        {
            TileLocation want;
            if (index < kGameplayTileBase) {
                want = TileLocation{.source = TileSource::COPYRIGHT_TITLE,
                                    .cell = static_cast<std::uint16_t>(index - kContentTileBase)};
            } else {
                const std::uint16_t cell = static_cast<std::uint16_t>(index - kGameplayTileBase);
                want = cell < kGameplayTileCount
                           ? TileLocation{.source = TileSource::GAMEPLAY, .cell = cell}
                           : fallback;
            }
            ASSERT_EQ(kirpich::render::locateTile(index, TileSheet::GAMEPLAY), want)
                << "index " << i;
        }
    }

    // The boundaries the loaders set, stated on their own so a shifted base is unmistakable.
    EXPECT_EQ(kContentTileBase, 0x27);
    EXPECT_EQ(kGameplayTileBase, 0x30);
    EXPECT_EQ(kCarriedCopyrightTiles, 9);

    // The empty cell is one of the carried-over nine, so it names one picture on every screen.
    EXPECT_EQ(kirpich::render::locateTile(kEmpty, TileSheet::COPYRIGHT_TITLE),
              kirpich::render::locateTile(kEmpty, TileSheet::GAMEPLAY));

    // Each source carries its own sheet handle, and the two bit depths their own palettes.
    EXPECT_EQ(kirpich::render::resolveTile(0, TileSheet::GAMEPLAY, kAtlas),
              (ResolvedTile{.atlas = kAtlas.font, .cell = 0, .palette = kAtlas.fontPalette}));
    EXPECT_EQ(kirpich::render::resolveTile(kContentTileBase, TileSheet::COPYRIGHT_TITLE, kAtlas),
              (ResolvedTile{
                  .atlas = kAtlas.copyrightTitle, .cell = 0, .palette = kAtlas.contentPalette}));
    EXPECT_EQ(kirpich::render::resolveTile(kGameplayTileBase, TileSheet::GAMEPLAY, kAtlas),
              (ResolvedTile{
                  .atlas = kAtlas.gameplay, .cell = 0, .palette = kAtlas.contentPalette}));
}

// ── Test 2: ComposeReadsTheVisibleWindowAndOnlyThat ─────────────────────────────────────────────────
// The grid is the board's top-left 20x18 — the Game Boy's screen — and nothing else on the 32x32
// board reaches it.
TEST(BackgroundBridge, ComposeReadsTheVisibleWindowAndOnlyThat) {
    using kirpich::render::kVisibleCells;
    using kirpich::render::kVisibleCols;
    using kirpich::render::kVisibleRows;

    PlayingFieldState field;
    // A value per cell that encodes its own position, so a transposed or mis-strided read is visible.
    for (std::size_t row = 0; row < kirpich::kBoardRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kBoardCols; ++col) {
            field.board[row][col] = static_cast<std::uint8_t>(row * 7 + col);
        }
    }

    std::vector<retropp::TileCell> cells;
    kirpich::render::composeBackground(field, TileSheet::GAMEPLAY, kAtlas, cells);

    ASSERT_EQ(cells.size(), kVisibleCells);
    EXPECT_EQ(kVisibleCols, 20u);
    EXPECT_EQ(kVisibleRows, 18u);

    for (std::size_t row = 0; row < kVisibleRows; ++row) {
        for (std::size_t col = 0; col < kVisibleCols; ++col) {
            const ResolvedTile want =
                kirpich::render::resolveTile(field.board[row][col], TileSheet::GAMEPLAY, kAtlas);
            const retropp::TileCell& got = cells[row * kVisibleCols + col];
            ASSERT_EQ(got.atlas, want.atlas) << "cell " << row << "," << col;
            ASSERT_EQ(got.tile, want.cell) << "cell " << row << "," << col;
            ASSERT_EQ(got.palette, want.palette) << "cell " << row << "," << col;
            // Nothing flips or rotates: the original's background has no such attribute.
            ASSERT_FALSE(got.flipX);
            ASSERT_FALSE(got.flipY);
            ASSERT_EQ(got.rotation, retropp::Rotation::None);
        }
    }

    // The regime is a live input, not a constant folded in at compose time.
    std::vector<retropp::TileCell> other;
    kirpich::render::composeBackground(field, TileSheet::COPYRIGHT_TITLE, kAtlas, other);
    EXPECT_FALSE(sameCells(cells, other));

    // Re-composing into the same buffer replaces it rather than growing it.
    kirpich::render::composeBackground(field, TileSheet::GAMEPLAY, kAtlas, other);
    EXPECT_EQ(other.size(), kVisibleCells);
    EXPECT_TRUE(sameCells(other, cells));
}

// ── Test 3: LayerShape ──────────────────────────────────────────────────────────────────────────────
// One layer, at the origin, the size of the screen, borrowing the composed cells.
TEST(BackgroundBridge, LayerShape) {
    std::vector<retropp::TileCell> cells;
    kirpich::render::composeBackground(PlayingFieldState{}, TileSheet::GAMEPLAY, kAtlas, cells);

    const retropp::DrawLayer layer = kirpich::render::backgroundLayer(cells);

    EXPECT_EQ(layer.key.value, kirpich::render::kBackgroundLayerKey);
    EXPECT_EQ(layer.z, kirpich::render::kBackgroundLayerZ);
    EXPECT_EQ(layer.size, retropp::ViewportResolution::GameBoy.size());
    EXPECT_EQ(layer.scroll, retropp::LayerScroll{});

    const auto* content = std::get_if<retropp::TileContent>(&layer.content);
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->widthInTiles, static_cast<int>(kirpich::render::kVisibleCols));
    EXPECT_EQ(content->heightInTiles, static_cast<int>(kirpich::render::kVisibleRows));
    EXPECT_EQ(content->wrap, retropp::TileWrap::Blank);

    // The content BORROWS: the span is the caller's vector, not a copy of it.
    ASSERT_EQ(content->cells.size(), cells.size());
    EXPECT_EQ(content->cells.data(), cells.data());
}

// ── Test 4: TheBackgroundIsAFunctionOfSimulationState ───────────────────────────────────────────────
// The whole point of the bridge: stamp a backdrop, let a shipped handler write the board, and the
// picture reflects both — with no render-side bookkeeping in between.
TEST(BackgroundBridge, TheBackgroundIsAFunctionOfSimulationState) {
    using kirpich::render::kVisibleCols;

    GameContext game;
    kirpich::systems::initTitleScreen(game);  // a shipped handler that paints and stamps

    std::vector<retropp::TileCell> before;
    kirpich::render::composeBackground(game.field, game.display.sheet, kAtlas, before);

    // The stamped screen is what the bridge sees.
    EXPECT_EQ(before[0].tile,
              kirpich::render::resolveTile(kirpich::kTitleScreenTilemap[0][0], game.display.sheet,
                                           kAtlas)
                  .cell);

    // Now write one board cell the way piece locking does, and only that cell moves.
    constexpr std::size_t kRow = 5;
    constexpr std::size_t kCol = 4;
    game.field.fieldCell(kRow, kCol) = 0x88;

    std::vector<retropp::TileCell> after;
    kirpich::render::composeBackground(game.field, game.display.sheet, kAtlas, after);

    const std::size_t moved = kRow * kVisibleCols + (kCol + kirpich::kPlayingFieldOriginCol);
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (i == moved) {
            ASSERT_FALSE(sameCell(after[i], before[i])) << "the written cell must change";
        } else {
            ASSERT_TRUE(sameCell(after[i], before[i])) << "cell " << i << " must not";
        }
    }
    EXPECT_EQ(after[moved].tile,
              kirpich::render::resolveTile(0x88, game.display.sheet, kAtlas).cell);
}
