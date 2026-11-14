#pragma once

// The grid's colors, as regions: each level's four colors at its cell of the rotation screen's grid
// (gridSwatchX and gridSwatchY in layout.h beside this file), under the digit LevelGridDigits draws
// there. Hand the result to the layer as its `regions` (render/types.h, LayerOptions).

#include <array>

#include <retropp/draw_state.h>  // Rgba8

#include "render/types.h"
#include "state/custom_rotations.h"  // kRotationLevelCount

namespace kirpich::render {

// Ten palettes' colors, level 0 first, each darkest shade first.
using LevelColors = std::array<std::array<retropp::Rgba8, 4>, kirpich::kRotationLevelCount>;

// One ColorSwatch per level, squares of kGridSquare pixels, level `i`'s named `rotation-level-<i>` -
// so each square is `rotation-level-<i>-<shade>`.
[[nodiscard]] Regions LevelGridSwatches(const LevelColors& levels);

}  // namespace kirpich::render
