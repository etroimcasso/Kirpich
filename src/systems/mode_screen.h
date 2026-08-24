#pragma once

// A settings screen of a mode's own: the screen a settings row opens when a mode needs more than a
// value in a field.
//
// The settings screen's rows hold one value each, in three cells. A mode that wants to explain itself
// does not fit there — so it gets a row that opens a screen instead, and this is that screen. It
// carries a title, one enable row in the settings screen's own scroller geometry, eleven rows of
// prose, and an optional drawing area over them. B goes back to the settings screen.
//
// It owns no state. The flag the enable row toggles belongs to the caller — it is a saved setting,
// which outlives a launch — and reaches the handlers through the wiring below. The blink is the one
// every port screen shares (ScreenUiState::cursorVisible).
//
// The extra game types use it: `SettingsRow::NEW_MODES` opens it, and the flag it turns on is
// `Settings::newModes`, which the config screen reads to decide whether to offer them.
//
// The screen is generic, so a second mode can have one of its own. What that takes: a row in
// SettingsRow, a label in labelFor, a reach in reachOf (an arrow toward the screen rather than a
// value), an early return in changeValue since it is an action, a branch in settingsScreen that
// writes INIT_MODE_SCREEN, and a call to installModeScreenHandlers with the mode's own content and
// flag.
//
// Every glyph it draws comes from the font (tile indices $00-$26), which means the same picture under
// either tile regime — so the screen reads correctly whether it was opened from the title screen or
// from a paused round, as the settings screen behind it does.

#include <functional>
#include <span>
#include <string_view>

#include "state/display_state.h"      // BackgroundMap
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// What the screen says. The title is centred at the top; the body runs down the rows below the enable
// row, one line per row, each centred.
//
// The caller wraps its own text: a line wider than the twenty-cell screen is clipped, and there is no
// word wrapping here. An empty line is a blank row, which is how paragraphs are separated. Lines past
// the bottom of the screen are not drawn.
//
// The font has letters, digits, a period and a hyphen. It has no comma and no apostrophe, so prose
// written for this screen has to do without them.
struct ModeScreenContent {
    std::string_view            title;
    std::span<const std::string_view> body;
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

// Where the screen's parts sit. The body starts below the enable row and runs to the bottom of the
// screen, so a mode has eleven rows to explain itself in.
inline constexpr std::size_t kModeScreenTitleRow    = 2;
inline constexpr std::size_t kModeScreenBodyFirstRow = 7;
inline constexpr std::size_t kModeScreenBodyLastRow  = 17;
inline constexpr std::size_t kModeScreenBodyRows =
    kModeScreenBodyLastRow - kModeScreenBodyFirstRow + 1;

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
