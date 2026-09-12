#pragma once

// Where the achievements screen puts things, in viewport pixels.
//
// Pixels rather than cells: the badges and the text are sprites, placed where they read best rather
// than on the background's eight-pixel grid. Shared by the components that draw them and by the tests
// that read them back, so the numbers live in one place.

#include <cstddef>

#include "render/background.h"            // kVisibleRows
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
//
// The vertical run is derived rather than restated, so a row cannot drift into the one above it. The
// badge hangs a clear cell below the heading; the title clears the tallest badge the set has (a
// standing figure, three tiles); and the description opens a wider gap than it keeps between its own
// lines, so the title reads as a heading for it rather than as its first line.
inline constexpr int kAchPanelBadgeX = 16;
inline constexpr int kAchPanelTextX  = 16;

inline constexpr int kAchPanelHeadingGap = kAchCell;      // heading to badge
inline constexpr int kAchPanelBadgeSpan  = 3 * kAchCell;  // the tallest badge art in the set
inline constexpr int kAchPanelBadgeGap   = kAchCell;      // badge to title
inline constexpr int kAchPanelTitleGap   = 20;            // title to the description under it

inline constexpr int         kAchPanelLineStep  = 12;
inline constexpr std::size_t kAchPanelTextCells = 18;
inline constexpr std::size_t kAchPanelDescLines = 3;  // the longest description in the set wraps to 3

inline constexpr int kAchPanelBadgeY = kAchHeadingY + kAchCell + kAchPanelHeadingGap;
inline constexpr int kAchPanelTitleY = kAchPanelBadgeY + kAchPanelBadgeSpan + kAchPanelBadgeGap;
inline constexpr int kAchPanelDescY  = kAchPanelTitleY + kAchPanelTitleGap;
inline constexpr int kAchPanelStampY =
    kAchPanelDescY + static_cast<int>(kAchPanelDescLines) * kAchPanelLineStep + kAchCell;

// The whole run has to land on the screen: opening a gap anywhere above pushes the stamp down, and
// there is no scroll to absorb it.
static_assert(kAchPanelStampY + kAchCell <= static_cast<int>(kVisibleRows) * kAchCell,
              "the panel's unlock stamp runs off the bottom of the screen");

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
