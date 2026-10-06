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
#include <optional>

#include "state/controls.h"  // GbButton

namespace kirpich {

// The screen's rows, top to bottom: one per Game Boy button, in GbButton order, then the row that puts
// every binding back to the defaults.
enum class ControlsRow : std::uint8_t {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    A,
    B,
    START,
    SELECT,
    RESTORE_DEFAULTS,
};

// How many rows the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kControlsRowCount =
    static_cast<std::size_t>(ControlsRow::RESTORE_DEFAULTS) + 1;

static_assert(static_cast<std::size_t>(ControlsRow::RESTORE_DEFAULTS) == kGbButtonCount,
              "the button rows are the Game Boy's buttons, in order, and the restore row follows them");

// The row a button stands on.
[[nodiscard]] constexpr ControlsRow rowOf(GbButton button) noexcept {
    return static_cast<ControlsRow>(button);
}

// The button a row stands for, or nothing for the restore row.
[[nodiscard]] constexpr std::optional<GbButton> buttonOf(ControlsRow row) noexcept {
    if (row == ControlsRow::RESTORE_DEFAULTS) {
        return std::nullopt;
    }
    return static_cast<GbButton>(row);
}

// The screen's two columns, left to right. The restore row is one item, so it has no column of its
// own; the column is kept while the cursor is on it, and a button row reached from it uses it again.
enum class ControlsColumn : std::uint8_t {
    KEYBOARD   = 0,
    CONTROLLER = 1,
};

// How many columns the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kControlsColumnCount =
    static_cast<std::size_t>(ControlsColumn::CONTROLLER) + 1;

struct ControlsScreenState {
    // Where the cursor is: a row, and on a button's row the column for its key or its controller
    // button.
    ControlsRow    row    = ControlsRow::UP;
    ControlsColumn column = ControlsColumn::KEYBOARD;

    // The screen is waiting for a press to bind to the cell. While it waits it reads nothing but the
    // engine's captured press.
    bool listening = false;

    // The screen is asking whether to restore the defaults, and `confirmYes` is the answer the cursor is
    // on. The question opens on "no".
    bool confirmingRestore = false;
    bool confirmYes        = false;

    // The bindings have just changed, or a wait has just ended, and the screen ignores input until
    // nothing is held. A change hands the engine a new action map, so a key the player is still
    // holding can mean a different action on the next frame than on this one - and the game reads a
    // held action it did not have last frame as a fresh press.
    bool awaitingRelease = false;

    void reset() { *this = ControlsScreenState{}; }

    friend constexpr bool operator==(const ControlsScreenState&,
                                     const ControlsScreenState&) = default;
};

}  // namespace kirpich
