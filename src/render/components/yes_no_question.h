#pragma once

// A question that guards an action that cannot be taken back, as the layer it takes the screen with.
//
// It stands where every confirm in the settings family stands (systems/settings_screen.h): the title
// on the heading row, the question on its two lines, and "no" and "yes" on the answer line, centered
// as a pair by confirmChoiceColumns, with a hyphen cursor kConfirmCursorGap cells before the answer
// `yes` names - drawn only while `blinkOn`. Every part is centered across the screen. The words are
// the caller's; the layout is the family's.

#include <cstdint>
#include <string_view>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/tile_atlas.h"

namespace kirpich::render {

// The two answers, in the order they stand.
inline constexpr std::string_view kQuestionNo  = "no";
inline constexpr std::string_view kQuestionYes = "yes";

// The question draws over its screen's backdrop.
inline constexpr std::int32_t kQuestionZ = 10;

// The sprite layer named `key`, at kQuestionZ, through the object palettes of `ramp`: `title` on the
// heading row, `firstLine` and `secondLine` on the question lines, the two answers, and the cursor on
// the one `yes` names while `blinkOn`.
[[nodiscard]] retropp::DrawLayer YesNoQuestion(std::string_view key, std::string_view title,
                                               std::string_view firstLine,
                                               std::string_view secondLine, bool yes, bool blinkOn,
                                               const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
