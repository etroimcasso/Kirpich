#include "render/screens/rotation_editor/level_preview_backdrop.h"

#include <cstddef>

#include <kirpich/char_tile.h>

#include "data/tilemaps.h"                       // kTypeAGameplayTilemap
#include "render/background.h"                   // kVisibleCols
#include "render/components/preview_backdrop.h"

namespace kirpich::render {

static_assert(kirpich::kTypeAGameplayTilemap[systems::kTypeALevelRow][systems::kTypeALevelCol] ==
                  static_cast<std::uint8_t>(kirpich::CharTile::SPACE),
              "the number's cell is blank in the tilemap, for the readout to write");
static_assert(kLevelBoxSourceColumn + kLevelBoxWidth <= kirpich::kTilemapScreenCols,
              "the box's columns are inside the tilemap");

Cells LevelPreviewBackdrop(std::uint8_t level, std::size_t column, std::size_t row,
                           const TileAtlas& atlas, std::uint8_t ramp) {
    Cells cells = PreviewBackdrop(atlas, ramp);
    for (std::size_t r = 0; r < kLevelBoxHeight; ++r) {
        for (std::size_t c = 0; c < kLevelBoxWidth; ++c) {
            const bool number = r == kLevelBoxNumberRow && c == kLevelBoxNumberColumn;
            // A digit's tile is the digit itself: the font puts 0-9 in the first ten slots.
            const std::uint8_t source =
                number ? static_cast<std::uint8_t>(level % 10)
                       : kirpich::kTypeAGameplayTilemap[kLevelBoxSourceRows[r]]
                                                       [kLevelBoxSourceColumn + c];
            const ResolvedTile tile = resolveTile(source, TileSheet::GAMEPLAY, atlas, ramp);
            cells[(row + r) * kVisibleCols + column + c] =
                retropp::TileCell{.atlas = tile.atlas, .tile = tile.cell, .palette = tile.palette};
        }
    }
    return cells;
}

}  // namespace kirpich::render
