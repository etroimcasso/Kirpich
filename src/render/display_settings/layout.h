#pragma once

// Where the Display settings screen puts things, in viewport pixels.
//
// Every number here is derived from the settings screens' shared cell grid (systems/settings_screen.h,
// through render/option_row.h), so the screen's heading, rows and cursor sit exactly where the
// settings screen puts its own. Shared by the screen function and by the tests that read it back.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/option_row.h"
#include "state/display_settings_state.h"
#include "systems/settings_screen.h"

namespace kirpich::render::display_settings {

inline constexpr std::string_view kHeading = "display";

inline constexpr int kHeadingX = optionHeadingX(kHeading.size());
inline constexpr int kHeadingY = kOptionHeadingY;
inline constexpr int kPitch    = kOptionCell;
inline constexpr int kCursorX  = kOptionCursorX;

// The map row each option sits on: the first two lines a settings page lays its rows on.
[[nodiscard]] constexpr std::size_t lineFor(kirpich::DisplaySettingsRow row) noexcept {
    return systems::kSettingsFirstRow +
           systems::kSettingsRowStride * static_cast<std::size_t>(row);
}

inline constexpr std::size_t kFullscreenLine = lineFor(kirpich::DisplaySettingsRow::FULLSCREEN);
inline constexpr std::size_t kSizeLine       = lineFor(kirpich::DisplaySettingsRow::WINDOW_SCALE);

[[nodiscard]] constexpr int cursorY(kirpich::DisplaySettingsRow row) noexcept {
    return optionPixels(lineFor(row));
}

// The content draws over the backdrop.
inline constexpr std::int32_t kContentZ = 10;

}  // namespace kirpich::render::display_settings
