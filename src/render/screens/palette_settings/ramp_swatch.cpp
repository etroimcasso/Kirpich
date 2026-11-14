#include "render/screens/palette_settings/ramp_swatch.h"

#include "render/components/color_swatch.h"
#include "render/screens/palette_settings/layout.h"

namespace kirpich::render {

using namespace palette_settings;

static_assert(kSwatchSquares == 4, "one square per shade");

Regions RampSwatch(const TileAtlas& atlas, std::uint8_t ramp) {
    return ColorSwatch("palette-swatch", shadesOf(atlas, ramp), kSwatchX, kSwatchY, kSwatchSquare);
}

}  // namespace kirpich::render
