#include "render/screens/palette_editor/preview_backdrop.h"

#include <cstddef>

#include "data/tilemaps.h"                 // kTypeAGameplayTilemap
#include "render/background.h"             // kVisibleCols, kVisibleRows
#include "render/components/backdrop.h"
#include "render/screens/palette_editor/layout.h"
#include "state/playing_field_state.h"     // kPlayingFieldOriginCol

namespace kirpich::render {

using namespace palette_editor;

static_assert(kirpich::kTilemapScreenRows == kVisibleRows,
              "the playfield's tilemap has a wall tile for every visible row");

Cells PreviewBackdrop(const TileAtlas& atlas, std::uint8_t ramp) {
    Cells cells = Backdrop(atlas, ramp);
    for (std::size_t row = 0; row < kVisibleRows; ++row) {
        const ResolvedTile brick = resolveTile(
            kirpich::kTypeAGameplayTilemap[row][kirpich::kPlayingFieldOriginCol - 1],
            TileSheet::GAMEPLAY, atlas, ramp);
        const retropp::TileCell cell{.atlas = brick.atlas, .tile = brick.cell, .palette = brick.palette};
        cells[row * kVisibleCols + kWallLeftColumn]  = cell;
        cells[row * kVisibleCols + kWallRightColumn] = cell;
    }
    return cells;
}

}  // namespace kirpich::render
