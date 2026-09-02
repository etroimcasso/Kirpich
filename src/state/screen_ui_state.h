#pragma once

// The state the port's own screens keep.
//
// The cartridge has no bytes for any of this, because it has none of these screens: a third item on
// the title menu, a settings screen, and the confirm that guards erasing the top scores. The layout
// fixtures in tests/fixtures/ tile the original's memory byte for byte, so a synthetic field with no
// address cannot go on GameFlowState or EngineState - the same reason DisplayState is its own struct.
//
// Most of it is small. The two large members are a copy of the background map the settings screen
// paints over and a copy of the object buffer it empties, both taken on the way in and put back on
// the way out. That is what lets the screen be opened from a paused round and leave it exactly as it
// was - the paused screen, the hidden piece objects, all of it - without rebuilding any of it.
//
// It is ordinary port state: a cold boot returns it to the values below, and so does the reset
// chord. The player's actual settings do NOT live here - they outlive a reset and are saved to disk,
// so they live in src/state/settings.h and reach the screen through its wiring.

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/game_state.h>

#include "state/demo_state.h"     // ActiveDemo
#include "state/display_state.h"  // BackgroundMap
#include "state/engine_state.h"   // EngineState::oam

namespace kirpich {

// The settings screen's option rows, in the order the cursor walks them.
//
// They span three pages: the window's own choices on the first, and on the two after them the ones
// that change how the game is played and the one that erases the scores. The walk itself is
// continuous - going down past the last row of a page turns to the next one, and up past the first
// row turns back - so a page is where a row is drawn rather than a mode the player has to switch
// between.
//
// The declaration order IS the layout: a page holds kSettingsRowsPerPage consecutive rows, so moving
// a row between pages means moving its enumerator rather than editing a table somewhere else.
enum class SettingsRow : std::uint8_t {
    FULLSCREEN   = 0,
    WINDOW_SCALE = 1,
    SHADE_RAMP   = 2,
    EXIT_GAME    = 3,
    GHOST_PIECE  = 4,
    NEW_MODES    = 5,
    FIXES        = 6,
    STATS        = 7,
    RESET_SCORES = 8,
};

// How many rows that walk covers. Tied to the last enumerator so the two cannot drift.
inline constexpr std::uint8_t kSettingsRowCount =
    static_cast<std::uint8_t>(SettingsRow::RESET_SCORES) + 1;

// How many rows a page holds, and how many pages that makes. Every page lays out the same way, so
// the count is one number rather than a split point per page; the last page carries the remainder.
inline constexpr std::uint8_t kSettingsRowsPerPage = 4;
inline constexpr std::uint8_t kSettingsPageCount =
    static_cast<std::uint8_t>((kSettingsRowCount + kSettingsRowsPerPage - 1) /
                              kSettingsRowsPerPage);

// Which page a row is drawn on, and where it sits within that page.
[[nodiscard]] constexpr std::uint8_t settingsPageOf(SettingsRow row) noexcept {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(row) / kSettingsRowsPerPage);
}
[[nodiscard]] constexpr std::uint8_t settingsRowWithinPage(SettingsRow row) noexcept {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(row) % kSettingsRowsPerPage);
}

// How many screens deep the stacking screens can go before a push is refused.
//
// The deepest path the game has is four - the title screen, the chooser it opens, the game type and
// the combination - so six is headroom rather than a limit anything reaches. It is declared here
// because it is the dimension of the array below; systems/screen_stack.h, which pushes and pops,
// reads it from here for the same reason.
inline constexpr std::size_t kScreenStackDepth = 6;

// What the confirm screen is currently guarding. Both of its actions are ones a player cannot undo,
// which is why neither happens without it.
enum class ConfirmAction : std::uint8_t {
    ERASE_SCORES = 0,
    EXIT_GAME    = 1,
};

struct ScreenUiState {
    // Whether the title screen's cursor is on the bottom row rather than on the player-count pair.
    // The pair's own choice is MultiplayerState::isMultiplayer, which this does not disturb: moving
    // down to the bottom row and back up leaves the player count where the player left it.
    bool titleSettingsSelected = false;

    // Which of the bottom row's two cells the cursor is on: the stats item when set, the settings
    // item when clear. The row holds two items only while the player has the statistics switched on
    // (Settings::showStats); with one item this is ignored, and moving up and back down leaves it
    // where the player left it, exactly as the player count above it is left.
    bool titleStatsColumn = false;

    // Which option row the settings cursor is on.
    SettingsRow settingsRow = SettingsRow::FULLSCREEN;

    // The blink phase every screen the port draws itself shares: the settings screen's cursor, the
    // confirm's, and any screen either of them opens. One flag serves all of them because only one is
    // ever on screen, and they all count the same frame timer.
    bool cursorVisible = true;

    // Which of the config screen's third-section choices is current: the right-hand one when set.
    // The section is a port surface with no cartridge byte behind it, so its choice lives here with
    // the rest of the port's own screen state.
    bool modeOptionRight = false;

    // Which option a carousel screen is showing (systems/carousel_screen.h). One field serves every
    // carousel instance for the same reason one blink flag serves every screen: only one is ever on
    // screen, and each instance's init starts it back at the first option.
    std::uint8_t carouselOption = 0;

    // The confirm's choice, and which of the two actions it is currently guarding. It opens on "no"
    // every time, so a player who reaches it by accident leaves it by pressing anything that acts.
    // Which of the confirm screen's two answers the cursor is on. Not "yes": the answers are the
    // caller's to name, and one confirm offers "no"/"yes" while another offers two destinations
    // neither of which is a refusal.
    bool          confirmRight   = false;
    ConfirmAction pendingConfirm = ConfirmAction::ERASE_SCORES;

    // Which row a list screen's cursor is on, the first row its window shows, and how many rows the
    // list held when it was last painted (systems/list_screen.h). Three fields serve every list
    // instance for the same reason one serves every carousel: only one list is ever on screen, and
    // each instance's init starts them at zero.
    //
    // The count is here because the two end indicators are drawn by the render bridge, which has no
    // way to ask an instance how long its list is; the screen records what it drew.
    std::uint8_t listRow   = 0;
    std::uint8_t listTop   = 0;
    std::uint8_t listCount = 0;

    // Where the stacking screens came from, deepest last, and how many of them are on it.
    //
    // The screens the settings screen opens hard-code their parent, which works because each has
    // exactly one. The screens reached from the title screen's stats item are four deep and share
    // their machinery, so each one records where it was entered from and comes back to it -
    // systems/screen_stack.h holds the two calls.
    //
    // An array and a count rather than a BoundedVec: that type is construct-only by design, and a
    // stack has to be pushed and popped.
    std::array<GameState, kScreenStackDepth> screenStack{};
    std::uint8_t                             screenStackDepth = 0;

    // The state to return to when the player leaves: the title screen, or the paused round the
    // screen was opened from.
    GameState settingsReturn = GameState::TITLE_SCREEN;

    // What the caller's frame timer held. The settings cursor blinks on that timer, so it is put
    // back rather than left wherever the blink happened to stop.
    std::uint8_t savedTimer1 = 0;

    // The caller's screen, taken on the way in and put back on the way out. The map is whichever one
    // the display was reading - the first at the title screen, the second in a paused round - so the
    // screen never paints over a map something else is still writing.
    BackgroundMap              savedMap{};
    decltype(EngineState::oam) savedOam{};

    // The tile art the caller was drawing through. The settings screen selects the copyright-and-title
    // set while it is up, because that is the set the game's own selector arrow lives in — the same
    // index is a solid block under the gameplay art. Its text is unaffected either way: the font and
    // the empty cell mean the same picture under both sets.
    TileSheet savedSheet = TileSheet::COPYRIGHT_TITLE;

    // Which demo the caller had running, taken on the way in and put back on the way out.
    //
    // The sound driver reads this byte and blanks all four cue mailboxes before playing while it is
    // non-zero, so a demo's recorded button presses make no sound (audio.asm:73-80). Nothing clears
    // it when a demo ends - only starting a round does - which is why the cartridge's title music
    // does not come back after an attract demo. That suppression is the cartridge's, and it covers
    // the cartridge's screens. This one is the port's own and is not a demo, so the gate is lifted
    // while it is up: without that, every sound the screen makes is swallowed for the rest of a
    // session in which a demo has played once.
    //
    // Lifting it changes nothing else. The three gameplay paths that read the demo number
    // (systems/piece.cpp, systems/gameplay.cpp) do not run while this screen holds the dispatcher,
    // and the two exits that do not restore it are right not to: leaving the program needs no state,
    // and returning to the title runs a soft reset, which boots the demo state along with the rest.
    ActiveDemo savedActiveDemo = ActiveDemo::NONE;

    // Return every field to its boot value.
    void reset() { *this = ScreenUiState{}; }

    friend bool operator==(const ScreenUiState&, const ScreenUiState&) = default;
};

}  // namespace kirpich
