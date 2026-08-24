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

// ── Where the drawn parts of the screen sit ───────────────────────────────────────────────────────
//
// Some of the screen is shape rather than text: the palette row's two scroll arrows, the preview
// strip of the ramp they scroll through, and the arrow at the edge of a page that says another page
// is there. The render bridge draws those (src/render/settings_overlay.h) and reads their geometry
// from here, so the drawn parts and the written ones cannot drift apart.
//
// Cells, not pixels: (row, column) in the background map, the same units the rest of the screen uses.

// An option row's place on its page. Every page lays its rows out the same way.
inline constexpr std::size_t kSettingsFirstRow  = 5;
inline constexpr std::size_t kSettingsRowStride = 3;

[[nodiscard]] constexpr std::size_t settingsRowLine(SettingsRow row) noexcept {
    return kSettingsFirstRow + kSettingsRowStride * settingsRowWithinPage(row);
}

// Every row that holds a choice is a scroller: an arrow, the value, an arrow. One geometry for all of
// them, so the arrows line up down the screen instead of each row placing its own.
//
// The value starts at the field's first cell and runs right, so every value on the screen begins in
// the same column and the rows read as one list - "off", "on", "4x" and a palette number all start
// where "off" starts. A value shorter than the field leaves the cells after it empty.
//
// The arrows sit at fixed columns rather than beside the text, so they stay put as a value changes
// width. A row whose value cannot go further that way simply has no arrow on that side.
// The left arrow clears the longest label on the screen: "fullscreen" is ten cells from column 3 and
// so ends on column 12.
inline constexpr std::size_t kOptionLeftArrowCol  = 13;
inline constexpr std::size_t kOptionValueCol      = 15;
inline constexpr std::size_t kOptionValueWidth    = 3;  // "off" is the widest value on the screen
inline constexpr std::size_t kOptionRightArrowCol = 19;

// The last cell of the value field. A value never runs past it.
inline constexpr std::size_t kOptionValueEnd = kOptionValueCol + kOptionValueWidth - 1;
inline constexpr std::size_t kPaletteSwatchRow     = settingsRowLine(SettingsRow::SHADE_RAMP) + 1;

// The page arrows: one at the foot of a page that has another below it, one at the head of a page
// that has one above. Centred, so they read as belonging to the page rather than to a row.
inline constexpr std::size_t kPageArrowCol      = 10;
inline constexpr std::size_t kPageDownArrowRow  = 16;
inline constexpr std::size_t kPageUpArrowRow    = 3;

// Everything the settings screens need from outside the game state.
//
// `settings` is the live value the screen edits — the host owns it, because it outlives a reset and
// is saved to disk. `apply` puts a change into effect on the window, and `save` writes it out; both
// fire on every change, so a player who changes something and quits comes back to it. `saveScores`
// persists the cleared tables when the confirm is answered yes. Every seam defaults to inert, so a
// build that installs only the screens still runs.
struct SettingsWiring {
    Settings*                                  settings = nullptr;
    std::function<void(const Settings&)>       apply;
    std::function<void(const Settings&)>       save;
    std::function<void(const HighScoreState&)> saveScores;

    // Ends the run. The confirm calls it once the player has answered yes; what ending the run means
    // is the host's business, and a build without one simply has an Exit row that does nothing.
    std::function<void()> exit;

    // The settings as they stand, or the defaults when the host installed none. Every screen reads
    // them through this, so a build with no settings behind it draws the defaults rather than
    // needing a null check of its own.
    [[nodiscard]] Settings current() const {
        return settings != nullptr ? *settings : Settings{};
    }
};

// ── Shared with the screens the settings screen opens ─────────────────────────────────────────────

// Place one scroller row's two arrows into object entries `entry` and `entry + 1`, at the map row
// `line`. An arrow is placed only where the value can still move that way; the other entry is
// emptied, so the ends of a range are visible rather than something a player finds by pressing.
void placeScrollerArrows(GameContext& game, std::size_t entry, std::size_t line, bool left,
                         bool right);

// Hold the cursor while the frame timer counts, then toggle it and reload the interval the game's own
// selection screens blink on. The dispatcher decrements the timer after the handler runs.
//
// One blink for every screen the port draws itself, because they all count the same timer and only
// one of them is ever on screen (see ScreenUiState::cursorVisible).
void blinkScreenCursor(GameContext& game);

// Repaint the settings screen and hand control back to it. Used by the screens it opens — the
// confirm and the New-mode screen — because re-entering the init would save their own picture as the
// caller's screen and lose the real one.
void returnToSettings(GameContext& game, const SettingsWiring& wiring);

// ── Opening ───────────────────────────────────────────────────────────────────────────────────────

// Remember the current state as the one to return to, and enter the settings screen. Called by the
// title screen's third item and by A in a paused round; both leave the caller's screen untouched,
// because the init below is what saves and repaints it.
void openSettings(GameContext& game);

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// INIT_SETTINGS — save the caller's screen and object buffer, empty the buffer, and paint the
// settings screen over the map the display is reading. Enters SETTINGS.
void initSettingsScreen(GameContext& game, const SettingsWiring& wiring);

// SETTINGS — one frame of the screen: blink the cursor, walk it through the rows across both pages,
// change the value on the row it is on, act on it if it is an action row (the confirm from the two
// that end something, the mode screen from the one that opens a screen), or leave.
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
