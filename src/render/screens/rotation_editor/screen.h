#pragma once

// The rotation editor, as the layers it is.
//
// Two layers: the backdrop, and either the held preview, the name screen, the question that has taken
// the screen, or the editor.
//
// The editor is the heading (with an asterisk while the working rotation has unsaved changes); the
// preset selector - its label, its value (a shipped rotation's name, a slot's name, or a slot's number
// while it has none, dimmed while the slot is empty) and an arrow either side where a step exists; its
// three actions, each dimmed while it can do nothing; and the grid of the ten levels, two columns of
// five, each cell its level's digit, a colon and the number of the palette that level wears
// (LevelCells), with the level's four colors under it (LevelCellSwatches). An entered number is drawn
// selected with an arrow either side; otherwise the blinking cursor marks the row or the cell.
//
// The editor, the name screen and the questions draw through the palette the player chose,
// paletteIndexOf(s), as every menu does; the swatches show the working rotation's colors directly.
//
// While a save is being named (RotationEditorState::naming) the name screen takes the screen: the
// backdrop and the text input screen (render/components/text_entry.h), titled `save preset`, holding
// the name as typed with its place blinking.
//
// While Select is held (RotationEditorState::previewing) the held preview takes the screen from the
// editor, the name screen and any question: the backdrop with a brick wall down each edge and the
// Type A panel's level box at the top, reading the selected level (LevelPreviewBackdrop), and a layer
// keyed `rotation-editor-preview` holding the palette editor's words and pieces (PalettePreview).
// Every part of it draws through the rotation set, kRotationRamp, which holds the working rotation's
// palette for the selected level while the preview is up (systems/frame_palette.h), whichever
// rotation is switched on. Laid out in pixels by layout.h beside this file.

#include <retropp/draw_state.h>

#include "render/screens/rotation_editor/rotation_editor_state.h"
#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/custom_palettes.h"
#include "state/custom_rotations.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers RotationEditorScreen(const kirpich::RotationEditorState& ui,
                                          const kirpich::Settings&            s,
                                          const kirpich::CustomRotations&     rotations,
                                          const kirpich::CustomPalettes&      palettes,
                                          bool blinkOn, const TileAtlas& atlas);

}  // namespace kirpich::render
