#pragma once

// The Palette settings screen: its layers.
//
// A backdrop of tiles, and over it the "palette" heading, the palette row with its value and arrows,
// the blinking cursor on that row, the swatch of the palette's four colors, and the seven pieces drawn
// in it. Everything is read from the settings and the player's custom palettes each frame and drawn
// through the palette in use, so the whole screen recolors as the palette steps. The value reads the
// ramp's number, or `C` and the two-digit slot the custom palette in use is saved in, or `C` alone
// while it is saved in none.

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/custom_palettes.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers PaletteSettingsScreen(const kirpich::Settings& s,
                                           const kirpich::CustomPalettes& palettes, bool blinkOn,
                                           const TileAtlas& atlas);

}  // namespace kirpich::render
