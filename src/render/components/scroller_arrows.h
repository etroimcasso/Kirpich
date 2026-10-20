#pragma once

// The two arrows either side of an option's value, as sprites.
//
// Each is an Arrow (render/components/arrow.h): the game's own selector, pointing right on the right
// and flipped to point left on the left. An arrow is drawn only where the value can still move that
// way, so the ends of a range are visible rather than something a player finds by pressing.

#include <cstddef>
#include <cstdint>

#include "render/components/arrow.h"  // kSelectorTile
#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The arrows for the option on map row `line`: the left one at kOptionLeftArrowCol when `left` is set,
// the right one at kOptionRightArrowCol when `right` is set (systems/settings_screen.h), both at depth
// 0 within their layer. Each is named `scroller-<line>-left` or `scroller-<line>-right`, so a row's
// arrows keep their identity between frames.
[[nodiscard]] Sprites ScrollerArrows(std::size_t line, bool left, bool right,
                                     const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
