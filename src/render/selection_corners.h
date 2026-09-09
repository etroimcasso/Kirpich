#pragma once

// A corner selector: four brackets marking out a box, the way a crosshair frames what it is over.
//
// It marks what is selected without touching what is selected - the thing inside keeps its own
// colours, so a grid of icons reads the same whether or not the cursor is on one of them. That is
// what a selector drawn as its own object buys over recolouring the item underneath.
//
// The brackets are shapes rather than art: two bars to a corner, filled through a region, so no tile
// has to exist for them and the box can be any size a caller wants.

#include <cstdint>
#include <string_view>

#include <retropp/palette.h>  // Rgba8

#include "render/types.h"

namespace kirpich::render {

// How thick a bracket's bars are, and how far they run from the corner.
inline constexpr float kSelectorThickness = 2.0f;
inline constexpr float kSelectorArm       = 6.0f;

// The four corner brackets of the box at (x, y) with the given size, named from `key`.
[[nodiscard]] Regions SelectionCorners(std::string_view key, float x, float y, float w, float h,
                                       retropp::Rgba8 colour);

}  // namespace kirpich::render
