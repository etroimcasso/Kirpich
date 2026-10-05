#pragma once

// The Display settings screen's logic: which of its two rows the cursor is on, and what a press does
// to that and to the player's settings. It draws nothing and names no drawing type - the picture is a
// function of this state and of the settings, built by the components under
// src/render/display_settings/.
//
// The screen opens from the settings screen's Display row and holds the window's two settings:
// fullscreen, and the window size. Up and Down walk the two rows, stopping at either end; Left and
// Right change the value on the row the cursor is on; B goes back to the settings screen with its
// cursor still on the Display row.
//
// Input is a dispatch table mapping an action to what it does, so what a button does is data a reader
// sees in one place.

#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Enter the screen: the cursor on the first row, shown, with the shared blink armed. It writes no
// background map, saves no caller and empties no object buffer - the picture is the screen's
// components, and the settings screen it was opened from repaints itself when the player comes back.
void openDisplaySettings(GameContext& game);

// One frame: blink the cursor, then run the input table. MenuUp and MenuDown move between the rows,
// with a menu-move cue on a move and nothing on an end stop. MenuRight turns fullscreen on or steps
// the size up, MenuLeft does the opposite, through changeSettings - so a press that lands on the value
// already held is an end stop. Back returns to the settings screen.
void displaySettingsScreen(GameContext& game, const SettingsWiring& wiring);

// Install the screen's one handler into the DISPLAY_SETTINGS slot.
void installDisplaySettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring);

}  // namespace kirpich::systems
