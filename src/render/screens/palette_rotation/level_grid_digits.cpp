#include "render/screens/palette_rotation/level_grid_digits.h"

#include <cstddef>
#include <string>

#include "render/components/glyphs.h"
#include "render/screens/palette_rotation/layout.h"

namespace kirpich::render {

using namespace palette_rotation;

Sprites LevelGridDigits(const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites out;
    for (std::size_t level = 0; level < kirpich::kRotationLevelCount; ++level) {
        const Sprites digit =
            Glyphs(std::to_string(level), gridDigitX(level), gridDigitY(level), kGlyph, atlas, ramp);
        out.insert(out.end(), digit.begin(), digit.end());
    }
    return out;
}

}  // namespace kirpich::render
