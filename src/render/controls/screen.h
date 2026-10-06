#pragma once

// The Controls screen: its layers.
//
// A backdrop of tiles, and over it the "controls" heading, the two column heads, a row for each Game
// Boy button holding its name, its key's name and its controller button's name, and the "restore
// defaults" row under them - dimmed while the bindings already are the defaults, since then it has
// nothing to restore. The cursor is a hyphen in the cell before the name it marks - before the
// label on the restore row - blinking while the screen is idle and held while it waits for a press.
// While it waits, the cell it waits on reads "..." and the bottom of the screen says what kind of
// press it wants and that Escape cancels. While it asks whether to restore the defaults, the question
// (render/controls/restore_confirm.h) takes the screen in place of all of that.
//
// Every part is read from the parameters each frame. The bindings come from the player's controls, so
// a binding shows the frame it is made; the controller names come from `padFamily`, the family of the
// pad the player has connected (render/controls/names.h).

#include <cstdint>

#include <retropp/input.h>  // ControllerType

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/controls.h"
#include "state/controls_screen_state.h"

namespace kirpich::render {

[[nodiscard]] Layers ControlsScreen(const kirpich::ControlsScreenState& ui,
                                    const kirpich::Controls& controls,
                                    retropp::ControllerType padFamily, bool blinkOn,
                                    const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
