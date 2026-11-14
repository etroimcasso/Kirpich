#pragma once

// The rotation editor's logic: what a press does to the player's working rotation and to the rotation
// slots. It draws nothing and names no drawing type - the picture is a function of the screen's state,
// the player's rotations and their custom palettes, built by RotationEditorScreen (screen.h beside this
// file).
//
// The screen opens from the rotation screen's `edit rotation` item and returns to it with B, the
// cursor back on that item. Its rows, top to bottom:
//
//   preset        Left and Right pick what the three actions below act on: the shipped rotations
//                 `hotter`, `colder` and `rainbow`, then slots 1-99. A slot reads its name, or its
//                 number while it has none, and is dimmed while empty.
//   load preset   copies the preset into the working rotation, asking first while the working
//                 rotation has unsaved changes (hasUnsavedChanges). Loading a shipped rotation is how
//                 a preset seeds a rotation of the player's own.
//   save preset   writes the working rotation into the slot under a name, asking first when that
//                 would replace a different rotation; then the name screen takes the screen. A slot
//                 already holding the working rotation can be saved again, which renames it. A
//                 shipped rotation cannot be saved over.
//   clear preset  empties the slot, name and all, asking first. Clearing the slot that holds the
//                 rotation in use, with no other slot holding it, turns the rotation off. A shipped
//                 rotation cannot be cleared.
//   the grid      the ten levels, two columns of five, each cell reading its level and the number of
//                 the palette that level wears. Up and Down walk a column, Left and Right cross to the
//                 other one, and A enters the cell's number.
//
// An action that would change nothing is an end stop: it asks nothing and cues nothing.
//
// While a number is entered, Left and Right step it one stop along the palette walk
// (systems/palette_walk.h) - the eighty ramps, then the player's saved palettes - and Up and Down ten
// stops, repeating while held on the game's own key repeat. A ten-step clamps: it runs as far as the
// walk goes and stops at its end. Every step copies that stop's colors into the level and writes the
// rotations out through the wiring's `save`. A keeps the number; B puts back the palette the level wore
// when it was entered.
//
// The name screen takes its characters as the top-score name entry does: Up and Down turn the
// character at the place on a wheel of `a` to `z`, `.`, `-`, `0` to `9` and the space, repeating while
// held; A moves to the next place and finishes on the seventh; Start finishes from any place; B moves
// back a place, and on the first place cancels the save. It opens holding the slot's name, or its
// number. Finishing is the save - nothing is written before it - and a name left as seven spaces is
// stored as the slot's number.
//
// Holding Select shows the held preview for exactly as long as the button is down: the palette
// editor's preview through the working rotation's palette for the selected level, with the Type A
// level box showing which level it is. While it is up in the walk, Up and Right step the level forward
// and Down and Left step it back, 0-9, and the cursor follows; while a number is entered, the name
// screen is up or a question is up, no press does anything until Select is released.
//
// Input is one dispatch table per mode - the walk, an entered number, the name screen, a question,
// the held preview - so what a button does is data a reader sees in one place. The screen's own
// state is GameContext::rotationEditor.

#include <optional>
#include <string>

#include "render/screens/palette_rotation/rotation_screen.h"  // RotationWiring
#include "render/screens/rotation_editor/rotation_editor_state.h"
#include "state/custom_palettes.h"
#include "state/custom_rotations.h"
#include "systems/game_context.h"
#include "systems/palette_walk.h"   // PaletteChoice
#include "systems/rotation_walk.h"  // RotationChoice
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Enter the screen: its state reset, the preset on the slot the working rotation is saved in, or on
// slot 1 when it is saved in none or `rotations` is null, the cursor shown and the shared blink armed.
// It writes no background map and no object buffer.
void openRotationEditor(GameContext& game, const CustomRotations* rotations);

// Whether the preset selector has a stop one step in `delta`'s direction (negative is left) from
// `preset`. The stops run `hotter`, `colder`, `rainbow`, then slots 1-99: Left from `hotter` and
// Right from slot 99 are end stops. Always false with no rotations. The screen's step and its arrows
// both ask this.
[[nodiscard]] bool rotationEditorPresetCanStep(RotationChoice preset,
                                               const CustomRotations* rotations,
                                               int delta) noexcept;

// Whether `action` - RotationEditorRow::LOAD, SAVE or CLEAR - has anything to do for `preset`:
// load while the shipped rotation's table, or the present slot's rotation, differs from the working
// rotation; save for every slot and never for a shipped rotation; clear while the slot holds
// anything. Always false with no rotations, and for any other row. The screen's action and its
// dimmed label both ask this.
[[nodiscard]] bool rotationEditorActionCanAct(RotationEditorRow action, RotationChoice preset,
                                              const CustomRotations* rotations) noexcept;

// What the preset selector reads for `preset`: a shipped rotation's name; a slot's name where it has
// one; otherwise the slot's number, `1` to `99`. The selector and the name screen's opening text both
// ask this.
[[nodiscard]] std::string rotationPresetText(RotationChoice preset,
                                             const CustomRotations& rotations);

// The character one turn of the name wheel from `current`: `delta` positive turns up - `a` to `z`,
// `.`, `-`, `0` to `9`, the space, then `a` again - and negative turns back down the same way. A
// character the wheel does not hold turns as the space does.
[[nodiscard]] char turnedNameWheel(char current, int delta) noexcept;

// The stop a level's number reads: `ui.entered` for the cell whose number is being entered, and for
// every other cell the stop whose colors the level wears (paletteChoiceOfColors), or nothing when
// they match none.
[[nodiscard]] std::optional<PaletteChoice> levelPaletteChoice(const RotationEditorState&  ui,
                                                              const CustomRotations&      rotations,
                                                              const CustomPalettes*       palettes,
                                                              std::size_t level) noexcept;

// Whether the entered number has a stop one step in `delta`'s direction on the palette walk. False
// while no number is entered. The screen's step and its arrows both ask this.
[[nodiscard]] bool enteredNumberCanStep(const RotationEditorState& ui, const CustomPalettes* palettes,
                                        int delta) noexcept;

// One frame: blink the cursor and record whether Select is held as RotationEditorState::previewing.
// While it is held the held preview's table runs in the walk and nothing runs otherwise; while it is
// not, the name screen's frame while it is up, else the question's table while a question is up,
// else the entered number's frame while a number is entered, else the walk's table.
void rotationEditor(GameContext& game, const SettingsWiring& settings,
                    const RotationWiring& rotations, const CustomPalettes* palettes);

// Install the screen's one handler into the ROTATION_EDITOR slot. `palettes` is read for the walk an
// entered number steps along and never written; it must outlive the dispatcher.
void installRotationEditor(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           RotationWiring rotations, const CustomPalettes* palettes);

}  // namespace kirpich::systems
