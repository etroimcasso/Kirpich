#pragma once

// Where the Controls screen puts things.
//
// The screen is a grid of the background's eight-pixel cells, the one every settings screen is laid
// out on (systems/settings_screen.h, converted to pixels by render/option_row.h): the heading on the
// shared heading row, and the first button on the row every settings screen puts its first option on.
// Eight buttons do not fit the settings pages' three-line spacing on an eighteen-line screen, so they
// stand one per line. Shared by the screen function and by the tests that read it back.
//
// Each line reads: the button's label from column 1, the keyboard cursor on column 8 and the key's
// name from 9, the controller cursor on column 14 and the controller button's name from 15. The
// column heads stand over the names on line 4. The cursor stands in the cell before the name it
// marks.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"  // kVisibleCols
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

// The lines, in cells.
inline constexpr std::size_t kColumnHeadLine  = 4;
inline constexpr std::size_t kFirstButtonLine = systems::kSettingsFirstRow;
inline constexpr std::size_t kPromptLine      = 15;
inline constexpr std::size_t kCancelHintLine  = 16;

// The longest button label, "select", ends before the keyboard cursor; each name fits the cells
// between its cursor and the next column; and the controller names end on the screen's last column.
static_assert(kLabelCol + 6 <= kKeyCursorCol);
static_assert(kKeyNameCol + kControlsNameWidth <= kControllerCursorCol);
static_assert(kControllerNameCol + kControlsNameWidth <= kVisibleCols);
static_assert(kFirstButtonLine + kirpich::kGbButtonCount <= kPromptLine);

inline constexpr int kColumnHeadY     = optionPixels(kColumnHeadLine);
inline constexpr int kLabelX          = optionPixels(kLabelCol);
inline constexpr int kKeyNameX        = optionPixels(kKeyNameCol);
inline constexpr int kControllerNameX = optionPixels(kControllerNameCol);
inline constexpr int kPromptY         = optionPixels(kPromptLine);
inline constexpr int kCancelHintY     = optionPixels(kCancelHintLine);

// The line a button stands on.
[[nodiscard]] constexpr std::size_t lineFor(kirpich::GbButton button) noexcept {
    return kFirstButtonLine + static_cast<std::size_t>(button);
}

[[nodiscard]] constexpr int rowY(kirpich::GbButton button) noexcept {
    return optionPixels(lineFor(button));
}

[[nodiscard]] constexpr int cursorX(kirpich::ControlsColumn column) noexcept {
    return optionPixels(column == kirpich::ControlsColumn::KEYBOARD ? kKeyCursorCol
                                                                    : kControllerCursorCol);
}

[[nodiscard]] constexpr int nameX(kirpich::ControlsColumn column) noexcept {
    return column == kirpich::ControlsColumn::KEYBOARD ? kKeyNameX : kControllerNameX;
}

// What the screen says while it waits for a press, centered across the screen on its own line.
inline constexpr std::string_view kKeyPrompt        = "press a key";
inline constexpr std::string_view kControllerPrompt = "press a button";
inline constexpr std::string_view kCancelHint       = "esc cancels";

// What stands in the cell being listened on.
inline constexpr std::string_view kWaiting = "...";

[[nodiscard]] constexpr std::string_view prompt(kirpich::ControlsColumn column) noexcept {
    return column == kirpich::ControlsColumn::KEYBOARD ? kKeyPrompt : kControllerPrompt;
}

// The content draws over the backdrop.
inline constexpr std::int32_t kContentZ = 10;

}  // namespace kirpich::render::controls
