#include "render/piece_shape.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <kirpich/sprite_id.h>

#include "data/sprites.h"  // getSprite

namespace kirpich::render {

namespace {

// A piece's identity packs its kind and its rotation together, four rotations to a shape, and the
// spawn orientation is the first of them (include/kirpich/piece_kind.h).
constexpr std::size_t kRotationsPerShape = 4;

// One part's side, in viewport pixels.
constexpr int kPartSize = 8;

const kirpich::Sprite& spawnShape(kirpich::PieceKind kind) {
    return kirpich::getSprite(
        static_cast<kirpich::SpriteId>(static_cast<std::size_t>(kind) * kRotationsPerShape));
}

// The shape's own smallest part offsets.
struct PartOrigin {
    std::uint8_t y = 0;
    std::uint8_t x = 0;
};

PartOrigin smallestOffsets(const kirpich::Sprite& shape) {
    PartOrigin least{.y = 255, .x = 255};
    for (const kirpich::SpritePart& part : shape.parts) {
        least.y = std::min(least.y, part.y);
        least.x = std::min(least.x, part.x);
    }
    return shape.parts.empty() ? PartOrigin{} : least;
}

}  // namespace

PieceShapeExtent pieceShapeExtent(kirpich::PieceKind kind) noexcept {
    const kirpich::Sprite& shape = spawnShape(kind);
    if (shape.parts.empty()) {
        return {};
    }
    const PartOrigin least = smallestOffsets(shape);

    int right  = 0;
    int bottom = 0;
    for (const kirpich::SpritePart& part : shape.parts) {
        right  = std::max(right, static_cast<int>(part.x) - static_cast<int>(least.x) + kPartSize);
        bottom = std::max(bottom, static_cast<int>(part.y) - static_cast<int>(least.y) + kPartSize);
    }
    return {.width = right, .height = bottom};
}

Sprites PieceShape(kirpich::PieceKind kind, int x, int y, std::int32_t z,
                   std::string_view keyStem, const TileAtlas& atlas, std::uint8_t ramp) {
    const kirpich::Sprite& shape = spawnShape(kind);
    const PartOrigin       least = smallestOffsets(shape);

    Sprites out;
    out.reserve(shape.parts.size());

    std::size_t index = 0;
    for (const kirpich::SpritePart& part : shape.parts) {
        const ResolvedTile art = resolveSpriteTile(part.tile, kirpich::TileSheet::GAMEPLAY,
                                                   /*palette1=*/false, atlas, ramp);
        out.push_back(retropp::Sprite{
            .key     = retropp::ObjectKey{std::string{keyStem} + "-" + std::to_string(index)},
            .x       = x + static_cast<int>(part.x) - static_cast<int>(least.x),
            .y       = y + static_cast<int>(part.y) - static_cast<int>(least.y),
            .z       = z,
            .atlas   = art.atlas,
            .tile    = art.cell,
            .palette = art.palette,
            .flipX   = part.xflip,
        });
        ++index;
    }
    return out;
}

}  // namespace kirpich::render
