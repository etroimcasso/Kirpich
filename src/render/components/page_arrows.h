#pragma once

// A screen's two page arrows, as sprites: the selector stood on end, one above the heading pointing up
// and one below the body pointing down, both on the screen's center column.
//
// They say there is another screen that way. The up arrow stands above the heading because an arrow
// drawn over a screen's text says the text moves, and one above the heading says the screen does.
// Their place is the one every screen in the settings family shares (kPageArrowCol, kPageUpArrowRow
// and kPageDownArrowRow in systems/settings_screen.h), so a page arrow stands in the same spot on every
// screen that has one.

#include <cstdint>
#include <string_view>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// Above every sprite the object buffer can produce, so a page arrow is never hidden behind one.
inline constexpr std::int32_t kPageArrowZ = 100;

// The up arrow, named `<keyStem>-up`, while `up` is set, and the down arrow, named `<keyStem>-down`,
// while `down` is set, through the object palette of `ramp`. The name is the caller's, so one screen's
// arrow is never eased into another's.
[[nodiscard]] Sprites PageArrows(std::string_view keyStem, bool up, bool down,
                                 const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
