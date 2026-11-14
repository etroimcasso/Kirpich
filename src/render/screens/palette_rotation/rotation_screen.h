#pragma once

// The rotation screen's logic: what a press does to the rotation a round's palette follows. It draws
// nothing and names no drawing type - the picture is a function of the screen's state, the settings
// and the player's rotations, built by PaletteRotationScreen (screen.h beside this file).
//
// The screen opens from the Palette screen's Up and returns to it with Down on its last row or with
// B. Its rows, top to bottom:
//
//   edit rotation   the item that opens the rotation editor. It is drawn dimmed and does nothing.
//   rotation        Left and Right step Settings::paletteRotation along the rotation walk
//                   (systems/rotation_walk.h): `off`, the three presets, then every slot a rotation has
//                   been saved into. Stepping onto a slot copies it into the working rotation, writes
//                   the rotations out through the wiring's `save`, and sets the mode to CUSTOM.
//
// A press that would change nothing is an end stop: it cues nothing.
//
// Holding Select shows the held preview for exactly as long as the button is down: the palette
// editor's preview through one level's palette, with that level's number below it. While it is up,
// Up and Right step the level forward and Down and Left step it back, 0-9, and nothing else does
// anything - no press moves the cursor, steps the rotation or leaves. On release the menu is where it
// was, and the level keeps its value for the next hold until the screen is left.
//
// Input is one dispatch table per mode - the menu and the held preview - so what a button does is
// data a reader sees in one place. The screen's own state is GameContext::rotationScreen.

#include <functional>

#include "render/screens/palette_rotation/rotation_screen_state.h"
#include "state/custom_rotations.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// What the rotation screen needs from outside the game state.
//
// `rotations` is the live value - the host owns it, because it outlives a reset and is saved to disk.
// `save` writes the document out; it fires when stepping onto a slot copies it into the working
// rotation. Both default to inert, and with no `rotations` behind the wiring the selector offers the
// presets alone.
struct RotationWiring {
    CustomRotations*                            rotations = nullptr;
    std::function<void(const CustomRotations&)> save;
};

// Enter the screen: its state reset - the cursor on the selector, the held preview down, its level
// 0 - the cursor shown and the shared blink armed. It writes no background map and no object buffer.
void openRotationScreen(GameContext& game);

// Whether the selector can move one step in `delta`'s direction (negative is left) from where the
// settings stand on the rotation walk, over the present slots of `rotations` (none when it is null).
// The screen's step and its arrows both ask this, so an arrow stands exactly where a press would move.
[[nodiscard]] bool rotationCanStep(const Settings& settings, const CustomRotations* rotations,
                                   int delta) noexcept;

// One frame: blink the cursor and record whether Select is held as RotationScreenState::previewing.
// While it is held the held preview's table runs and nothing else; otherwise the menu's table.
void rotationScreen(GameContext& game, const SettingsWiring& settings,
                    const RotationWiring& rotations);

// Install the screen's one handler into the PALETTE_ROTATION slot.
void installRotationScreen(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           RotationWiring rotations);

}  // namespace kirpich::systems
