#include "render/stats_pages.h"

#include <algorithm>
#include <string>

#include <kirpich/piece_kind.h>
#include <kirpich/sprite_id.h>

#include "data/sprites.h"           // getSprite
#include "systems/stats_pages.h"    // the grid's geometry and which page is up

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// A piece's identity packs its kind and its rotation together, four rotations to a shape, and the
// spawn orientation is the first of them (include/kirpich/piece_kind.h).
constexpr std::size_t kRotationsPerShape = 4;

// Above the object buffer, so a shape is never hidden behind a cursor left over from another screen.
constexpr std::int32_t kShapeZ = 200;

// The shape's own smallest part offsets. The composed origin a falling piece is drawn from sits above
// and, for the square, to the left of the shape itself, so the seven would hang at different places
// on their lines if it were used here.
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

bool statsPieceShapesShown(kirpich::GameState state, const kirpich::ScreenUiState& ui) noexcept {
    if (state != kirpich::GameState::STATS_PAGE) {
        return false;
    }
    return systems::statsPageIsPieces(systems::statsBranchOf(ui.statsBranch), ui.statsPage);
}

ShapeOrigin statsPieceShapeOrigin(std::size_t kind, bool underPicker) noexcept {
    const systems::StatsPieceSlot slot = systems::statsPieceSlot(kind);
    return ShapeOrigin{
        .x = static_cast<int>(systems::kStatsPieceCols[slot.column]) * kCell + kStatsShapeXOffset,
        .y = static_cast<int>(systems::statsPieceLine(slot.row, underPicker)) * kCell +
             kStatsShapeYOffset,
    };
}

std::vector<retropp::Sprite> statsPieceShapeSprites(const kirpich::ScreenUiState& ui,
                                                    const TileAtlas& atlas, std::uint8_t ramp) {
    const bool underPicker =
        systems::statsBranchIsMode(systems::statsBranchOf(ui.statsBranch));

    std::vector<retropp::Sprite> sprites;
    sprites.reserve(kirpich::kPieceKindCount * 4);

    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        const auto id = static_cast<kirpich::SpriteId>(kind * kRotationsPerShape);
        const kirpich::Sprite& shape = kirpich::getSprite(id);

        const PartOrigin  least  = smallestOffsets(shape);
        const ShapeOrigin origin = statsPieceShapeOrigin(kind, underPicker);

        std::size_t index = 0;
        for (const kirpich::SpritePart& part : shape.parts) {
            const ResolvedTile art = resolveSpriteTile(part.tile, kirpich::TileSheet::GAMEPLAY,
                                                       /*palette1=*/false, atlas, ramp);
            sprites.push_back(retropp::Sprite{
                .key     = retropp::ObjectKey{"stats-piece-" + std::to_string(kind) + "-" +
                                              std::to_string(index)},
                .x       = origin.x + static_cast<int>(part.x) - static_cast<int>(least.x),
                .y       = origin.y + static_cast<int>(part.y) - static_cast<int>(least.y),
                .z       = kShapeZ,
                .atlas   = art.atlas,
                .tile    = art.cell,
                .palette = art.palette,
                .flipX   = part.xflip,
            });
            ++index;
        }
    }
    return sprites;
}

}  // namespace kirpich::render
