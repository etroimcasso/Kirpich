#pragma once

// The palette editor, as the layers it is.
//
// Two layers: the backdrop, and either the held preview, the question that has taken the screen, or
// the editor. The
// editor is the up arrow, the heading (with an asterisk while the working palette has unsaved
// changes), the preset selector and its three actions - each dimmed while it can do nothing - and the
// four shade rows with the band of the working palette's colors beside them. While a value is entered
// it is drawn selected, with arrows either side and over and under the digit being stepped, and the
// hint stands at the bottom; otherwise the blinking cursor marks the row.
//
// The editor draws through the built-in ramp the player has underneath, never through the custom
// set, so stepping a value never recolors the editor itself; the band shows the working palette's
// colors directly.
//
// While Select is held (PaletteEditorState::previewing) the held preview takes the screen from the
// editor and from any question: the backdrop with a brick wall down each edge (PreviewBackdrop), and a
// layer keyed `editor-preview` holding the heading, the words `selected`, `unselected` and `dimmed`
// in their three inks, and the seven pieces (PalettePreview). Every part of it draws through the
// custom set, kCustomRamp, so the player sees the palette as the game will use it. Laid out in pixels
// by layout.h beside this file.

#include <retropp/draw_state.h>

#include "render/screens/palette_editor/palette_editor_state.h"
#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/custom_palettes.h"
#include "state/settings.h"

namespace kirpich::render {

[[nodiscard]] Layers PaletteEditorScreen(const kirpich::PaletteEditorState& ui,
                                         const kirpich::Settings&           s,
                                         const kirpich::CustomPalettes&     palettes,
                                         bool blinkOn, const TileAtlas& atlas);

}  // namespace kirpich::render
