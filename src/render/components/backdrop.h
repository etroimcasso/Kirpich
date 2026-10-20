#pragma once

// The screen's background: the cells behind everything else.
//
// It is genuinely background, so it is tiles rather than sprites - the one part of this screen that
// is. Plain for now; the art it draws is a first cut and changing it changes nothing else.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Cells Backdrop(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
