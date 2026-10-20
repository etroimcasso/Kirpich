#include "render/components/page_arrows.h"

#include <string>

#include "render/components/arrow.h"
#include "systems/settings_screen.h"  // kPageArrowCol, kPageUpArrowRow, kPageDownArrowRow

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

constexpr int kX     = static_cast<int>(systems::kPageArrowCol) * kCell;
constexpr int kUpY   = static_cast<int>(systems::kPageUpArrowRow) * kCell;
constexpr int kDownY = static_cast<int>(systems::kPageDownArrowRow) * kCell;

}  // namespace

Sprites PageArrows(std::string_view keyStem, bool up, bool down, const TileAtlas& atlas,
                   std::uint8_t ramp) {
    const std::string stem{keyStem};
    Sprites           out;
    if (up) {
        const Sprites arrow =
            Arrow(stem + "-up", kX, kUpY, ArrowDirection::UP, kPageArrowZ, atlas, ramp);
        out.insert(out.end(), arrow.begin(), arrow.end());
    }
    if (down) {
        const Sprites arrow =
            Arrow(stem + "-down", kX, kDownY, ArrowDirection::DOWN, kPageArrowZ, atlas, ramp);
        out.insert(out.end(), arrow.begin(), arrow.end());
    }
    return out;
}

}  // namespace kirpich::render
