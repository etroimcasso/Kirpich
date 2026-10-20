#pragma once

// Where the palette editor puts things, in viewport pixels.
//
// Every position here is a pixel; the one exception is the held preview's two brick walls, which are
// cells of the backdrop and so are named by column. The up arrow is the shared page arrow
// (render/components/page_arrows.h) and stands where every screen in the settings family draws its
// own. Shared by the screen function and by the tests that read it back.
//
// The four shade rows stand kEntryPitch apart, each value's digits kGlyph apart, the arrows over and
// under an entered digit kArrowRise from it, and the preview's words kPreviewWordPitch apart; changing
// one of those moves everything derived from it and nothing else.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/background.h"                // kVisibleCols
#include "render/components/piece_preview.h"  // kPiecePreviewFirstRowY
#include "state/custom_palettes.h"            // ColorChannel, kShadeCount

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

// The content draws over the backdrop; the arrows stand above the glyphs inside it. The held preview's
// layer stands at the same depth as the content it replaces.
inline constexpr std::int32_t kContentZ = 10;
inline constexpr std::int32_t kArrowZ   = 30;

// The held preview: the heading where the editor draws it, then three words, one per font ink, each
// centered and kPreviewWordPitch below the one before, then the seven pieces at their own fixed place
// (render/components/piece_preview.h). Word `i` is drawn through the ink its name says.
inline constexpr std::array<std::string_view, 3> kPreviewWords{"selected", "unselected", "dimmed"};
inline constexpr int                             kPreviewWordFirstY = 32;
inline constexpr int                             kPreviewWordPitch  = 16;

// The brick walls that flank the preview: a column of the playfield's own wall tiles down each edge of
// the screen. They are cells of the backdrop, so these two are the one place on the screen named by
// column; kWallLeftX and kWallRightX are their left edges in pixels, for the checks below.
inline constexpr int         kBrickSide       = 8;
inline constexpr std::size_t kWallLeftColumn  = 0;
inline constexpr std::size_t kWallRightColumn = kVisibleCols - 1;
inline constexpr int         kWallLeftX       = static_cast<int>(kWallLeftColumn) * kBrickSide;
inline constexpr int         kWallRightX      = static_cast<int>(kWallRightColumn) * kBrickSide;

[[nodiscard]] constexpr int entryY(std::size_t shade) noexcept {
    return kEntryFirstY + static_cast<int>(shade) * kEntryPitch;
}

[[nodiscard]] constexpr int valueX(kirpich::ColorChannel channel) noexcept {
    return kRedX + static_cast<int>(channel) * kChannelPitch;
}

// Where preview word `i` stands, and where a word `length` characters long starts so it is centered.
[[nodiscard]] constexpr int previewWordY(std::size_t i) noexcept {
    return kPreviewWordFirstY + static_cast<int>(i) * kPreviewWordPitch;
}

[[nodiscard]] constexpr int previewWordX(std::size_t length) noexcept {
    return (kScreenWidth - static_cast<int>(length) * kGlyph) / 2;
}

// Whether every preview word stands between the two walls.
[[nodiscard]] constexpr bool previewWordsInsideWalls() noexcept {
    for (const std::string_view word : kPreviewWords) {
        const int x = previewWordX(word.size());
        if (x < kWallLeftX + kBrickSide || x + static_cast<int>(word.size()) * kGlyph > kWallRightX) {
            return false;
        }
    }
    return true;
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

static_assert(kPreviewWordFirstY >= kHeadingY + kGlyph, "the first preview word runs into the heading");
static_assert(previewWordY(kPreviewWords.size() - 1) + kGlyph <= kPiecePreviewFirstRowY,
              "the last preview word runs into the pieces");
static_assert(kHeadingX >= kWallLeftX + kBrickSide, "the heading runs into the left wall");
static_assert(kAsteriskX + kGlyph <= kWallRightX, "the asterisk runs into the right wall");
static_assert(previewWordsInsideWalls(), "a preview word runs into a wall");

}  // namespace kirpich::render::palette_editor
