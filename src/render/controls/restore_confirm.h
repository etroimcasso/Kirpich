#pragma once

// The question the Controls screen asks before it restores the defaults, as sprites.
//
// It reads as the settings screen's own confirms do and stands on the same cells
// (systems/settings_screen.h): the title "restore defaults" on the heading row, the question
// "restore every / key and button" on the two question lines, and "no" and "yes" on the answer line,
// with a hyphen cursor two cells before the answer `yes` names - drawn only while `blinkOn`.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Sprites RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas,
                                     std::uint8_t ramp);

}  // namespace kirpich::render
