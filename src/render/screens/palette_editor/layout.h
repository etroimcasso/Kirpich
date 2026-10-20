#pragma once

// Where the palette editor puts things, in viewport pixels.
//
// Every position here is a pixel; nothing is placed on the background's cell grid. The up arrow is the
// shared page arrow (render/components/page_arrows.h) and stands where every screen in the settings
// family draws its own. Shared by the screen function and by the tests that read it back.
//
// The four shade rows stand kEntryPitch apart, each value's digits kGlyph apart, and the arrows over
// and under an entered digit kArrowRise from it; changing one of those moves everything derived from
// it and nothing else.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "state/custom_palettes.h"  // ColorChannel, kShadeCount

namespace kirpich::render::palette_editor {

inline constexpr int kScreenWidth  = 160;
inline constexpr int kScreenHeight = 144;

// A glyph's side, and so the distance from one character of a run to the next.
inline constexpr int kGlyph = 8;

// The heading, centered as fourteen glyphs, and the asterisk in the cell after it while the working
// palette has unsaved changes. The heading never moves for the asterisk.
inline constexpr std::string_view kHeading  = "custom palette";
inline constexpr int              kHeadingX = 24;
inline constexpr int              kHeadingY = 16;
inline constexpr int kAsteriskX = kHeadingX + static_cast<int>(kHeading.size()) * kGlyph;

// The labels of the four rows above the shades, and the cursor before them.
inline constexpr int kLabelX        = 24;
inline constexpr int kActionCursorX = 8;

// The preset row and its selector: the left arrow, the two digits, the right arrow.
inline constexpr int kPresetY           = 32;
inline constexpr int kPresetLeftArrowX  = 104;
inline constexpr int kPresetValueX      = 120;
inline constexpr int kPresetRightArrowX = 152;

// The three action rows.
inline constexpr int kLoadY  = 40;
inline constexpr int kSaveY  = 48;
inline constexpr int kClearY = 56;

// The shade rows: shade `i` stands at kEntryFirstY + i * kEntryPitch, its number at kEntryNumberX and
// its red, green and blue values kChannelPitch apart from kRedX.
inline constexpr int kEntryFirstY  = 72;
inline constexpr int kEntryPitch   = 16;
inline constexpr int kEntryNumberX = 8;
inline constexpr int kRedX         = 24;
inline constexpr int kChannelPitch = 32;
inline constexpr int kValueDigits  = 3;
inline constexpr int kValueWidth   = kValueDigits * kGlyph;

// The cursor and the left arrow stand kValueCursorGap before a value, the right arrow just after it,
// and the up and down arrows kArrowRise above and below the digit being stepped.
inline constexpr int kValueCursorGap = 8;
inline constexpr int kArrowRise      = 8;

// The band: the four shades of the working palette as rectangles kBandWidth wide and kEntryPitch
// tall, abutting down the right of the shade rows.
inline constexpr int kBandX     = 120;
inline constexpr int kBandWidth = 16;

// The hint, while a value is entered.
inline constexpr std::string_view kHint  = "a confirms b cancels";
inline constexpr int              kHintX = 0;
inline constexpr int              kHintY = 136;

// The content draws over the backdrop; the arrows stand above the glyphs inside it.
inline constexpr std::int32_t kContentZ = 10;
inline constexpr std::int32_t kArrowZ   = 30;

[[nodiscard]] constexpr int entryY(std::size_t shade) noexcept {
    return kEntryFirstY + static_cast<int>(shade) * kEntryPitch;
}

[[nodiscard]] constexpr int valueX(kirpich::ColorChannel channel) noexcept {
    return kRedX + static_cast<int>(channel) * kChannelPitch;
}

inline constexpr int kLastEntryY = entryY(kirpich::kShadeCount - 1);
inline constexpr int kBlueX      = valueX(kirpich::ColorChannel::BLUE);

static_assert(kAsteriskX + kGlyph <= kScreenWidth, "the asterisk runs off the screen");
static_assert(kPresetRightArrowX + kGlyph <= kScreenWidth, "the preset's right arrow runs off");
static_assert(kEntryFirstY - kArrowRise >= kClearY + kGlyph,
              "the first shade's up arrow runs into the clear row");
static_assert(kLastEntryY + kArrowRise + kGlyph <= kHintY,
              "the last shade's down arrow runs into the hint");
static_assert(kBlueX + kValueWidth + kGlyph <= kBandX,
              "the blue value's right arrow runs into the band");
static_assert(kBandX + kBandWidth <= kScreenWidth, "the band runs off the screen");
static_assert(kEntryFirstY + static_cast<int>(kirpich::kShadeCount) * kEntryPitch <= kHintY,
              "the band runs into the hint");
static_assert(kHintX + static_cast<int>(kHint.size()) * kGlyph <= kScreenWidth,
              "the hint runs off the screen");
static_assert(kHintY + kGlyph <= kScreenHeight, "the hint runs off the bottom of the screen");

}  // namespace kirpich::render::palette_editor
