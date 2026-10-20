#pragma once

// One arrow, as a sprite: the game's own selector tile (kirpich::kSelectorTile), pointing one of four
// ways.
//
// The selector points right. Left is the same tile flipped; up and down are the tile turned a quarter,
// which is why every arrow is a sprite rather than a background cell - the hardware's cells can be
// flipped but never turned. The tile belongs to the copyright-and-title art, and the sprite names that
// sheet, so it draws correctly over a screen in either regime.
//
// Every arrow the render layer draws comes from here: the scroller arrows either side of an option's
// value, the page arrows above a heading and below a body, and the arrows of a selector a screen lays
// out for itself.

#include <cstdint>
#include <string>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/display_state.h"  // kSelectorTile

namespace kirpich::render {

using kirpich::kSelectorTile;

enum class ArrowDirection : std::uint8_t {
    RIGHT,  // the tile as drawn
    LEFT,   // flipped across its vertical axis
    UP,     // turned a quarter counter-clockwise
    DOWN,   // turned a quarter clockwise
};

// The arrow named `key`, with its top-left corner at viewport pixel (x, y) and depth `z`, through the
// object palette of `ramp` (a built-in ramp, or kCustomRamp for the custom set; other values clamp).
// Returns one sprite.
[[nodiscard]] Sprites Arrow(std::string key, int x, int y, ArrowDirection direction, std::int32_t z,
                            const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
