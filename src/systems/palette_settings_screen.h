#pragma once

// The Palette settings screen's logic: what a press does to the palette the game draws in. It draws
// nothing and names no drawing type - the picture is a function of the settings and the player's
// custom palettes, built by the components under src/render/palette_settings/.
//
// The screen opens from the settings screen's Palette row and holds one row, the palette selector.
// Left and Right step through the palettes: the built-in ramps first, then every custom slot a
// palette has been saved into, in slot order, stopping at the first ramp and the last present slot.
// Stepping onto a slot puts its palette into use; stepping back off the last built-in returns to the
// ramp the player left. B goes back to the settings screen with its cursor still on the Palette row.
// The screen keeps no state of its own: the row is the only one, and the palette is the player's
// setting.
//
// Input is a dispatch table mapping an action to what it does, so what a button does is data a reader
// sees in one place.

#include <functional>

#include "render/palettes.h"  // ShadeRamp
#include "state/custom_palettes.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Everything the screens that touch the custom palettes need from outside the game state.
//
// `palettes` is the live value - the host owns it, because it outlives a reset and is saved to disk.
// `apply` gives the atlas's custom set the working palette's colors, and `save` writes the document
// out; both fire whenever the working palette changes, in that order. Every seam defaults to inert,
// and with no `palettes` behind it the selector offers no custom slot at all.
struct CustomPaletteWiring {
    CustomPalettes*                               palettes = nullptr;
    std::function<void(const render::ShadeRamp&)> apply;
    std::function<void(const CustomPalettes&)>    save;
};

// Enter the screen with the cursor shown and the shared blink armed. It writes no background map,
// saves no caller and empties no object buffer - the picture is the screen's components, and the
// settings screen it was opened from repaints itself when the player comes back.
void openPaletteSettings(GameContext& game);

// Whether the selector can move one step in `delta`'s direction (negative is left) from the palette
// the settings name, over the built-in ramps and the present slots of `palettes` (none when it is
// null). The screen's step and its arrows both ask this, so an arrow stands exactly where a press
// would move.
[[nodiscard]] bool paletteCanStep(const Settings& settings, const CustomPalettes* palettes,
                                  int delta) noexcept;

// One frame: blink the cursor, then run the input table. MenuRight steps to the next palette and
// MenuLeft to the previous one, through changeSettings - so a press past either end is an end stop.
// Back returns to the settings screen.
void paletteSettingsScreen(GameContext& game, const SettingsWiring& wiring,
                           const CustomPaletteWiring& palettes);

// Install the screen's one handler into the PALETTE_SETTINGS slot.
void installPaletteSettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring,
                                  CustomPaletteWiring palettes);

}  // namespace kirpich::systems
