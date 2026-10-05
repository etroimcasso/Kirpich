#pragma once

// The seven pieces, drawn in a palette, so a player choosing one sees the game's own art in it.
//
// Two rows - L J I O, then S Z T, in PieceKind order - each centered across the screen from the
// shapes' own widths with a cell between neighbors (render/palette_settings/layout.h). Each shape is
// a PieceShape, named for its kind, so the preview keeps its identity as the palette steps.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Sprites PiecePreview(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
