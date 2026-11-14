#pragma once

// The rotation screen, as the layers it is.
//
// Two layers: the backdrop, and either the held preview or the menu.
//
// The menu is the heading; the grid of the ten levels, two rows of five, each cell its level's number
// (LevelGridDigits) above that level's four colors (LevelGridSwatches); the editor item, dimmed; the
// rotation selector - its label, its value (`off`, `hotter`, `colder`, `rainbow`, `C` and the saved
// slot as two digits, or `C` alone while the working rotation is saved in no slot) and an arrow either
// side where a step exists; the blinking cursor before the row it is on; and the down page arrow. The
// grid shows the selected mode's ten palettes, so it changes as the selector steps; on `off` it shows
// the player's own palette ten times, which is what every level looks like then. The menu draws through
// the player's palette, paletteIndexOf(s).
//
// While Select is held (RotationScreenState::previewing) the held preview takes the screen: the
// backdrop with a brick wall down each edge (PreviewBackdrop), and a layer keyed `rotation-preview`
// holding the heading, the palette editor's words and pieces (PalettePreview) and the `level` item
// (LevelSelector) for RotationScreenState::previewLevel. It draws through the rotation set,
// kRotationRamp, which holds the mode's palette at that level while the preview is up
// (systems/frame_palette.h) - or through the player's palette while the mode is `off`. Laid out in
// pixels by layout.h beside this file.

#include <retropp/draw_state.h>

#include "render/screens/palette_rotation/rotation_screen_state.h"
#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/custom_rotations.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers PaletteRotationScreen(const kirpich::RotationScreenState& ui,
                                           const kirpich::Settings&            s,
                                           const kirpich::CustomRotations&     rotations,
                                           bool blinkOn, const TileAtlas& atlas);

}  // namespace kirpich::render
