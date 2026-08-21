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

#include <cstdint>

#include <kirpich/game_state.h>

#include "state/display_state.h"  // BackgroundMap
#include "state/engine_state.h"   // EngineState::oam

namespace kirpich {

// The settings screen's option rows, in the order the cursor walks them.
enum class SettingsRow : std::uint8_t {
    FULLSCREEN   = 0,
    WINDOW_SCALE = 1,
    RESET_SCORES = 2,
};

// How many rows that walk covers. Tied to the last enumerator so the two cannot drift.
inline constexpr std::uint8_t kSettingsRowCount =
    static_cast<std::uint8_t>(SettingsRow::RESET_SCORES) + 1;

struct ScreenUiState {
    // Whether the title screen's cursor is on the settings item rather than the player-count pair.
    // The pair's own choice is MultiplayerState::isMultiplayer, which this does not disturb: moving
    // down to the settings item and back up leaves the player count where the player left it.
    bool titleSettingsSelected = false;

    // Which option row the settings cursor is on, and whether the cursor glyph is currently drawn.
    SettingsRow settingsRow = SettingsRow::FULLSCREEN;
    bool        cursorVisible = true;

    // The reset confirm's choice. It opens on "no" every time, so a player who reaches the screen by
    // accident leaves it by pressing anything that acts.
    bool confirmYes = false;

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

    // Return every field to its boot value.
    void reset() { *this = ScreenUiState{}; }

    friend bool operator==(const ScreenUiState&, const ScreenUiState&) = default;
};

}  // namespace kirpich
