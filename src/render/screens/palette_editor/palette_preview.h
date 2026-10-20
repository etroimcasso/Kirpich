#pragma once

// What the held preview shows of a palette, as sprites: text in its three inks, then the seven pieces.
//
// The words `selected`, `unselected` and `dimmed`, top to bottom, are each drawn through the font
// palette they name - SelectedGlyphs, Glyphs and DimGlyphs - so the three kinds of text the game draws
// can be told apart, or not, in the palette being judged. The pieces are PiecePreview at its own fixed
// place, named `preview-piece`, the same shapes where the Palette screen draws them. Laid out by
// layout.h beside this file.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Sprites PalettePreview(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
