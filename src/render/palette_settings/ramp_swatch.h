#pragma once

// A palette's four colors, side by side.
//
// They are regions rather than sprites or cells: the game's art has no solid-color tile to draw them
// with, and a region is a shape filled with one color, placed by pixel. The four squares touch, so the
// strip reads as one band of color rather than as four blocks. Hand the result to the layer the
// swatch belongs to as its `regions` (render/types.h, LayerOptions).

#include <cstdint>

#include "render/types.h"

namespace kirpich::render {

// Four squares, darkest shade to lightest, opaque, abutting and centered across the screen on the
// line under the palette row (render/palette_settings/layout.h). A ramp past the last is drawn as the
// last.
[[nodiscard]] Regions RampSwatch(std::uint8_t ramp);

}  // namespace kirpich::render
