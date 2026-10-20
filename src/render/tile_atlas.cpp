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

template <std::size_t N>
std::span<const retropp::Rgba8> colors(const std::array<retropp::Rgba8, N>& palette) noexcept {
    return std::span<const retropp::Rgba8>(palette);
}

// Upload one ramp's eight palettes and return their handles.
RampPalettes uploadRampPalettes(retropp::Renderer& renderer, const RampPaletteColors& c) {
    return RampPalettes{
        .font          = renderer.uploadPalette(colors(c.font)),
        .content       = renderer.uploadPalette(colors(c.content)),
        .fontDim       = renderer.uploadPalette(colors(c.fontDim)),
        .fontSprite    = renderer.uploadPalette(colors(c.fontSprite)),
        .sprite0       = renderer.uploadPalette(colors(c.sprite0)),
        .sprite1       = renderer.uploadPalette(colors(c.sprite1)),
        .fontSpriteDim = renderer.uploadPalette(colors(c.fontSpriteDim)),
        .spriteDim     = renderer.uploadPalette(colors(c.spriteDim)),
    };
}

}  // namespace

TileLocation locateTile(std::uint8_t index, TileSheet sheet) noexcept {
    // The launch scenes' regime is laid out unlike the other two. Its loader copies the whole sheet
    // to the base of the tile block and loads no font at all (InitRocketLaunchGraphics,
    // tetris.asm:2731-2733), so there is no font range and no carried-over block: an index IS the
    // cell, all the way down from zero. The letters those scenes print come out of this sheet too.
    //
    // The loader copies 256 tiles where the art is 207 - the source calls the length "Way too much" -
    // so the indices past the art hold whatever followed it in the cartridge. Nothing either scene
    // draws reaches them; they resolve to the empty cell rather than to art nobody authored.
    if (sheet == TileSheet::MULTIPLAYER_BURAN) {
        return index < kMultiplayerBuranTileCount
                   ? TileLocation{.source = TileSource::MULTIPLAYER_BURAN, .cell = index}
                   : kEmptyLocation;
    }

    // The font is the first 39 slots under either of the other regimes (LoadFontTiles,
    // tetris.asm:6378-6392).
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

RampPaletteColors rampPaletteColors(const ShadeRamp& shades) noexcept {
    // The object palettes are each the background counterpart with the last entry - the one the
    // decode's inversion puts the see-through colour at - made see-through.
    //
    // The plain object ramp is otherwise the background's: every colour keeps its own shade. The
    // variant differs in one place, the second entry: the colour that is ordinarily the second-darkest
    // is drawn at the lightest shade instead. That is the ramp the two dancers select, and it is why
    // they read differently from their neighbours.
    //
    // The dim pair: every shade that would have been ink becomes the light one, so the art keeps its
    // shape and loses its weight - the same relation the dim font has to the font.
    return RampPaletteColors{
        .font          = {shades.darkest, shades.lightest},
        .content       = {shades.darkest, shades.dark, shades.light, shades.lightest},
        .fontDim       = {shades.light, shades.lightest},
        .fontSprite    = {shades.darkest, kShadeTransparent},
        .sprite0       = {shades.darkest, shades.dark, shades.light, kShadeTransparent},
        .sprite1       = {shades.darkest, shades.lightest, shades.light, kShadeTransparent},
        .fontSpriteDim = {shades.light, kShadeTransparent},
        .spriteDim     = {shades.light, shades.light, shades.lightest, kShadeTransparent},
    };
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

    // One set of palettes per shade ramp, all uploaded here. A ramp only changes what the four shades
    // are, so each set is built the same way from its own four colours, and choosing a ramp later is
    // choosing between handles rather than uploading anything.
    for (std::size_t i = 0; i < kShadeRampCount; ++i) {
        atlas.palettes[i] = uploadRampPalettes(renderer, rampPaletteColors(kShadeRamps[i]));
    }

    // The custom set, arranged the same way from the palette a custom slot starts from. It is the one
    // set whose colors change after this: updateCustomPalette writes over these eight handles.
    atlas.customShades = freshCustomPalette();
    atlas.custom       = uploadRampPalettes(renderer, rampPaletteColors(atlas.customShades));

    return atlas;
}

const RampPalettes& palettesFor(const TileAtlas& atlas, std::uint8_t index) noexcept {
    return index == kCustomRamp ? atlas.custom : atlas.palettes[clampShadeRamp(index)];
}

std::array<retropp::Rgba8, 4> shadesOf(const TileAtlas& atlas, std::uint8_t index) noexcept {
    if (index == kCustomRamp) {
        const ShadeRamp& c = atlas.customShades;
        return {c.darkest, c.dark, c.light, c.lightest};
    }
    return rampColours(index);
}

void updateCustomPalette(retropp::Renderer& renderer, TileAtlas& atlas, const ShadeRamp& shades) {
    const RampPaletteColors c = rampPaletteColors(shades);
    renderer.updatePalette(atlas.custom.font, colors(c.font));
    renderer.updatePalette(atlas.custom.content, colors(c.content));
    renderer.updatePalette(atlas.custom.fontDim, colors(c.fontDim));
    renderer.updatePalette(atlas.custom.fontSprite, colors(c.fontSprite));
    renderer.updatePalette(atlas.custom.sprite0, colors(c.sprite0));
    renderer.updatePalette(atlas.custom.sprite1, colors(c.sprite1));
    renderer.updatePalette(atlas.custom.fontSpriteDim, colors(c.fontSpriteDim));
    renderer.updatePalette(atlas.custom.spriteDim, colors(c.spriteDim));
    atlas.customShades = shades;
}

ResolvedTile resolveTile(std::uint8_t index, TileSheet sheet, const TileAtlas& atlas,
                         std::uint8_t ramp) noexcept {
    const RampPalettes& palettes = palettesFor(atlas, ramp);
    const TileLocation  where    = locateTile(index, sheet);
    switch (where.source) {
        case TileSource::FONT:
            return ResolvedTile{
                .atlas = atlas.font, .cell = where.cell, .palette = palettes.font};
        case TileSource::COPYRIGHT_TITLE:
            return ResolvedTile{
                .atlas = atlas.copyrightTitle, .cell = where.cell, .palette = palettes.content};
        case TileSource::MULTIPLAYER_BURAN:
            return ResolvedTile{.atlas   = atlas.multiplayerBuran,
                                .cell    = where.cell,
                                .palette = palettes.content};
        case TileSource::GAMEPLAY:
            break;
    }
    return ResolvedTile{.atlas = atlas.gameplay, .cell = where.cell, .palette = palettes.content};
}

namespace {

// A tile drawn as an object: the sheet its art sits on, and the palette named for it. The font is the
// one source with a palette of its own, because its expansion writes only the darkest colour and the
// see-through one, so the two object variants agree on it and it needs no variant of its own.
[[nodiscard]] ResolvedTile spriteTileThrough(std::uint8_t index, TileSheet sheet,
                                             const TileAtlas& atlas, retropp::PaletteId fontPalette,
                                             retropp::PaletteId contentPalette) noexcept {
    const TileLocation where = locateTile(index, sheet);
    if (where.source == TileSource::FONT) {
        return ResolvedTile{.atlas = atlas.font, .cell = where.cell, .palette = fontPalette};
    }

    retropp::AtlasId source = atlas.gameplay;
    if (where.source == TileSource::COPYRIGHT_TITLE) {
        source = atlas.copyrightTitle;
    } else if (where.source == TileSource::MULTIPLAYER_BURAN) {
        source = atlas.multiplayerBuran;
    }
    return ResolvedTile{.atlas = source, .cell = where.cell, .palette = contentPalette};
}

}  // namespace

ResolvedTile resolveSpriteTile(std::uint8_t index, TileSheet sheet, bool palette1,
                               const TileAtlas& atlas, std::uint8_t ramp) noexcept {
    const RampPalettes& palettes = palettesFor(atlas, ramp);
    return spriteTileThrough(index, sheet, atlas, palettes.fontSprite,
                             palette1 ? palettes.sprite1 : palettes.sprite0);
}

ResolvedTile resolveDimSpriteTile(std::uint8_t index, TileSheet sheet, const TileAtlas& atlas,
                                  std::uint8_t ramp) noexcept {
    const RampPalettes& palettes = palettesFor(atlas, ramp);
    return spriteTileThrough(index, sheet, atlas, palettes.fontSpriteDim, palettes.spriteDim);
}

}  // namespace kirpich::render
