#pragma once

// What the Controls screen is currently showing: which cell the cursor is on, and whether the screen is
// waiting for a press.
//
// This is the screen's own state, not the game's or the player's. What the player has bound lives in
// state/controls.h and outlives a reset; where the cursor sits and whether the screen is listening do
// not. It is a GameContext member of its own, because the screen owns its whole state space in one
// place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below, and
// opening the screen does the same.

#include <cstddef>
#include <cstdint>

#include "state/controls.h"  // GbButton

namespace kirpich {

// The screen's two columns, left to right.
enum class ControlsColumn : std::uint8_t {
    KEYBOARD   = 0,
    CONTROLLER = 1,
};

// How many columns the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kControlsColumnCount =
    static_cast<std::size_t>(ControlsColumn::CONTROLLER) + 1;

struct ControlsScreenState {
    // The cell the cursor is on: a Game Boy button's row, and the column for its key or its
    // controller button.
    GbButton       row    = GbButton::UP;
    ControlsColumn column = ControlsColumn::KEYBOARD;

    // The screen is waiting for a press to bind to the cell. While it waits it reads nothing but the
    // engine's captured press.
    bool listening = false;

    // Listening has just ended, and the screen ignores input until nothing is held. A binding hands
    // the engine a new action map, so a key the player is still holding can mean a different action
    // on the next frame than on this one - and the game reads a held action it did not have last
    // frame as a fresh press.
    bool awaitingRelease = false;

    void reset() { *this = ControlsScreenState{}; }

    friend constexpr bool operator==(const ControlsScreenState&,
                                     const ControlsScreenState&) = default;
};

}  // namespace kirpich
