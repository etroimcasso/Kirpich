#pragma once

// A level picker, as sprites: the word `level`, the level's number after it, and an arrow on each side
// of the number where a step that way exists.
//
//     level  <5>      with an up arrow over the 5 and a down arrow under it
//
// Up and right point to the next level, down and left to the previous one, so the four arrows stand
// for the four directions a player can press. At level 0 the left and down arrows are gone; at the
// last level, kRotationLevelCount - 1, the right and up arrows are. The number is one digit.

#include <cstdint>
#include <string_view>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The number's left edge, from the word's left edge.
inline constexpr int kLevelSelectorNumberDx = 48;

// From the word's left edge to the right arrow's right edge: the whole item's width - the number's
// offset, then the number and the right arrow, a glyph of 8 pixels each.
inline constexpr int kLevelSelectorWidth = kLevelSelectorNumberDx + 2 * 8;

// The arrows' depth in the layer they are drawn on, above the glyphs.
inline constexpr std::int32_t kLevelSelectorArrowZ = 30;

// The item for `level`, 0-9, with the word's top-left at viewport pixel (x, y), through `ramp`'s
// palettes. The word and the number are named by position, as Glyphs names them; the arrows
// `<keyStem>-left`, `<keyStem>-right`, `<keyStem>-up` and `<keyStem>-down`. The left arrow stands in
// the cell before the number, the right arrow in the cell after it, the up arrow a glyph above it and
// the down arrow a glyph below it - the item reaches from y - 8 to y + 16.
[[nodiscard]] Sprites LevelSelector(std::string_view keyStem, std::uint8_t level, int x, int y,
                                    const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
