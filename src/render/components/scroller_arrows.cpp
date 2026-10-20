#include "render/components/scroller_arrows.h"

#include <string>

#include "render/components/arrow.h"
#include "render/components/option_row.h"  // optionPixels
#include "systems/settings_screen.h"       // the arrow columns

namespace kirpich::render {

Sprites ScrollerArrows(std::size_t line, bool left, bool right, const TileAtlas& atlas,
                       std::uint8_t ramp) {
    const std::string stem = "scroller-" + std::to_string(line);
    const int         y    = optionPixels(line);

    Sprites out;
    const auto add = [&out](const Sprites& part) { out.insert(out.end(), part.begin(), part.end()); };
    if (left) {
        add(Arrow(stem + "-left", optionPixels(systems::kOptionLeftArrowCol), y,
                  ArrowDirection::LEFT, /*z=*/0, atlas, ramp));
    }
    if (right) {
        add(Arrow(stem + "-right", optionPixels(systems::kOptionRightArrowCol), y,
                  ArrowDirection::RIGHT, /*z=*/0, atlas, ramp));
    }
    return out;
}

}  // namespace kirpich::render
