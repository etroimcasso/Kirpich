#include "render/background.h"

#include <span>

namespace kirpich::render {

void composeBackground(const PlayingFieldState& field, TileSheet sheet, const TileAtlas& atlas,
                       std::vector<retropp::TileCell>& cells) {
    cells.resize(kVisibleCells);

    for (std::size_t row = 0; row < kVisibleRows; ++row) {
        for (std::size_t col = 0; col < kVisibleCols; ++col) {
            const ResolvedTile art = resolveTile(field.board[row][col], sheet, atlas);
            cells[row * kVisibleCols + col] = retropp::TileCell{
                .atlas   = art.atlas,
                .tile    = art.cell,
                .palette = art.palette,
            };
        }
    }
}

retropp::DrawLayer backgroundLayer(const std::vector<retropp::TileCell>& cells,
                                   retropp::ViewportResolution viewport) {
    return retropp::DrawLayer{
        // Identity only - z alone orders - but it has to be the same string every frame for the
        // layer to reconcile against its own previous tick.
        .key = kBackgroundLayerKey,
        .z   = kBackgroundLayerZ,
        // The original never scrolls the background: there is no camera in this game, and the map
        // is exactly the screen. So the layer is the viewport, parked at the origin.
        .size   = viewport.size(),
        .scroll = retropp::LayerScroll{},
        .content =
            retropp::TileContent{
                .widthInTiles  = static_cast<int>(kVisibleCols),
                .heightInTiles = static_cast<int>(kVisibleRows),
                .cells         = std::span<const retropp::TileCell>(cells),
                // Finite, never toroidal. Map and screen are the same size, so no sample can fall
                // outside - this states the intent rather than guarding a real edge.
                .wrap = retropp::TileWrap::Blank,
            },
    };
}

}  // namespace kirpich::render
