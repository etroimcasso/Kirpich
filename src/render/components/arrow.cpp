#include "render/components/arrow.h"

#include <utility>

namespace kirpich::render {

Sprites Arrow(std::string key, int x, int y, ArrowDirection direction, std::int32_t z,
              const TileAtlas& atlas, std::uint8_t ramp) {
    const ResolvedTile art = resolveSpriteTile(kSelectorTile, kirpich::TileSheet::COPYRIGHT_TITLE,
                                               /*palette1=*/false, atlas, ramp);

    retropp::Rotation turn = retropp::Rotation::None;
    if (direction == ArrowDirection::UP) turn = retropp::Rotation::Rot270;
    if (direction == ArrowDirection::DOWN) turn = retropp::Rotation::Rot90;

    return {retropp::Sprite{
        .key      = retropp::ObjectKey{std::move(key)},
        .x        = x,
        .y        = y,
        .z        = z,
        .atlas    = art.atlas,
        .tile     = art.cell,
        .palette  = art.palette,
        .flipX    = direction == ArrowDirection::LEFT,
        .rotation = turn,
    }};
}

}  // namespace kirpich::render
