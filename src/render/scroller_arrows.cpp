#include "render/scroller_arrows.h"

#include <string>

#include "render/option_row.h"        // optionPixels
#include "systems/settings_screen.h"  // the arrow columns

namespace kirpich::render {

Sprites ScrollerArrows(std::size_t line, bool left, bool right, const TileAtlas& atlas,
                       std::uint8_t ramp) {
    const ResolvedTile art = resolveSpriteTile(kSelectorTile, kirpich::TileSheet::COPYRIGHT_TITLE,
                                               /*palette1=*/false, atlas, ramp);
    const std::string stem = "scroller-" + std::to_string(line);

    Sprites out;
    const auto arrow = [&](std::string key, std::size_t col, bool flip) {
        out.push_back(retropp::Sprite{
            .key     = retropp::ObjectKey{std::move(key)},
            .x       = optionPixels(col),
            .y       = optionPixels(line),
            .atlas   = art.atlas,
            .tile    = art.cell,
            .palette = art.palette,
            .flipX   = flip,
        });
    };

    if (left) arrow(stem + "-left", systems::kOptionLeftArrowCol, /*flip=*/true);
    if (right) arrow(stem + "-right", systems::kOptionRightArrowCol, /*flip=*/false);
    return out;
}

}  // namespace kirpich::render
