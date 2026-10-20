#pragma once

// Where the end-of-round notice puts its banner, in viewport pixels.
//
// The banner is the badge on the left with its title beside it and the description under the title,
// one badge to a screen. The text hangs off the badge's own width rather than off a fixed column: the
// set's emblems are different shapes, and a bar four tiles across would run under a title placed at a
// constant offset. For the same reason the wrap width is derived from where the text actually starts,
// so a wider badge narrows the passage instead of pushing it off the screen.

#include <cstddef>

#include "render/background.h"  // kVisibleCols

namespace kirpich::render {

inline constexpr int kNoticeCell       = 8;   // a background cell's side
inline constexpr int kNoticeGlyphPitch = 8;   // one character to the next
inline constexpr int kNoticeLineStep   = 12;  // one line of the description to the next

// The title to the first line of the description. Wider than the step between the description's own
// lines, so the title reads as a heading for it rather than as its first line.
inline constexpr int kNoticeTitleGap = 20;

// The screen the banner is placed on.
inline constexpr int kNoticeScreenWidth = static_cast<int>(kVisibleCols) * kNoticeCell;

// The badge, far enough down that a banner of a title and three description lines sits about the
// middle of the screen.
inline constexpr int kNoticeBadgeX = 16;
inline constexpr int kNoticeBadgeY = 48;

// The air between the badge and the text beside it.
inline constexpr int kNoticeTextGap = 8;

// Where the text starts, given how wide this badge's art turned out to be.
[[nodiscard]] constexpr int noticeTextX(int badgeWidth) noexcept {
    return kNoticeBadgeX + badgeWidth + kNoticeTextGap;
}

// How many characters fit on a line that starts at `textX` and ends at the screen's edge.
[[nodiscard]] constexpr std::size_t noticeTextCells(int textX) noexcept {
    const int room = kNoticeScreenWidth - textX;
    return room <= 0 ? 0 : static_cast<std::size_t>(room / kNoticeGlyphPitch);
}

}  // namespace kirpich::render
