#include "render/tile_atlas.h"

#include <array>
#include <span>

#include <retropp/geometry.h>  // AssetDimensions
#include <retropp/image.h>     // ContentKind

#include <kirpich/char_tile.h>

namespace kirpich::render {

namespace {

// The empty cell every screen is built on, and what an index with no art falls back to. It is the
// last of the nine copyright-art tiles the gameplay loader carries over, so it names the same
// picture under both regimes - which is what makes it a safe fallback rather than a regime-specific
// guess.
constexpr std::uint8_t kEmptyTile = static_cast<std::uint8_t>(CharTile::SPACE);

// Where the empty cell's art lives. Both regimes reach the same sheet and the same cell.
constexpr TileLocation kEmptyLocation{
    .source = TileSource::COPYRIGHT_TITLE,
    .cell   = kEmptyTile - kContentTileBase,
};

static_assert(kEmptyLocation.cell < kCarriedCopyrightTiles,
              "the empty tile must be one of the tiles the gameplay loader carries over, or it "
              "would name different art on a gameplay screen than on a title screen");

}  // namespace

TileLocation locateTile(std::uint8_t index, TileSheet sheet) noexcept {
    // The font is the first 39 slots under either regime (LoadFontTiles, tetris.asm:6378-6392).
    if (index < kFontTileCount) {
        return TileLocation{.source = TileSource::FONT, .cell = index};
    }

    if (sheet == TileSheet::COPYRIGHT_TITLE) {
        // The copyright-and-title art runs from $27 up (:6394-6398).
        const std::uint16_t cell = static_cast<std::uint16_t>(index - kContentTileBase);
        return cell < kCopyrightTitleTileCount
                   ? TileLocation{.source = TileSource::COPYRIGHT_TITLE, .cell = cell}
                   : kEmptyLocation;
    }

    // Under the gameplay regime the block holds two arts: nine tiles carried over from the
    // copyright art at $27, then the config-and-gameplay art from $30 (:6368-6376).
    if (index < kGameplayTileBase) {
        return TileLocation{.source = TileSource::COPYRIGHT_TITLE,
                            .cell   = static_cast<std::uint16_t>(index - kContentTileBase)};
    }

    const std::uint16_t cell = static_cast<std::uint16_t>(index - kGameplayTileBase);
    return cell < kGameplayTileCount
               ? TileLocation{.source = TileSource::GAMEPLAY, .cell = cell}
               : kEmptyLocation;
}

TileAtlas uploadTileAtlas(retropp::Renderer& renderer) {
    // Each sheet is a grid of independent 8x8 tiles, sixteen to a row, in reading order - so a
    // tile's cell index is its position in the file, which is exactly what a tile index names.
    //
    // Every path below is a string literal at its call site, and must stay one: the engine's
    // build-time asset scan reads these out of the source text, so a path held in any named binding
    // is invisible to it.
    TileAtlas atlas;
    atlas.font = renderer
                     .loadAtlas("assets/gfx/default/font.png",
                                retropp::AssetDimensions::GameBoy8x8, retropp::ContentKind::Tileset)
                     .atlasId;
    atlas.copyrightTitle = renderer
                               .loadAtlas("assets/gfx/default/copyrightandtitlescreen.png",
                                          retropp::AssetDimensions::GameBoy8x8,
                                          retropp::ContentKind::Tileset)
                               .atlasId;
    atlas.gameplay = renderer
                         .loadAtlas("assets/gfx/default/configandgameplay.png",
                                    retropp::AssetDimensions::GameBoy8x8,
                                    retropp::ContentKind::Tileset)
                         .atlasId;
    atlas.multiplayerBuran = renderer
                                 .loadAtlas("assets/gfx/default/multiplayerandburan.png",
                                            retropp::AssetDimensions::GameBoy8x8,
                                            retropp::ContentKind::Tileset)
                                 .atlasId;

    constexpr std::array<retropp::Rgba8, 2> fontShades{kShadeDarkest, kShadeLightest};
    constexpr std::array<retropp::Rgba8, 4> contentShades{kShadeDarkest, kShadeDark, kShadeLight,
                                                          kShadeLightest};
    atlas.fontPalette    = renderer.uploadPalette(std::span<const retropp::Rgba8>(fontShades));
    atlas.contentPalette = renderer.uploadPalette(std::span<const retropp::Rgba8>(contentShades));

    return atlas;
}

ResolvedTile resolveTile(std::uint8_t index, TileSheet sheet, const TileAtlas& atlas) noexcept {
    const TileLocation where = locateTile(index, sheet);
    switch (where.source) {
        case TileSource::FONT:
            return ResolvedTile{
                .atlas = atlas.font, .cell = where.cell, .palette = atlas.fontPalette};
        case TileSource::COPYRIGHT_TITLE:
            return ResolvedTile{
                .atlas = atlas.copyrightTitle, .cell = where.cell, .palette = atlas.contentPalette};
        case TileSource::GAMEPLAY:
            break;
    }
    return ResolvedTile{
        .atlas = atlas.gameplay, .cell = where.cell, .palette = atlas.contentPalette};
}

}  // namespace kirpich::render
