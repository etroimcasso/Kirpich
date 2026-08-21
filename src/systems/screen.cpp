#include "systems/screen.h"

#include <cstddef>
#include <cstdint>

#include "data/charmap.h"  // encodeCharmapText

namespace kirpich::systems {

void loadScreenTilemap(BackgroundMap& map, const ScreenTilemap& tilemap) {
    // The original walks 18 rows of 20 cells, adding the map's 32-cell stride between rows
    // (tetris.asm:6414-6430). Here the stride is the map's own row shape, so the walk is the two
    // loops and nothing else.
    for (std::size_t row = 0; row < kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
            map[row][col] = tilemap[row][col];
        }
    }
}

void loadScreenTilemap(DisplayState& display, const ScreenTilemap& tilemap) {
    loadScreenTilemap(display.map, tilemap);
}

void writeMapText(BackgroundMap& map, std::size_t row, std::size_t col, std::string_view text) {
    const auto glyphs = encodeCharmapText(text);
    if (!glyphs) {
        return;
    }
    for (std::size_t i = 0; i < glyphs->size() && col + i < kTilemapScreenCols; ++i) {
        map[row][col + i] = static_cast<std::uint8_t>((*glyphs)[i]);
    }
}

void loadTileSheet(DisplayState& display, TileSheet sheet) {
    display.sheet = sheet;
}

}  // namespace kirpich::systems
