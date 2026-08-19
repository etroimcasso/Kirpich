#pragma once

// The tile art: uploading the extracted sheets, and turning a board cell's tile index into the
// picture it names.
//
// A tile index is not a picture on its own. The original keeps one tile block and rewrites it when
// it changes screens, so index $30 is one glyph while the copyright art is loaded and a different
// one while the gameplay art is. The port keeps every sheet uploaded at once and resolves the index
// against the regime the simulation is in (kirpich::TileSheet), which is the same answer arrived at
// from the other side.
//
// The relation itself is read out of the two loaders. Both begin with LoadFontTiles
// (tetris.asm:6378-6392), which expands the 39-tile 1bpp font into the first 39 slots of the block,
// so indices $00-$26 mean the same glyph under either regime. They differ after that:
//
//   LoadCopyrightAndTitleScreenTiles (:6394-6398) copies straight on from where the font ended, so
//   the copyright-and-title art occupies $27 upward - its slot i at index $27 + i.
//
//   LoadGameplayTiles (:6368-6376) copies ten tiles from that same art to the same place, then the
//   config-and-gameplay art to index $30. The second copy overlaps the first by one tile and wins,
//   so nine tiles of the copyright art survive at $27-$2F and the gameplay art runs from $30 up.
//
// Those nine surviving tiles are not an accident worth routing around: index $2F is the board's
// empty cell (CharTile::SPACE), and it is the last of them. Carrying them is what lets one blank
// tile mean blank on every screen.
//
// Both loaders copy more than their art holds - the disassembly's own comments call the surplus
// garbage - so an index past the real tiles named a picture nobody authored. Those indices resolve
// to the empty cell here rather than throwing: the original draws whatever the block happens to
// contain, and a screen the port has not finished should look wrong, not crash.

#include <cstdint>

#include <retropp/image.h>     // AtlasId
#include <retropp/palette.h>   // PaletteId, Rgba8
#include <retropp/renderer.h>  // Renderer

#include "state/display_state.h"  // TileSheet

namespace kirpich::render {

// Which sheet a tile index draws from. Distinct from kirpich::TileSheet, which names a regime: a
// regime chooses between these, and the font belongs to both.
enum class TileSource : std::uint8_t {
    FONT,             // font.png - indices $00-$26 under either regime
    COPYRIGHT_TITLE,  // copyrightandtitlescreen.png
    GAMEPLAY,         // configandgameplay.png
};

// Where a tile index's art sits: which sheet, and which cell of it. The cell index is the tile's
// position in the sheet's own reading order, which is what a TileCell's `tile` field means.
struct TileLocation {
    TileSource    source = TileSource::FONT;
    std::uint16_t cell   = 0;

    friend constexpr bool operator==(const TileLocation&, const TileLocation&) = default;
};

// How many real tiles each sheet holds. The sheets are padded out to a whole row of 16, so the
// files are larger than these; a cell past the real count is art nobody drew.
inline constexpr std::uint16_t kFontTileCount           = 39;   // $00-$26
inline constexpr std::uint16_t kCopyrightTitleTileCount = 119;
inline constexpr std::uint16_t kGameplayTileCount       = 197;

// The first index past the font, where each regime's own art begins.
inline constexpr std::uint8_t kContentTileBase = 0x27;

// The index the config-and-gameplay art starts at under the gameplay regime ($8300 / $10).
inline constexpr std::uint8_t kGameplayTileBase = 0x30;

// Tiles of the copyright-and-title art that survive under the gameplay regime, at kContentTileBase.
inline constexpr std::uint16_t kCarriedCopyrightTiles = kGameplayTileBase - kContentTileBase;  // 9

// Resolve a tile index to its art under a regime. Pure - no renderer, no upload, no device.
[[nodiscard]] TileLocation locateTile(std::uint8_t index, TileSheet sheet) noexcept;

// The uploaded sheets and the palettes their samples index through.
//
// Two palettes, not one, because the two bit depths do not share an index space. The extractor
// writes each sample as its own palette index (docs/contracts/tile-graphics.md): a 2bpp tile yields
// 0-3 and the 1bpp font yields 0-1, and in both cases the decode inverts, so 0 is the darkest shade
// and the top value is white. A four-entry ramp is therefore right for the content sheets and a
// two-entry one for the font; each is the identity for its own sheet.
// Objects need their own palettes, for two reasons that compound.
//
// The first is transparency. An object's lowest colour is not a shade at all - it is see-through,
// which is what lets a sprite be a shape rather than a rectangle. The background has no such colour;
// its lowest value is a real shade. So a sprite drawn through a background palette would come out on
// a solid card, and the palettes cannot be shared.
//
// The second is which entry that is. The decode inverts (docs/contracts/tile-graphics.md), so a
// sample's palette index counts down from the darkest shade while the hardware colour it came from
// counts up - and the see-through colour, being the hardware's lowest, is the port's HIGHEST index.
// It is the last entry of each ramp, never the first.
//
// The game keeps two object palettes and uses both (tetris.asm:296-300): the first is the plain
// ramp, the second re-maps the two middle colours, and the ending's dancers select it for two of
// their performers. The font needs neither variant - its expansion writes only the darkest colour
// and the see-through one (LoadFontTiles, :6383-6387), and both object palettes agree on those two,
// so one font palette serves whichever is selected.
inline constexpr retropp::Rgba8 kShadeTransparent{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0x00};

struct TileAtlas {
    retropp::AtlasId font{};
    retropp::AtlasId copyrightTitle{};
    retropp::AtlasId gameplay{};
    // Uploaded because the extractor writes it and the presence check requires it. No screen the
    // port draws selects it - the link-cable and launch scenes that do are not ported.
    retropp::AtlasId multiplayerBuran{};

    retropp::PaletteId fontPalette{};     // two entries: black, white
    retropp::PaletteId contentPalette{};  // four entries: the DMG shade ramp, darkest first

    // The object palettes. Same ramps, last entry see-through.
    retropp::PaletteId fontSpritePalette{};  // font art drawn as an object; serves both object palettes
    retropp::PaletteId spritePalette0{};     // the plain ramp
    retropp::PaletteId spritePalette1{};     // the variant the dancers select
};

// The four DMG shades, darkest first - the order the decode's inversion produces.
inline constexpr retropp::Rgba8 kShadeDarkest{.r = 0x00, .g = 0x00, .b = 0x00};
inline constexpr retropp::Rgba8 kShadeDark{.r = 0x55, .g = 0x55, .b = 0x55};
inline constexpr retropp::Rgba8 kShadeLight{.r = 0xAA, .g = 0xAA, .b = 0xAA};
inline constexpr retropp::Rgba8 kShadeLightest{.r = 0xFF, .g = 0xFF, .b = 0xFF};

// Upload every sheet and both palettes. Call once, after the assets are present. Throws whatever
// the engine's loaders throw when a file is missing or will not decode.
[[nodiscard]] TileAtlas uploadTileAtlas(retropp::Renderer& renderer);

// A resolved cell: everything a TileCell needs to name a picture.
struct ResolvedTile {
    retropp::AtlasId   atlas{};
    std::uint16_t      cell = 0;
    retropp::PaletteId palette{};

    friend constexpr bool operator==(const ResolvedTile&, const ResolvedTile&) = default;
};

// locateTile, carried through to the uploaded handles.
[[nodiscard]] ResolvedTile resolveTile(std::uint8_t index, TileSheet sheet,
                                       const TileAtlas& atlas) noexcept;

// The same, for a tile drawn as an object: same sheet and cell, an object palette instead of a
// background one. `palette1` selects the variant ramp, as the object's own attribute does.
[[nodiscard]] ResolvedTile resolveSpriteTile(std::uint8_t index, TileSheet sheet, bool palette1,
                                             const TileAtlas& atlas) noexcept;

}  // namespace kirpich::render
