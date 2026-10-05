#pragma once

// The Palette settings screen's logic: what a press does to the palette the game draws in. It draws
// nothing and names no drawing type - the picture is a function of the settings, built by the
// components under src/render/palette_settings/.
//
// The screen opens from the settings screen's Palette row and holds one row, the palette selector.
// Left and Right step through the palettes, stopping at the first and the last; B goes back to the
// settings screen with its cursor still on the Palette row. The screen keeps no state of its own: the
// row is the only one, and the palette is the player's setting.
//
// Input is a dispatch table mapping an action to what it does, so what a button does is data a reader
// sees in one place.

#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Enter the screen with the cursor shown and the shared blink armed. It writes no background map,
// saves no caller and empties no object buffer - the picture is the screen's components, and the
// settings screen it was opened from repaints itself when the player comes back.
void openPaletteSettings(GameContext& game);

// One frame: blink the cursor, then run the input table. MenuRight steps to the next palette and
// MenuLeft to the previous one, through changeSettings - so a press past either end is an end stop.
// Back returns to the settings screen.
void paletteSettingsScreen(GameContext& game, const SettingsWiring& wiring);

// Install the screen's one handler into the PALETTE_SETTINGS slot.
void installPaletteSettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring);

}  // namespace kirpich::systems
