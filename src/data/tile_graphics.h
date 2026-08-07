#pragma once

// Tile graphics: the four ROM blocks Kirpich turns into greyscale PNGs at first start.
//
// The port ships no art. Every graphic is derived from the Game Boy Tetris ROM, where the tiles sit
// in four contiguous blocks - the font (1bpp) and three 2bpp screens. kTileGraphics is the table
// the extractor walks: for each block, where it begins in the ROM, how many 8x8 tiles it holds, and
// its bit format. The extractor (src/assets/extract.h) decodes each block to a 16-tiles-wide
// greyscale PNG and writes it under assets/gfx/default/ using the row's fileName.
//
// The table drives EXTRACTION only — it is never a source of load paths. Code that loads these
// graphics through the engine's path-based load calls (loadAtlas and friends) spells the full
// path as a string literal at the call site, exactly as src/assets/presence.cpp does; the
// engine's build scan is textual and a path assembled from this table is invisible to it.
//
// The table is generated from upstream's dump_gfx.py and the ROM by
// tools/asm_parser/parse_tile_graphics.py; edit the source facts and regenerate, not here. The
// decode itself, the value inversion, and the padding are specified in docs/contracts/tile-graphics.md.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich {

// A block's bit format. Port-design values: the ROM carries no symbol for this, so there are no
// bytes to preserve - the value is the port's own. The font is the only 1bpp block.
enum class TileGraphicFormat : std::uint8_t { OneBpp, TwoBpp };

// Bytes one tile occupies in the ROM: 8 for a 1bpp tile, 16 for a 2bpp tile (two interleaved
// bitplanes of eight rows).
constexpr int bytesPerTile(TileGraphicFormat format) {
    return format == TileGraphicFormat::OneBpp ? 8 : 16;
}

// One extractable graphics block.
struct TileGraphic {
    std::string_view  fileName;   // bare name written under assets/gfx/default/ (e.g. "font.png")
    std::uint16_t     romOffset;  // first byte of the block in the ROM
    std::uint16_t     tileCount;  // number of 8x8 tiles in the block
    TileGraphicFormat format;     // 1bpp (font) or 2bpp

    friend constexpr bool operator==(const TileGraphic&, const TileGraphic&) = default;
};

// kTileGraphics, the four blocks, generated at namespace scope.
#include "generated/tile_graphics_data.inc"

}  // namespace kirpich
