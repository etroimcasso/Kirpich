#include "systems/screen.h"

#include <cstddef>

namespace kirpich::systems {

void loadScreenTilemap(DisplayState& display, const ScreenTilemap& tilemap) {
    // The original walks 18 rows of 20 cells, adding the map's 32-cell stride between rows
    // (tetris.asm:6414-6430). Here the stride is the map's own row shape, so the walk is the two
    // loops and nothing else.
    for (std::size_t row = 0; row < kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
            display.map[row][col] = tilemap[row][col];
        }
    }
}

void loadTileSheet(DisplayState& display, TileSheet sheet) {
    display.sheet = sheet;
}

}  // namespace kirpich::systems
