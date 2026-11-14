#pragma once

// The grid's level numbers, as sprites: the digits `0` to `9`, each at its cell of the rotation
// screen's grid (gridDigitX and gridDigitY in layout.h beside this file), over the swatch
// LevelGridSwatches draws there.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The ten digits through `ramp`'s font palette, named by position as Glyphs names them.
[[nodiscard]] Sprites LevelGridDigits(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
