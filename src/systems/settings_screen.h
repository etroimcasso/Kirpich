#pragma once

// The settings screen and the confirm that guards erasing the top scores.
//
// Two screens, four game states — an init that paints and a loop that reads input, for each. They
// are free functions on GameContext, the same shape as the menu, title, and gameplay handlers, and
// they own no state of their own: the cursor position and the caller's saved screen live on
// GameContext (state/screen_ui_state.h), and the player's actual settings live outside it and reach
// these functions through the wiring below.
//
// The screen is opened from two places — the title screen's third item, and A in a paused round —
// and returns to whichever one it came from. Opening it saves the background map the display is
// reading and the object buffer; leaving it puts both back, which is what lets a paused round come
// back with its paused screen, its hidden piece objects, and its music exactly as they were.
//
// Every glyph these screens draw comes from the font (tile indices $00-$26) or the empty cell
// ($2F). Those mean the same picture under both tile regimes (src/render/tile_atlas.h), so one
// layout reads correctly whether the screen was opened from the title screen or from a round. There
// is no colon, no slash and no question mark in the font, which is why a row reads FULLSCREEN ON
// and the confirm asks its question without one.

#include <functional>

#include "state/high_score_state.h"
#include "state/settings.h"
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// Everything the settings screens need from outside the game state.
//
// `settings` is the live value the screen edits — the host owns it, because it outlives a reset and
// is saved to disk. `apply` puts a change into effect on the window, and `save` writes it out; both
// fire on every change, so a player who changes something and quits comes back to it. `saveScores`
// persists the cleared tables when the confirm is answered yes. Every seam defaults to inert, so a
// build that installs only the screens still runs.
struct SettingsWiring {
    Settings*                                settings = nullptr;
    std::function<void(const Settings&)>     apply;
    std::function<void(const Settings&)>     save;
    std::function<void(const HighScoreState&)> saveScores;
};

// ── Opening ───────────────────────────────────────────────────────────────────────────────────────

// Remember the current state as the one to return to, and enter the settings screen. Called by the
// title screen's third item and by A in a paused round; both leave the caller's screen untouched,
// because the init below is what saves and repaints it.
void openSettings(GameContext& game);

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// INIT_SETTINGS — save the caller's screen and object buffer, empty the buffer, and paint the
// settings screen over the map the display is reading. Enters SETTINGS.
void initSettingsScreen(GameContext& game, const SettingsWiring& wiring);

// SETTINGS — one frame of the screen: blink the cursor, move it between the three rows, change the
// value on the row it is on, open the confirm from the reset row, or leave.
void settingsScreen(GameContext& game, const SettingsWiring& wiring);

// INIT_RESET_CONFIRM — paint the confirm over the same map, opening on "no". Enters RESET_CONFIRM.
void initResetConfirmScreen(GameContext& game);

// RESET_CONFIRM — one frame of the confirm: blink the cursor, move between no and yes, and act.
// Yes clears both top-score tables and writes the cleared state out; no and Back leave them alone.
// Every path returns to the settings screen.
void resetConfirmScreen(GameContext& game, const SettingsWiring& wiring);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install the four handlers into their dispatch slots.
void installSettingsHandlers(GameStateDispatcher& dispatcher, SettingsWiring wiring);

}  // namespace kirpich::systems
