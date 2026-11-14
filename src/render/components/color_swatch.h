#pragma once

// A palette's four colors, side by side, as regions.
//
// They are regions rather than sprites or cells: the game's art has no solid-color tile to draw them
// with, and a region is a shape filled with one color, placed by pixel. The four squares touch, so the
// strip reads as one band of color rather than as four blocks. Hand the result to the layer the
// swatch belongs to as its `regions` (render/types.h, LayerOptions).

#include <array>
#include <string_view>

#include <retropp/draw_state.h>  // Rgba8

#include "render/types.h"

namespace kirpich::render {

// Four opaque squares of `square` pixels a side, `colors[0]` leftmost, abutting left to right from
// viewport pixel (x, y) - the strip is 4 * `square` wide. Each color is drawn with alpha 255 whatever
// its own alpha says. Square `i` is named `<keyStem>-<i>`.
[[nodiscard]] Regions ColorSwatch(std::string_view keyStem,
                                  const std::array<retropp::Rgba8, 4>& colors, int x, int y,
                                  int square);

}  // namespace kirpich::render
