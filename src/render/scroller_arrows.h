#pragma once

// The two arrows either side of an option's value, as sprites.
//
// They are the game's own selector arrow - the one the title screen points at the player count with -
// so a scroller is drawn in the game's own hand rather than in shapes invented for it. The selector
// points right; the left arrow is the same tile flipped. The tile belongs to the copyright-and-title
// art, and the arrows name that set explicitly, so they draw correctly whichever set the background is
// using.
//
// An arrow is drawn only where the value can still move that way, so the ends of a range are visible
// rather than something a player finds by pressing.

#include <cstddef>
#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The selector arrow's index in the copyright-and-title art.
inline constexpr std::uint8_t kSelectorTile = 0x58;

// The arrows for the option on map row `line`: the left one at kOptionLeftArrowCol when `left` is set,
// the right one at kOptionRightArrowCol when `right` is set (systems/settings_screen.h). Each is named
// for its line and its side, so a row's arrows keep their identity between frames.
[[nodiscard]] Sprites ScrollerArrows(std::size_t line, bool left, bool right,
                                     const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
