#pragma once

// The rotation editor's grid as text: for each of the ten levels, its digit, a colon and the number of
// the palette it wears, at its cell of the grid (cellX and cellY in layout.h beside this file).
//
// The number is paletteChoiceText of the level's stop (levelPaletteChoice): a ramp's number counted
// from one, `C` and a saved palette's slot, or `C` alone for colors on no stop. The cell whose number
// is entered draws it through the selected font palette, with an arrow either side where a step
// exists (enteredNumberCanStep), named `rotation-editor-number-left` and `-right`. The cursor stands
// before the cell it is on while the cursor is in the grid, no number is entered and `blinkOn`.
// LevelCellSwatches draws each level's colors under its text.

#include <cstdint>

#include "render/screens/rotation_editor/rotation_editor_state.h"
#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/custom_palettes.h"
#include "state/custom_rotations.h"

namespace kirpich::render {

[[nodiscard]] Sprites LevelCells(const kirpich::RotationEditorState& ui,
                                 const kirpich::CustomRotations&     rotations,
                                 const kirpich::CustomPalettes& palettes, bool blinkOn,
                                 const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
