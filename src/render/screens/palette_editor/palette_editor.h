#pragma once

// The palette editor's logic: what a press does to the player's working palette and to the preset
// slots. It draws nothing and names no drawing type - the picture is a function of the screen's state
// and the player's palettes, built by PaletteEditorScreen (screen.h beside this file).
//
// The screen opens from the Palette screen's Down and returns to it with Up on its first row or with
// B. Its rows, top to bottom:
//
//   preset        Left and Right pick the slot, 1-99, that the three actions below act on.
//   load preset   copies the slot into the working palette, asking first while the working palette
//                 has unsaved changes (hasUnsavedChanges). An empty slot has nothing to load.
//   save preset   writes the working palette into the slot, asking first when that would replace a
//                 different palette.
//   clear preset  empties the slot, asking first. Clearing the slot that holds the palette in use,
//                 with no other slot holding it, puts the game back on the built-in ramp underneath.
//   1 to 4        one row per shade of the working palette, darkest first, each holding its red,
//                 green and blue as values 0-255. Left and Right pick the channel; A enters it.
//
// An action that would change nothing is an end stop: it asks nothing and cues nothing.
//
// While a value is entered, Left and Right pick its place - hundreds, tens or ones - and Up and Down
// step it by that place, clamped to 0-255, repeating while held on the game's own key repeat. Every
// step that changes the value writes the working palette through the wiring's `apply` and then its
// `save`. A keeps the value; B puts back the value it had when it was entered.
//
// Holding Select shows the held preview in every mode - the walk, an entered value, a question - for
// exactly as long as the button is down. While it is up no table runs: no press moves, steps, answers
// or leaves, and nothing is cued. On release the editor is where it was, a question still up and an
// entered value still entered at its place.
//
// Input is one dispatch table per mode - the walk, an entered value, a question - so what a button
// does is data a reader sees in one place. The screen's own state is GameContext::paletteEditor.

#include <cstdint>

#include "render/screens/palette_editor/palette_editor_state.h"
#include "state/custom_palettes.h"
#include "systems/game_context.h"
#include "systems/palette_settings_screen.h"  // CustomPaletteWiring
#include "systems/settings_screen.h"          // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Enter the screen: its state reset, the preset on the slot the working palette is saved in, or on
// slot 1 when it is saved in none or `palettes` is null, the cursor shown and the shared blink armed.
// It writes no background map and no object buffer.
void openPaletteEditor(GameContext& game, const CustomPalettes* palettes);

// Whether the preset selector can move one step in `delta`'s direction (negative is left) from
// `preset`: inside 1-99, and never with no palettes. The screen's step and its arrows both ask this.
[[nodiscard]] bool presetCanStep(std::uint8_t preset, const CustomPalettes* palettes,
                                 int delta) noexcept;

// What a step of `delta` (+1 up, -1 down) at `place` makes of `value`, clamped to 0-255. A result
// equal to `value` is an end stop. The screen's step and its arrows both ask this.
[[nodiscard]] std::uint8_t steppedValue(std::uint8_t value, DigitPlace place, int delta) noexcept;

// Whether `action` - PaletteEditorRow::LOAD, SAVE or CLEAR - would change anything for slot `preset`:
// load while the slot holds a palette other than the working one, save while the slot does not
// already hold the working palette, clear while the slot holds anything.
// Always false with no palettes, and for any other row. The screen's action and its dimmed label both
// ask this.
[[nodiscard]] bool paletteEditorActionCanAct(PaletteEditorRow action, std::uint8_t preset,
                                             const CustomPalettes* palettes) noexcept;

// One frame: blink the cursor and record whether Select is held as PaletteEditorState::previewing.
// While it is held the frame ends there; otherwise the question's table while a question is up, else
// the entered value's frame while a value is entered, else the walk's table.
void paletteEditor(GameContext& game, const SettingsWiring& settings,
                   const CustomPaletteWiring& palettes);

// Install the screen's one handler into the PALETTE_EDITOR slot.
void installPaletteEditor(GameStateDispatcher& dispatcher, SettingsWiring settings,
                          CustomPaletteWiring palettes);

}  // namespace kirpich::systems
