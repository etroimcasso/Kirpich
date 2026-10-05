#pragma once

// The Display settings screen: its layers.
//
// A backdrop of tiles, and over it the "display" heading, the fullscreen and size rows with their
// values and arrows, and the blinking cursor on the row it is on. Every part is read from its
// parameters each frame: the values come from the settings, so a change made from outside the screen
// - the fullscreen shortcut - shows on it the frame it happens, and the arrows follow the values to
// the ends of their ranges.

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/display_settings_state.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers DisplaySettingsScreen(const kirpich::DisplaySettingsState& ui,
                                           const kirpich::Settings& s, bool blinkOn,
                                           const TileAtlas& atlas);

}  // namespace kirpich::render
