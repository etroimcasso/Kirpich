#pragma once

// Where the rotation screen puts things, in viewport pixels.
//
// Every position here is a pixel. The down arrow is the shared page arrow
// (render/components/page_arrows.h) and stands where every screen in the settings family draws its
// own. The held preview is two shared components with their own geometry - PalettePreview's words and
// pieces, and PreviewBackdrop's brick walls, the one part of the screen laid out by cell - and the
// checks at the bottom keep the heading and the `level` item clear of both. Shared by the screen
// function, its components and the tests that read it back.
//
// The screen, top to bottom: the heading; the grid of the ten levels, two rows of five, each cell its
// level's number above that level's four colors; the item that opens the rotation editor; the rotation
// selector; the page arrow.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/components/level_selector.h"    // kLevelSelectorWidth
#include "render/components/palette_preview.h"   // palettePreviewWordsInside
#include "render/components/piece_preview.h"     // kPiecePreviewSecondRowY, kPiecePreviewShapeSpan
#include "render/components/preview_backdrop.h"  // kPreviewWallLeftX, kPreviewWallRightX
#include "state/custom_rotations.h"              // kRotationLevelCount
#include "systems/settings_screen.h"             // kPageDownArrowRow

namespace kirpich::render::palette_rotation {

inline constexpr int kScreenWidth  = 160;
inline constexpr int kScreenHeight = 144;

// A glyph's side, and so the distance from one character of a run to the next.
inline constexpr int kGlyph = 8;

// The heading, centered as sixteen glyphs. The menu and the held preview both draw it here.
inline constexpr std::string_view kHeading  = "palette rotation";
inline constexpr int              kHeadingX = 16;
inline constexpr int              kHeadingY = 16;

// The grid: level `i` in column `i % kGridColumns` of row `i / kGridColumns`. A cell is kGridCellPitch
// wide and the rows stand kGridRowPitch apart; within a cell the digit stands kGridDigitDx from its
// left edge with its top on the row's line, and the swatch - four squares of kGridSquare - stands
// kGridSwatchDx from its left edge and kGridSwatchDy below the line.
inline constexpr int kGridCellPitch = 32;
inline constexpr int kGridColumns   = 5;
inline constexpr int kGridRowPitch  = 24;
inline constexpr int kGridFirstX    = 0;
inline constexpr int kGridFirstY    = 32;
inline constexpr int kGridDigitDx   = 12;
inline constexpr int kGridSwatchDx  = 4;
inline constexpr int kGridSwatchDy  = 8;
inline constexpr int kGridSquare    = 6;

[[nodiscard]] constexpr int gridCellX(std::size_t level) noexcept {
    return kGridFirstX + static_cast<int>(level % kGridColumns) * kGridCellPitch;
}

[[nodiscard]] constexpr int gridRowY(std::size_t level) noexcept {
    return kGridFirstY + static_cast<int>(level / kGridColumns) * kGridRowPitch;
}

[[nodiscard]] constexpr int gridDigitX(std::size_t level) noexcept {
    return gridCellX(level) + kGridDigitDx;
}

[[nodiscard]] constexpr int gridDigitY(std::size_t level) noexcept {
    return gridRowY(level);
}

[[nodiscard]] constexpr int gridSwatchX(std::size_t level) noexcept {
    return gridCellX(level) + kGridSwatchDx;
}

[[nodiscard]] constexpr int gridSwatchY(std::size_t level) noexcept {
    return gridRowY(level) + kGridSwatchDy;
}

// The two menu rows' labels, and the cursor before them.
inline constexpr int kLabelX  = 24;
inline constexpr int kCursorX = 8;

// The item that opens the rotation editor.
inline constexpr std::string_view kEditorItem = "edit rotation";
inline constexpr int              kEditorY    = 80;

// The rotation selector: its label, then the left arrow, the value and the right arrow.
inline constexpr std::string_view kSelectorLabel          = "rotation";
inline constexpr int              kSelectorY              = 96;
inline constexpr int              kSelectorLeftArrowX     = 88;
inline constexpr int              kSelectorValueX         = 96;
inline constexpr int              kSelectorRightArrowX    = 152;
// The widest value: a preset's name is at most seven characters ("rainbow" is seven), and a slot
// reads three (`C01`).
inline constexpr int kSelectorValueMaxGlyphs = 7;

// The `level` item on the held preview: the word's top-left (render/components/level_selector.h).
inline constexpr int kPreviewLevelX = 48;
inline constexpr int kPreviewLevelY = 128;

// The content draws over the backdrop; the arrows stand above the glyphs inside it. The held preview's
// layer stands at the same depth as the menu it replaces.
inline constexpr std::int32_t kContentZ = 10;
inline constexpr std::int32_t kArrowZ   = 30;

inline constexpr std::size_t kLastLevel   = kirpich::kRotationLevelCount - 1;
inline constexpr int         kPageArrowY  = static_cast<int>(systems::kPageDownArrowRow) * kGlyph;
inline constexpr int         kHeadingEndX = kHeadingX + static_cast<int>(kHeading.size()) * kGlyph;

// The menu.
static_assert(kHeadingX >= 0 && kHeadingEndX <= kScreenWidth, "the heading runs off the screen");
static_assert(kGridFirstY >= kHeadingY + kGlyph, "the grid runs into the heading");
static_assert(gridSwatchX(kGridColumns - 1) + 4 * kGridSquare <= kScreenWidth,
              "the grid's last column runs off the screen");
static_assert(gridDigitX(kGridColumns - 1) + kGlyph <= kScreenWidth,
              "the grid's last digit runs off the screen");
static_assert(gridSwatchY(kLastLevel) + kGridSquare <= kEditorY,
              "the grid runs into the editor item");
static_assert(kEditorY + kGlyph <= kSelectorY, "the editor item runs into the selector");
static_assert(kLabelX + static_cast<int>(kSelectorLabel.size()) * kGlyph <= kSelectorLeftArrowX,
              "the selector's label runs into its left arrow");
static_assert(kSelectorLeftArrowX + kGlyph <= kSelectorValueX,
              "the selector's left arrow runs into its value");
static_assert(kSelectorValueX + kSelectorValueMaxGlyphs * kGlyph <= kSelectorRightArrowX,
              "the widest value runs into the selector's right arrow");
static_assert(kSelectorRightArrowX + kGlyph <= kScreenWidth,
              "the selector's right arrow runs off the screen");
static_assert(kSelectorY + kGlyph <= kPageArrowY, "the selector runs into the page arrow");

// The held preview.
static_assert(kHeadingX >= kPreviewWallLeftX + kPreviewBrickSide,
              "the heading runs into the left wall");
static_assert(kHeadingEndX <= kPreviewWallRightX, "the heading runs into the right wall");
static_assert(kPalettePreviewWordFirstY >= kHeadingY + kGlyph,
              "the first preview word runs into the heading");
static_assert(palettePreviewWordsInside(kPreviewWallLeftX + kPreviewBrickSide, kPreviewWallRightX),
              "a preview word runs into a wall");
static_assert(kPreviewLevelY - kGlyph >= kPiecePreviewSecondRowY + kPiecePreviewShapeSpan,
              "the level item's up arrow runs into the pieces");
static_assert(kPreviewLevelY + 2 * kGlyph <= kScreenHeight,
              "the level item's down arrow runs off the screen");
static_assert(kPreviewLevelX >= kPreviewWallLeftX + kPreviewBrickSide,
              "the level item runs into the left wall");
static_assert(kPreviewLevelX + kLevelSelectorWidth <= kPreviewWallRightX,
              "the level item runs into the right wall");

}  // namespace kirpich::render::palette_rotation
