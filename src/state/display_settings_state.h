#pragma once

// What the Display settings screen is currently showing: which of its rows the cursor is on.
//
// This is the screen's own state, not the game's or the player's. What the player has chosen -
// fullscreen, the window size - lives in state/settings.h and outlives a reset; where the cursor
// happens to sit does not, and has no bearing on what the game is. It is a GameContext member of its
// own rather than another field on ScreenUiState, because the screen owns its whole state space in one
// place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below, and
// opening the screen puts the cursor back on the first row.

#include <cstddef>
#include <cstdint>

namespace kirpich {

// The screen's rows, in the order the cursor walks them.
enum class DisplaySettingsRow : std::uint8_t {
    FULLSCREEN   = 0,
    WINDOW_SCALE = 1,
};

// How many rows the walk covers. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kDisplaySettingsRowCount =
    static_cast<std::size_t>(DisplaySettingsRow::WINDOW_SCALE) + 1;

struct DisplaySettingsState {
    DisplaySettingsRow row = DisplaySettingsRow::FULLSCREEN;

    void reset() { *this = DisplaySettingsState{}; }

    friend constexpr bool operator==(const DisplaySettingsState&,
                                     const DisplaySettingsState&) = default;
};

}  // namespace kirpich
