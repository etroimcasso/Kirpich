#pragma once

// Where the rotation editor puts things, in viewport pixels.
//
// Every position here is a pixel. The held preview is shared components with their own geometry -
// PalettePreview's words and pieces, and PreviewBackdrop's brick walls - and the Type A panel's level
// box, which LevelPreviewBackdrop stamps into the backdrop; the backdrop is the one part of the screen
// laid out by cell, so the box's place is given in cells. The checks at the bottom keep every part
// clear of its neighbors. Shared by the screen function, its components, the logic's grid walk and
// the tests that read it back.
//
// The screen, top to bottom: the heading; the preset selector and its three actions; the grid of the
// ten levels, two columns of five, level `i` in column `i / kGridRows` of row `i % kGridRows`. A cell,
// from its left edge, is the cursor, the level's digit, a colon, a place for the entered number's left
// arrow, the number and its right arrow, with the level's four colors under the digit.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/components/palette_preview.h"   // kPalettePreviewWordFirstY, palettePreviewWordsInside
#include "render/components/preview_backdrop.h"  // kPreviewWallLeftColumn, kPreviewWallRightColumn
#include "render/rotation_presets.h"             // kRotationPresetNameMaxGlyphs
#include "render/screens/rotation_editor/level_preview_backdrop.h"  // kLevelBoxWidth, kLevelBoxHeight
#include "state/custom_rotations.h"              // kRotationLevelCount, kRotationNameLength

namespace kirpich::render::rotation_editor {

inline constexpr int kScreenWidth  = 160;
inline constexpr int kScreenHeight = 144;

// A glyph's side, and so the distance from one character of a run to the next.
inline constexpr int kGlyph = 8;

// The heading, centered as fifteen glyphs, and the asterisk in the cell after it while the working
// rotation has unsaved changes. The heading never moves for the asterisk.
inline constexpr std::string_view kHeading   = "custom rotation";
inline constexpr int              kHeadingX  = 20;
inline constexpr int              kHeadingY  = 16;
inline constexpr int              kAsteriskX = 140;

// The labels of the four rows above the grid, and the cursor before them.
inline constexpr int kLabelX  = 24;
inline constexpr int kCursorX = 8;

// The four rows above the grid.
inline constexpr int kPresetY = 32;
inline constexpr int kLoadY   = 40;
inline constexpr int kSaveY   = 48;
inline constexpr int kClearY  = 56;

// The preset selector: the left arrow, the value and the right arrow. The widest value is a name,
// shipped or the player's, of seven characters.
inline constexpr int kPresetLeftArrowX     = 80;
inline constexpr int kPresetValueX         = 88;
inline constexpr int kPresetRightArrowX    = 152;
inline constexpr int kPresetValueMaxGlyphs = 7;

// The name screen's title: the save's own.
inline constexpr std::string_view kNameTitle = "save preset";

// The grid's shape: two columns of five rows.
inline constexpr int kGridColumns = 2;
inline constexpr int kGridRows    = 5;

// The grid's placing: one column to the next, one row to the next, and the first cell's top-left.
inline constexpr int kGridCellPitch = 80;
inline constexpr int kGridRowPitch  = 15;
inline constexpr int kGridFirstX    = 0;
inline constexpr int kGridFirstY    = 68;

// Within a cell, from its left edge: the cursor, the level's digit and the colon.
inline constexpr int kCellCursorDx = 8;
inline constexpr int kCellLevelDx  = 16;
inline constexpr int kCellColonDx  = 24;

// The entered number's left arrow, the number, its widest text (`C04`) and its right arrow.
inline constexpr int kCellLeftArrowDx     = 32;
inline constexpr int kCellNumberDx        = 40;
inline constexpr int kCellNumberMaxGlyphs = 3;
inline constexpr int kCellRightArrowDx    = 64;

// The swatch under the cell's text: four squares of kCellSquare pixels, from under the level's digit.
inline constexpr int kCellSwatchDx = 16;
inline constexpr int kCellSwatchDy = 8;
inline constexpr int kCellSquare   = 5;

// Which column and row of the grid level `i` stands in.
[[nodiscard]] constexpr int gridColumn(std::size_t level) noexcept {
    return static_cast<int>(level) / kGridRows;
}

[[nodiscard]] constexpr int gridRow(std::size_t level) noexcept {
    return static_cast<int>(level) % kGridRows;
}

// Level `i`'s cell: its top-left, and its swatch's top-left.
[[nodiscard]] constexpr int cellX(std::size_t level) noexcept {
    return kGridFirstX + gridColumn(level) * kGridCellPitch;
}

[[nodiscard]] constexpr int cellY(std::size_t level) noexcept {
    return kGridFirstY + gridRow(level) * kGridRowPitch;
}

[[nodiscard]] constexpr int cellSwatchX(std::size_t level) noexcept {
    return cellX(level) + kCellSwatchDx;
}

[[nodiscard]] constexpr int cellSwatchY(std::size_t level) noexcept {
    return cellY(level) + kCellSwatchDy;
}

// The level box on the held preview, in backdrop cells: its top-left column and row.
inline constexpr std::size_t kPreviewLevelBoxColumn = 6;
inline constexpr std::size_t kPreviewLevelBoxRow    = 0;

// The content draws over the backdrop; the arrows stand above the glyphs inside it. The held preview's
// layer stands at the same depth as the content it replaces.
inline constexpr std::int32_t kContentZ = 10;
inline constexpr std::int32_t kArrowZ   = 30;

inline constexpr std::size_t kLastLevel = kirpich::kRotationLevelCount - 1;

// The rows above the grid.
static_assert(kHeadingX >= 0 &&
                  kHeadingX + static_cast<int>(kHeading.size()) * kGlyph <= kAsteriskX,
              "the heading runs into its asterisk");
static_assert(kAsteriskX + kGlyph <= kScreenWidth, "the asterisk runs off the screen");
static_assert(kLabelX + 6 * kGlyph <= kPresetLeftArrowX,
              "the preset label runs into its left arrow");
static_assert(kPresetLeftArrowX + kGlyph <= kPresetValueX,
              "the preset's left arrow runs into its value");
static_assert(kPresetValueX + kPresetValueMaxGlyphs * kGlyph <= kPresetRightArrowX,
              "the widest preset value runs into the right arrow");
static_assert(kPresetRightArrowX + kGlyph <= kScreenWidth, "the preset's right arrow runs off");
static_assert(kPresetValueMaxGlyphs >= static_cast<int>(kirpich::kRotationNameLength),
              "the preset's value has no room for a saved rotation's name");
static_assert(kPresetValueMaxGlyphs >= kRotationPresetNameMaxGlyphs,
              "the preset's value has no room for a shipped rotation's name");

// The grid.
static_assert(kGridColumns * kGridRows == static_cast<int>(kirpich::kRotationLevelCount),
              "the grid holds one cell per level");
static_assert(kGridFirstY >= kClearY + kGlyph, "the grid runs into the clear row");
static_assert(kCellSwatchDy + kCellSquare <= kGridRowPitch,
              "a cell's text and swatch run into the next row");
static_assert(kGlyph <= kCellSwatchDy, "a cell's swatch runs into its text");
static_assert(cellSwatchY(kLastLevel) + kCellSquare <= kScreenHeight,
              "the last cell's swatch runs off the screen");
static_assert(kCellNumberDx + kCellNumberMaxGlyphs * kGlyph <= kCellRightArrowDx,
              "the number's widest text runs into its right arrow");
static_assert(kCellLeftArrowDx + kGlyph <= kCellNumberDx,
              "the number's left arrow runs into the number");
static_assert(kCellColonDx + kGlyph <= kCellLeftArrowDx, "the colon runs into the left arrow");
static_assert(cellX(kLastLevel) + kCellRightArrowDx + kGlyph <= kScreenWidth,
              "the right column's right arrow runs off the screen");
static_assert(kCellRightArrowDx + kGlyph <= kGridCellPitch,
              "a cell's right arrow runs into the next column");
static_assert(kCellSwatchDx + 4 * kCellSquare <= kGridCellPitch, "a swatch is wider than its cell");

// The held preview.
static_assert(kPreviewLevelBoxColumn > kPreviewWallLeftColumn,
              "the level box runs into the left wall");
static_assert(kPreviewLevelBoxColumn + kLevelBoxWidth <= kPreviewWallRightColumn,
              "the level box runs into the right wall");
static_assert(static_cast<int>(kPreviewLevelBoxRow + kLevelBoxHeight) * kGlyph <=
                  kPalettePreviewWordFirstY,
              "the level box runs into the first preview word");
static_assert(palettePreviewWordsInside(kPreviewWallLeftX + kPreviewBrickSide, kPreviewWallRightX),
              "a preview word runs into a wall");

}  // namespace kirpich::render::rotation_editor
