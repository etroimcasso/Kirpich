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
// is no colon, no slash and no question mark in the font, which is why a page heading reads
// "settings 1" and the confirm asks its question without one.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "state/achievement_state.h"
#include "state/high_score_state.h"
#include "state/settings.h"
#include "state/stats_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// ── Where the parts of the screen sit ─────────────────────────────────────────────────────────────
//
// The settings screen, the screens its rows open and the render components that draw those screens
// all read their geometry from here, so a row on one of them lines up with a row on any other.
//
// Cells, not pixels: (row, column) in the background map, the same units the rest of the screen uses.
// A component that places sprites multiplies by the cell's eight pixels (src/render/option_row.h).

// An option row's place on its page. Every page lays its rows out the same way, and so does every
// screen the page opens.
inline constexpr std::size_t kSettingsFirstRow  = 5;
inline constexpr std::size_t kSettingsRowStride = 3;

[[nodiscard]] constexpr std::size_t settingsRowLine(SettingsRow row) noexcept {
    return kSettingsFirstRow + kSettingsRowStride * settingsRowWithinPage(row);
}

// The label starts on column 3, and the cursor stands on column 1, two cells to its left.
inline constexpr std::size_t kLabelCol  = 3;
inline constexpr std::size_t kCursorCol = 1;

// Every row that holds a choice is a scroller: an arrow, the value, an arrow. One geometry for all of
// them, so the arrows line up down the screen instead of each row placing its own.
//
// The value starts at the field's first cell and runs right, so every value begins in the same column
// and the rows read as one list - "off", "on", "4x" and a palette number all start where "off" starts.
// A value shorter than the field leaves the cells after it empty.
//
// The arrows sit at fixed columns rather than beside the text, so they stay put as a value changes
// width. A row whose value cannot go further that way simply has no arrow on that side. The left arrow
// clears the longest label a scroller row carries: "fullscreen" is ten cells from column 3 and so ends
// on column 12.
inline constexpr std::size_t kOptionLeftArrowCol  = 13;
inline constexpr std::size_t kOptionValueCol      = 15;
inline constexpr std::size_t kOptionValueWidth    = 3;  // "off" is the widest value a row carries
inline constexpr std::size_t kOptionRightArrowCol = 19;

// The last cell of the value field. A value never runs past it.
inline constexpr std::size_t kOptionValueEnd = kOptionValueCol + kOptionValueWidth - 1;

// Every screen the settings screen owns or opens puts its heading on the same row, and the screens
// that open from it match, so they read as siblings.
inline constexpr std::size_t kScreenTitleRow = 2;

// The arrows that say there is another screen in a direction: one above the heading, one below the
// body. Centred, so they belong to the screen rather than to a row.
//
// The up arrow sits ABOVE the heading, and that is the whole point of it. An arrow drawn over a
// screen's text says the TEXT moves; an arrow above the heading says the SCREEN does. It is derived
// from the heading's row rather than written as a number of its own, so a screen cannot end up
// drawing one under its heading without moving the heading itself.
inline constexpr std::size_t kPageArrowCol     = 10;
inline constexpr std::size_t kPageUpArrowRow   = kScreenTitleRow - 1;
inline constexpr std::size_t kPageDownArrowRow = 16;

// A confirm: the question that guards a row whose effect cannot be taken back. Its title stands on the
// heading row, its question on two lines below - two because the font has no question mark and
// "erase all high scores" is one cell wider than the screen - and its two answers on one line, each
// with a cursor two cells before it. Every confirm in the family is laid out from these, so every
// question reads the same.
inline constexpr std::size_t kConfirmQuestionFirstRow  = 5;
inline constexpr std::size_t kConfirmQuestionSecondRow = 7;
inline constexpr std::size_t kConfirmChoiceRow         = 11;
inline constexpr std::size_t kConfirmCursorGap         = 2;  // cursor to the word it points at
inline constexpr std::size_t kConfirmChoiceGap         = 2;  // one answer to the next one's cursor
inline constexpr std::size_t kConfirmScreenCols        = 20;

// Where a confirm's two answers start.
struct ConfirmChoiceColumns {
    std::size_t left;
    std::size_t right;
};

// The pair is centered as a block - cursor, word, gap, cursor, word - rather than nailed to fixed
// columns, so a pair of long answers still fits the screen. For "no" and "yes" it lands on columns 6
// and 12; each answer's cursor stands kConfirmCursorGap cells before it.
[[nodiscard]] constexpr ConfirmChoiceColumns confirmChoiceColumns(std::size_t leftLength,
                                                                  std::size_t rightLength) noexcept {
    const std::size_t block =
        kConfirmCursorGap + leftLength + kConfirmChoiceGap + kConfirmCursorGap + rightLength;
    const std::size_t start = block >= kConfirmScreenCols ? 0 : (kConfirmScreenCols - block) / 2;
    const std::size_t left  = start + kConfirmCursorGap;
    return {left, left + leftLength + kConfirmChoiceGap + kConfirmCursorGap};
}

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

    // The other two records a reset row can clear. Each is persisted in its own document, so clearing
    // one writes only that one and leaves the others as they are - which is the whole reason the rows
    // are separate.
    std::function<void(const StatsState&)>       saveStats;
    std::function<void(const AchievementState&)> saveAchievements;

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

// The interval the cursor holds for between toggles, in frames - the one the game's own selection
// screens blink on. A screen arms the frame timer with it as it opens.
inline constexpr std::uint8_t kScreenBlinkFrames = 16;

// Hold the cursor while the frame timer counts, then toggle it and reload kScreenBlinkFrames. The
// dispatcher decrements the timer after the handler runs.
//
// One blink for every screen the port draws itself, because they all count the same timer and only
// one of them is ever on screen (see ScreenUiState::cursorVisible).
void blinkScreenCursor(GameContext& game);

// Put a changed set of settings into effect: store it in `*wiring.settings`, cue the menu move, then
// fire `apply` and `save` in that order. A change that lands on the settings already held is an end
// stop - nothing is written, nothing is cued, and neither seam fires - and so is a wiring with no
// settings behind it. Returns whether anything changed.
bool changeSettings(GameContext& game, const SettingsWiring& wiring, const Settings& next);

// Repaint the settings screen and hand control back to it, the cursor still on the row that opened the
// screen being left. Used by the screens it opens — the confirm, the Display, Palette and Controls
// screens, the carousels and the mode screen — because re-entering the init would save their own picture as the caller's
// screen and lose the real one.
void returnToSettings(GameContext& game, const SettingsWiring& wiring);

// Take the caller's whole screen, and put it back.
//
// A port screen paints over whatever was on the display, so the picture underneath is saved on the
// way in and restored on the way out rather than rebuilt: that is what lets a paused round come back
// with its paused screen, its hidden piece objects and its music exactly as they were, and what lets
// the title screen come back without running the init that would clear the round it has just played.
//
// Saving also does the three things every port screen needs done before it draws: it empties the
// object buffer, it selects the copyright-and-title art (the set the game's own selector arrow is a
// tile of), and it lifts the sound driver's demo gate for as long as the screen is up. See the note
// on ScreenUiState::savedActiveDemo for why the gate has to be lifted at all.
//
// Restoring puts every one of them back. It does not choose a state to hand control to — that is the
// caller's, because the screens that use this leave in different directions.
//
// One saved picture serves every port screen, because only one of them is ever on the display. A
// screen opened on top of another does NOT save again: the picture it is covering belongs to the
// screen it was opened from, and saving it would store that screen as what the player returns to.
void saveCallerScreen(GameContext& game);
void restoreCallerScreen(GameContext& game);

// ── Opening ───────────────────────────────────────────────────────────────────────────────────────

// Remember the current state as the one to return to, and enter the settings screen. Called by the
// title screen's third item and by A in a paused round; both leave the caller's screen untouched,
// because the init below is what saves and repaints it.
void openSettings(GameContext& game);

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// INIT_SETTINGS — save the caller's screen and object buffer, empty the buffer, and paint the
// settings screen over the map the display is reading. Enters SETTINGS.
void initSettingsScreen(GameContext& game, const SettingsWiring& wiring);

// SETTINGS — one frame of the screen: blink the cursor, walk it through the rows across the pages,
// open the screen a row points into (Right, Confirm or Start), open the confirm from a row that
// erases something or ends the game, or leave. No row holds a value of its own; the values live on
// the screens the rows open.
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
