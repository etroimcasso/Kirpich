#include "render/components/color_swatch.h"

#include <cstddef>
#include <string>

namespace kirpich::render {

Regions ColorSwatch(std::string_view keyStem, const std::array<retropp::Rgba8, 4>& colors, int x,
                    int y, int square) {
    const std::string stem{keyStem};

    Regions out;
    out.reserve(colors.size());

    const auto top  = static_cast<float>(y);
    const auto side = static_cast<float>(square);
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto left = static_cast<float>(x + static_cast<int>(i) * square);

        retropp::Rgba8 fill = colors[i];
        fill.a              = 255;

        out.push_back(retropp::Region{
            .key   = retropp::ObjectKey{stem + "-" + std::to_string(i)},
            .shape = retropp::ShapePoints{.points = {{left, top},
                                                     {left + side, top},
                                                     {left + side, top + side},
                                                     {left, top + side}}},
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::ColorFill, .fill = fill}},
        });
    }
    return out;
}

}  // namespace kirpich::render
