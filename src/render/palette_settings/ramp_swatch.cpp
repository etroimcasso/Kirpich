#include "render/palette_settings/ramp_swatch.h"

#include <array>
#include <string>

#include "render/palette_settings/layout.h"
#include "render/palettes.h"  // rampColours

namespace kirpich::render {

using namespace palette_settings;

Regions RampSwatch(std::uint8_t ramp) {
    const std::array<retropp::Rgba8, 4> colors = rampColours(clampShadeRamp(ramp));
    static_assert(colors.size() == kSwatchSquares, "one square per shade");

    Regions out;
    out.reserve(colors.size());

    const auto top  = static_cast<float>(kSwatchY);
    const auto side = static_cast<float>(kSwatchSquare);
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto x = static_cast<float>(kSwatchX + static_cast<int>(i) * kSwatchSquare);

        retropp::Rgba8 fill = colors[i];
        fill.a              = 255;

        out.push_back(retropp::Region{
            .key   = retropp::ObjectKey{"palette-swatch-" + std::to_string(i)},
            .shape = retropp::ShapePoints{
                .points = {{x, top}, {x + side, top}, {x + side, top + side}, {x, top + side}}},
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::ColorFill, .fill = fill}},
        });
    }
    return out;
}

}  // namespace kirpich::render
