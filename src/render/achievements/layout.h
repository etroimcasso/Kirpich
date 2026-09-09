#pragma once

// Where the achievements screen puts things, in viewport pixels.
//
// Pixels rather than cells: the badges and the text are sprites, placed where they read best rather
// than on the background's eight-pixel grid. Shared by the components that draw them and by the tests
// that read them back, so the numbers live in one place.

#include <cstddef>

#include "systems/achievements_screen.h"  // kAchievementGridCols

namespace kirpich::render {

inline constexpr int kAchCell       = 8;  // a background cell's side
inline constexpr int kAchGlyphPitch = 8;  // one character to the next

// The heading, on the row every port screen puts its title on.
inline constexpr int kAchHeadingX = 16;
inline constexpr int kAchHeadingY = 2 * kAchCell;

// The badge grid: three across, a slot every 48 x 40 pixels, starting clear of the heading.
inline constexpr int kAchGridX     = 16;
inline constexpr int kAchGridY     = 40;
inline constexpr int kAchGridStepX = 48;
inline constexpr int kAchGridStepY = 40;

// An open badge's panel: the badge, then its title, the description of what it asks for, and when it
// was earned. The description wraps, so it has a line step and a width to wrap at - the width is in
// characters, which at the glyph pitch is what fits between the text's left edge and the screen's.
inline constexpr int kAchPanelBadgeX = 16;
inline constexpr int kAchPanelBadgeY = 24;
inline constexpr int kAchPanelTextX  = 16;
inline constexpr int kAchPanelTitleY = 56;
inline constexpr int kAchPanelDescY  = 72;
inline constexpr int kAchPanelStampY = 120;

inline constexpr int         kAchPanelLineStep  = 12;
inline constexpr std::size_t kAchPanelTextCells = 18;

// Badges ride above the backdrop; text above the badges, so a name over a panel badge stays legible.
inline constexpr int kAchBadgeZ = 10;
inline constexpr int kAchTextZ  = 20;

// Where a badge sits in the grid.
[[nodiscard]] constexpr int achGridX(std::size_t slot) noexcept {
    return kAchGridX + static_cast<int>(slot % systems::kAchievementGridCols) * kAchGridStepX;
}
[[nodiscard]] constexpr int achGridY(std::size_t slot) noexcept {
    return kAchGridY + static_cast<int>(slot / systems::kAchievementGridCols) * kAchGridStepY;
}

// The box the cursor brackets: the badge's own art with a little air around it, so the corners sit
// outside it rather than on it. The emblems are different shapes - a square of four, a bar of four
// across, a figure three tall - so the box is sized from the badge rather than fixed.
inline constexpr float kAchSelectMargin = 4.0f;

[[nodiscard]] constexpr float achSelectX(std::size_t slot) noexcept {
    return static_cast<float>(achGridX(slot)) - kAchSelectMargin;
}
[[nodiscard]] constexpr float achSelectY(std::size_t slot) noexcept {
    return static_cast<float>(achGridY(slot)) - kAchSelectMargin;
}
[[nodiscard]] constexpr float achSelectSpan(int extent) noexcept {
    return static_cast<float>(extent) + 2.0f * kAchSelectMargin;
}

}  // namespace kirpich::render
