#include "render/selection_corners.h"

#include <string>

namespace kirpich::render {

namespace {

// One bar of a bracket: a filled rectangle, named so it keeps its identity between frames.
[[nodiscard]] retropp::Region bar(std::string_view key, std::string_view part, float x, float y,
                                  float w, float h, retropp::Rgba8 colour) {
    return retropp::Region{
        .key   = retropp::ObjectKey{std::string{key} + "-" + std::string{part}},
        .shape = retropp::ShapePoints::rectangle(retropp::Point{x, y}, w, h),
        .effects =
            {
                retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::ColorFill,
                                           .fill = colour},
            },
    };
}

}  // namespace

Regions SelectionCorners(std::string_view key, float x, float y, float w, float h,
                         retropp::Rgba8 colour) {
    const float t   = kSelectorThickness;
    const float arm = kSelectorArm;

    const float right  = x + w;
    const float bottom = y + h;

    return {
        // Top left: along the top, and down the side.
        bar(key, "tl-h", x, y, arm, t, colour),
        bar(key, "tl-v", x, y, t, arm, colour),

        // Top right.
        bar(key, "tr-h", right - arm, y, arm, t, colour),
        bar(key, "tr-v", right - t, y, t, arm, colour),

        // Bottom left.
        bar(key, "bl-h", x, bottom - t, arm, t, colour),
        bar(key, "bl-v", x, bottom - arm, t, arm, colour),

        // Bottom right.
        bar(key, "br-h", right - arm, bottom - t, arm, t, colour),
        bar(key, "br-v", right - t, bottom - arm, t, arm, colour),
    };
}

}  // namespace kirpich::render
