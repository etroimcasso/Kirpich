#pragma once

// A settings screen of a mode's own: the screen a settings row opens when a mode needs more than a
// value in a field.
//
// The settings screen's rows hold one value each, in three cells. A mode that wants a sentence about
// itself, or a picture of what it does, does not fit there — so it gets a row that opens a screen
// instead, and this is that screen. It carries a title, one enable row in the settings screen's own
// scroller geometry, two lines of description, and an area the caller draws whatever it likes into.
// B goes back to the settings screen.
//
// It owns no state. The flag the enable row toggles belongs to the caller — it is a saved setting,
// which outlives a launch — and reaches the handlers through the wiring below. The blink is the one
// every port screen shares (ScreenUiState::cursorVisible).
//
// NOTHING INSTALLS THESE TODAY. The screen is compiled and inert: no caller installs the handlers, so
// nothing writes INIT_MODE_SCREEN and neither state is reachable. It is machinery, not a feature.
//
// A mode that wants it installs the handlers with its own title, its own flag and its own preview,
// and adds the settings row that opens it — a row in SettingsRow, a label in labelFor, {} from
// reachOf and an early return in changeValue (it is an action, not a value), and a branch in
// settingsScreen's Confirm/Start that writes INIT_MODE_SCREEN. The config screen's matching third
// section is the other half; see systems/menu_screens.h.
//
// Every glyph it draws comes from the font (tile indices $00-$26), which means the same picture under
// either tile regime — so the screen reads correctly whether it was opened from the title screen or
// from a paused round, as the settings screen behind it does.

#include <functional>
#include <string_view>

#include "state/display_state.h"      // BackgroundMap
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// What the screen says. The title is centred; the two description lines are centred under the enable
// row. Text wider than the twenty-cell screen is clipped, so a caller wraps it itself.
struct ModeScreenContent {
    std::string_view title;
    std::string_view firstLine;
    std::string_view secondLine;
};

// Everything the screen needs from outside the game state.
//
// `enabled` is the flag the enable row turns on and off — the caller owns it, because it is saved.
// `changed` fires on every change, which is where a caller applies and writes it out. `preview` draws
// into the map below the description; it is optional, and a mode with nothing to show omits it.
struct ModeScreenWiring {
    ModeScreenContent                   content{};
    bool*                               enabled = nullptr;
    std::function<void()>               changed;
    std::function<void(BackgroundMap&)> preview;
};

// Where the screen's parts sit, so a caller's preview knows what rows are left for it.
inline constexpr std::size_t kModeScreenTitleRow       = 2;
inline constexpr std::size_t kModeScreenFirstLine      = 7;
inline constexpr std::size_t kModeScreenSecondLine     = 8;
inline constexpr std::size_t kModeScreenPreviewFirstRow = 10;
inline constexpr std::size_t kModeScreenPreviewLastRow  = 17;

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// INIT_MODE_SCREEN — paint the screen over the map the settings screen is on, place the enable row's
// arrows, and enter MODE_SCREEN.
//
// It does NOT save the caller's screen. The settings screen already holds the picture this one is
// covering, and saving again here would store this screen as what a player returns to.
void initModeScreen(GameContext& game, const ModeScreenWiring& wiring);

// MODE_SCREEN — one frame: blink the cursor, turn the mode on or off, or go back to the settings
// screen. A change fires `changed` as it is made, the way every settings row does.
void modeScreen(GameContext& game, const ModeScreenWiring& wiring, const SettingsWiring& settings);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install the two handlers into their dispatch slots. The settings wiring is needed because leaving
// this screen repaints the one that opened it.
void installModeScreenHandlers(GameStateDispatcher& dispatcher, ModeScreenWiring wiring,
                               SettingsWiring settings);

}  // namespace kirpich::systems
