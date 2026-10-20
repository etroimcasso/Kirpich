#pragma once

// The palette editor, as the layers it is.
//
// Two layers: the backdrop, and either the editor or the question that has taken the screen. The
// editor is the up arrow, the heading (with an asterisk while the working palette has unsaved
// changes), the preset selector and its three actions - each dimmed while it can do nothing - and the
// four shade rows with the band of the working palette's colors beside them. While a value is entered
// it is drawn selected, with arrows either side and over and under the digit being stepped, and the
// hint stands at the bottom; otherwise the blinking cursor marks the row.
//
// The screen draws through the built-in ramp the player has underneath, never through the custom
// set, so stepping a value never recolors the editor itself; the band shows the working palette's
// colors directly. Laid out in pixels by layout.h beside this file.

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
