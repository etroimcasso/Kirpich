#include "render/screens/palette_rotation/level_grid_swatches.h"

#include <cstddef>
#include <string>

#include "render/components/color_swatch.h"
#include "render/screens/palette_rotation/layout.h"

namespace kirpich::render {

using namespace palette_rotation;

Regions LevelGridSwatches(const LevelColors& levels) {
    Regions out;
    out.reserve(levels.size() * 4);
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const Regions swatch = ColorSwatch("rotation-level-" + std::to_string(level), levels[level],
                                           gridSwatchX(level), gridSwatchY(level), kGridSquare);
        out.insert(out.end(), swatch.begin(), swatch.end());
    }
    return out;
}

}  // namespace kirpich::render
