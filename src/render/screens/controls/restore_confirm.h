#pragma once

// The question the Controls screen asks before it restores the defaults, as the layer it takes the
// screen with.
//
// It is the family's YesNoQuestion (render/components/yes_no_question.h), named `controls-confirm`:
// the title "restore defaults", the question "restore every / key and button", and "no" and "yes"
// with a hyphen cursor before the answer `yes` names - drawn only while `blinkOn`.

#include <cstdint>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/tile_atlas.h"

namespace kirpich::render {

[[nodiscard]] retropp::DrawLayer RestoreConfirm(bool yes, bool blinkOn, const TileAtlas& atlas,
                                                std::uint8_t ramp);

}  // namespace kirpich::render
