#pragma once

// The Palette settings screen: its layers.
//
// A backdrop of tiles, and over it the "palette" heading, the palette row with its number and arrows,
// the blinking cursor on that row, the swatch of the palette's four colors, and the seven pieces drawn
// in it. Everything is read from the settings each frame and drawn through the chosen palette, so the
// whole screen recolors as the palette steps.

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers PaletteSettingsScreen(const kirpich::Settings& s, bool blinkOn,
                                           const TileAtlas& atlas);

}  // namespace kirpich::render
