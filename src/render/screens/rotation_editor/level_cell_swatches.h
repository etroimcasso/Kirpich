#pragma once

// The rotation editor's grid as color: each level's four colors, darkest first, under that level's
// text (cellSwatchX and cellSwatchY in layout.h beside this file). Hand the result to the layer as its
// `regions` (render/types.h, LayerOptions). The colors are the working rotation's, so a step of a
// level's number recolors its cell on the same frame.

#include "render/types.h"
#include "state/custom_rotations.h"  // RotationProgression

namespace kirpich::render {

// One ColorSwatch per level, squares of kCellSquare pixels, level `i`'s named
// `rotation-editor-level-<i>` - so each square is `rotation-editor-level-<i>-<shade>`.
[[nodiscard]] Regions LevelCellSwatches(const kirpich::RotationProgression& rotation);

}  // namespace kirpich::render
