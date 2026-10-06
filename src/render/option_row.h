#pragma once

// One option on a settings screen, as sprites: its label, its value, and the arrows either side of
// the value.
//
// The settings screens lay their parts out on the background's cell grid, and every screen the
// settings screen opens uses the same grid (systems/settings_screen.h): the label from column 3, the
// value from kOptionValueCol, the arrows at kOptionLeftArrowCol and kOptionRightArrowCol, the heading
// on kScreenTitleRow and the cursor on column 1. This header converts that grid to the viewport pixels
// a sprite is placed in, so a screen built from these components lines up cell for cell with one
// written into the map.
//
// The text is the font's (render/glyphs.h): letters, digits, a period and a hyphen.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"        // kVisibleCols
#include "render/tile_atlas.h"
#include "render/types.h"
#include "systems/settings_screen.h"  // the grid

namespace kirpich::render {

// A background cell's side, in viewport pixels, and so the distance from one character to the next.
inline constexpr int kOptionCell = 8;

// A run of cells, in viewport pixels.
[[nodiscard]] constexpr int optionPixels(std::size_t cells) noexcept {
    return static_cast<int>(cells) * kOptionCell;
}

// Where a heading of `length` characters starts: centered across the screen, rounding toward the
// left as the settings screen centers its own heading. A heading as wide as the screen starts at 0.
[[nodiscard]] constexpr int optionHeadingX(std::size_t length) noexcept {
    return length >= kVisibleCols ? 0 : optionPixels((kVisibleCols - length) / 2);
}

// The heading's row and the cursor's column, as every screen in the family places them.
inline constexpr int kOptionHeadingY = optionPixels(systems::kScreenTitleRow);
inline constexpr int kOptionCursorX  = optionPixels(systems::kCursorCol);

// The option on map row `line`. `left` and `right` say which ways the value can still move, and so
// which arrows are drawn (render/scroller_arrows.h).
[[nodiscard]] Sprites OptionRow(std::string_view label, std::string_view value, std::size_t line,
                                bool left, bool right, const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
