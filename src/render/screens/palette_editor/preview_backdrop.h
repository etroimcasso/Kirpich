#pragma once

// The palette editor's backdrop while the held preview is up: the plain backdrop with a brick wall down
// each edge of the screen.
//
// The bricks are the playfield's own left wall, read from the Type A playfield's tilemap row by row,
// so they repeat down each column exactly as they run beside the well in a round. They are background
// cells drawn through the ramp's content palette, which draws the brick's lightest shade as a color; an
// object palette would draw that shade see-through. The two columns are kWallLeftColumn and
// kWallRightColumn (layout.h beside this file).

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Cells PreviewBackdrop(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
