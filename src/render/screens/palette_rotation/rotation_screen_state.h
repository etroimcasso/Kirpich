#pragma once

// What the rotation screen is currently showing: which of its two rows the cursor is on, whether the
// held preview has taken the screen, and which level the preview shows.
//
// This is the screen's own state, not the player's. The rotation it chooses is a setting
// (Settings::paletteRotation) and the rotations it chooses among live in state/custom_rotations.h;
// both outlive a reset, and where the cursor sits does not. It is a GameContext member of its own,
// because the screen owns its whole state space in one place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below, and
// opening the screen does the same.

#include <cstddef>
#include <cstdint>

namespace kirpich {

// The screen's two menu rows, top to bottom: the item that opens the rotation editor, then the
// rotation selector.
enum class RotationScreenRow : std::uint8_t {
    EDITOR,
    SELECTOR,
};

// How many rows the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kRotationScreenRowCount =
    static_cast<std::size_t>(RotationScreenRow::SELECTOR) + 1;

struct RotationScreenState {
    // The row the cursor is on. The screen opens on the selector.
    RotationScreenRow row = RotationScreenRow::SELECTOR;

    // The held preview is up: Select was held on the frame the handler last ran. While it is set the
    // menu's table does not run, so the cursor keeps its row and the menu resumes on release.
    bool previewing = false;

    // The level the held preview shows, 0-9. It keeps its value from one hold to the next while the
    // screen is open.
    std::uint8_t previewLevel = 0;

    void reset() { *this = RotationScreenState{}; }

    friend constexpr bool operator==(const RotationScreenState&,
                                     const RotationScreenState&) = default;
};

}  // namespace kirpich
