#include "render/stats_pages.h"

#include <string>

#include <kirpich/piece_kind.h>

#include "render/piece_shape.h"
#include "systems/stats_pages.h"    // the grid's geometry and which page is up

namespace kirpich::render {

namespace {

constexpr int kCell = 8;  // a background cell's side, in viewport pixels

// Above the object buffer, so a shape is never hidden behind a cursor left over from another screen.
constexpr std::int32_t kShapeZ = 200;

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
        const ShapeOrigin origin = statsPieceShapeOrigin(kind, underPicker);
        const Sprites     shape =
            PieceShape(static_cast<kirpich::PieceKind>(kind), origin.x, origin.y, kShapeZ,
                       "stats-piece-" + std::to_string(kind), atlas, ramp);
        sprites.insert(sprites.end(), shape.begin(), shape.end());
    }
    return sprites;
}

}  // namespace kirpich::render
