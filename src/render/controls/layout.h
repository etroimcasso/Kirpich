#pragma once

// Where the Controls screen puts things.
//
// The screen is a grid of the background's eight-pixel cells, the one every settings screen is laid
// out on (systems/settings_screen.h, converted to pixels by render/option_row.h): the heading on the
// shared heading row, and the first button on the row every settings screen puts its first option on.
// Eight buttons do not fit the settings pages' three-line spacing on an eighteen-line screen, so they
// stand one per line. Shared by the screen function and by the tests that read it back.
//
// Each button's line reads: the button's label from column 1, the keyboard cursor on column 8 and the
// key's name from 9, the controller cursor on column 14 and the controller button's name from 15. The
// column heads stand over the names on line 4. The cursor stands in the cell before the name it
// marks. The restore row stands a line below the buttons, laid out as a settings row is: its label
// from column 3 and its cursor on column 1. The prompt and its hint fill the two lines under it.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"  // kVisibleCols, kVisibleRows
#include "render/controls/names.h"
#include "render/option_row.h"
#include "state/controls.h"
#include "state/controls_screen_state.h"
#include "systems/settings_screen.h"

namespace kirpich::render::controls {

inline constexpr std::string_view kHeading = "controls";

inline constexpr int kHeadingX = optionHeadingX(kHeading.size());
inline constexpr int kHeadingY = kOptionHeadingY;
inline constexpr int kPitch    = kOptionCell;

// The columns, in cells.
inline constexpr std::size_t kLabelCol            = 1;
inline constexpr std::size_t kKeyCursorCol        = 8;
inline constexpr std::size_t kKeyNameCol          = 9;
inline constexpr std::size_t kControllerCursorCol = 14;
inline constexpr std::size_t kControllerNameCol   = 15;
inline constexpr std::size_t kRestoreLabelCol     = systems::kLabelCol;
inline constexpr std::size_t kRestoreCursorCol    = systems::kCursorCol;

// The lines, in cells.
inline constexpr std::size_t kColumnHeadLine  = 4;
inline constexpr std::size_t kFirstButtonLine = systems::kSettingsFirstRow;
inline constexpr std::size_t kRestoreLine     = kFirstButtonLine + kirpich::kGbButtonCount + 1;
inline constexpr std::size_t kPromptLine      = kRestoreLine + 2;
inline constexpr std::size_t kCancelHintLine  = kPromptLine + 1;

inline constexpr std::string_view kRestoreLabel = "restore defaults";

// The longest button label, "select", ends before the keyboard cursor; each name fits the cells
// between its cursor and the next column; the controller names and the restore label end on or before
// the screen's last column; and the hint is on the screen's last line or above it.
static_assert(kLabelCol + 6 <= kKeyCursorCol);
static_assert(kKeyNameCol + kControlsNameWidth <= kControllerCursorCol);
static_assert(kControllerNameCol + kControlsNameWidth <= kVisibleCols);
static_assert(kRestoreLabelCol + kRestoreLabel.size() <= kVisibleCols);
static_assert(kCancelHintLine < kVisibleRows);

inline constexpr int kColumnHeadY     = optionPixels(kColumnHeadLine);
inline constexpr int kLabelX          = optionPixels(kLabelCol);
inline constexpr int kKeyNameX        = optionPixels(kKeyNameCol);
inline constexpr int kControllerNameX = optionPixels(kControllerNameCol);
inline constexpr int kRestoreLabelX   = optionPixels(kRestoreLabelCol);
inline constexpr int kPromptY         = optionPixels(kPromptLine);
inline constexpr int kCancelHintY     = optionPixels(kCancelHintLine);

// The line a row stands on.
[[nodiscard]] constexpr std::size_t lineFor(kirpich::ControlsRow row) noexcept {
    return row == kirpich::ControlsRow::RESTORE_DEFAULTS
               ? kRestoreLine
               : kFirstButtonLine + static_cast<std::size_t>(row);
}

[[nodiscard]] constexpr int rowY(kirpich::ControlsRow row) noexcept {
    return optionPixels(lineFor(row));
}

[[nodiscard]] constexpr int rowY(kirpich::GbButton button) noexcept {
    return rowY(kirpich::rowOf(button));
}

// Where the cursor stands: before the cell's name on a button's row, and before the label on the
// restore row, which has no columns.
[[nodiscard]] constexpr int cursorX(kirpich::ControlsRow row, kirpich::ControlsColumn column) noexcept {
    if (row == kirpich::ControlsRow::RESTORE_DEFAULTS) {
        return optionPixels(kRestoreCursorCol);
    }
    return optionPixels(column == kirpich::ControlsColumn::KEYBOARD ? kKeyCursorCol
                                                                    : kControllerCursorCol);
}

// What the screen says while it waits for a press, centered across the screen on its own line.
inline constexpr std::string_view kKeyPrompt        = "press a key";
inline constexpr std::string_view kControllerPrompt = "press a button";
inline constexpr std::string_view kCancelHint       = "esc cancels";

// What stands in the cell being listened on.
inline constexpr std::string_view kWaiting = "...";

// The question asked before restoring the defaults, on the settings confirms' cells; its title is
// kRestoreLabel.
inline constexpr std::string_view kConfirmFirstLine  = "restore every";
inline constexpr std::string_view kConfirmSecondLine = "key and button";
inline constexpr std::string_view kConfirmNo         = "no";
inline constexpr std::string_view kConfirmYes        = "yes";

[[nodiscard]] constexpr std::string_view prompt(kirpich::ControlsColumn column) noexcept {
    return column == kirpich::ControlsColumn::KEYBOARD ? kKeyPrompt : kControllerPrompt;
}

// The content draws over the backdrop.
inline constexpr std::int32_t kContentZ = 10;

}  // namespace kirpich::render::controls
