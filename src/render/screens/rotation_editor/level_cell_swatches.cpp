#include "render/screens/rotation_editor/level_cell_swatches.h"

#include <array>
#include <cstddef>
#include <string>

#include "render/components/color_swatch.h"
#include "render/screens/rotation_editor/layout.h"

namespace kirpich::render {

using namespace rotation_editor;

Regions LevelCellSwatches(const kirpich::RotationProgression& rotation) {
    Regions out;
    out.reserve(rotation.levels.size() * 4);
    for (std::size_t level = 0; level < rotation.levels.size(); ++level) {
        const ShadeRamp& p = rotation.levels[level];
        const Regions    swatch =
            ColorSwatch("rotation-editor-level-" + std::to_string(level),
                        std::array<retropp::Rgba8, 4>{p.darkest, p.dark, p.light, p.lightest},
                        cellSwatchX(level), cellSwatchY(level), kCellSquare);
        out.insert(out.end(), swatch.begin(), swatch.end());
    }
    return out;
}

}  // namespace kirpich::render
