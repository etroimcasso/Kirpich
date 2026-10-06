#pragma once

// The question the Controls screen asks before it restores the defaults, as the layer it takes the
// screen with.
//
// It reads as the settings screen's own confirms do and stands on the same cells
// (systems/settings_screen.h): the title "restore defaults" on the heading row, the question
// "restore every / key and button" on the two question lines, and "no" and "yes" on the answer line,
// with a hyphen cursor two cells before the answer `yes` names - drawn only while `blinkOn`.

#include <cstdint>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/tile_atlas.h"

namespace kirpich::render {

[[nodiscard]] retropp::DrawLayer RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas,
                                                std::uint8_t ramp);

}  // namespace kirpich::render
