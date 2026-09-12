#include "render/backdrop.h"

#include <kirpich/char_tile.h>

#include "render/background.h"  // kVisibleCells

namespace kirpich::render {

Cells Backdrop(const TileAtlas& atlas, std::uint8_t ramp) {
    const ResolvedTile art =
        resolveTile(static_cast<std::uint8_t>(CharTile::SPACE), TileSheet::GAMEPLAY, atlas, ramp);
    return Cells(kVisibleCells,
                 retropp::TileCell{.atlas = art.atlas, .tile = art.cell, .palette = art.palette});
}

}  // namespace kirpich::render
