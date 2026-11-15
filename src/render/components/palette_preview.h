#pragma once

// What a palette looks like in use, as sprites: text in its three inks, then the seven pieces, and,
// when a level is given, the `level` item under them.
//
// The words `selected`, `unselected` and `dimmed`, top to bottom, are each drawn through the font
// palette they name - SelectedGlyphs, Glyphs and DimGlyphs - so the three kinds of text the game draws
// can be told apart, or not, in the palette being judged. Each word is centered across the screen,
// kPalettePreviewWordPitch below the one before. The pieces are PiecePreview at its own fixed place,
// named `preview-piece`, the same shapes where the Palette screen draws them.
//
// The level is the preview's one switch. Given a level, the `level` item (LevelSelector) stands under
// the pieces at kPalettePreviewLevelX/Y, reading which level the palette is, with an arrow each way the
// level can still step; given none, nothing stands there. Every screen that previews a level's
// palette shows it this way and only this way.
//
// A screen shows it while the player holds Select to judge a palette, over PreviewBackdrop
// (render/components/preview_backdrop.h) and under the screen's own heading. Draw it through the
// palette being judged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "render/components/level_selector.h"    // kLevelSelectorWidth
#include "render/components/piece_preview.h"     // kPiecePreviewFirstRowY, kPiecePreviewSecondRowY
#include "render/components/preview_backdrop.h"  // kPreviewWallLeftX, kPreviewWallRightX
#include "render/sprites.h"                      // kScreenWidthPx, kScreenHeightPx
#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The three words, top to bottom. Word `i` is drawn through the ink its name says.
inline constexpr std::array<std::string_view, 3> kPalettePreviewWords{"selected", "unselected",
                                                                       "dimmed"};

// The first word's top, the distance from one word's top to the next, and a glyph's side.
inline constexpr int kPalettePreviewWordFirstY = 32;
inline constexpr int kPalettePreviewWordPitch  = 16;
inline constexpr int kPalettePreviewGlyph      = 8;

// Where word `i` stands, and where a word `length` characters long starts so it is centered.
[[nodiscard]] constexpr int palettePreviewWordY(std::size_t i) noexcept {
    return kPalettePreviewWordFirstY + static_cast<int>(i) * kPalettePreviewWordPitch;
}

[[nodiscard]] constexpr int palettePreviewWordX(std::size_t length) noexcept {
    return (kScreenWidthPx - static_cast<int>(length) * kPalettePreviewGlyph) / 2;
}

// Whether every word stands between viewport x `left` and x `right`, its last glyph ending at or
// before `right`. A screen that frames the preview asserts this against its frame.
[[nodiscard]] constexpr bool palettePreviewWordsInside(int left, int right) noexcept {
    for (const std::string_view word : kPalettePreviewWords) {
        const int x = palettePreviewWordX(word.size());
        if (x < left || x + static_cast<int>(word.size()) * kPalettePreviewGlyph > right) {
            return false;
        }
    }
    return true;
}

static_assert(palettePreviewWordY(kPalettePreviewWords.size() - 1) + kPalettePreviewGlyph <=
                  kPiecePreviewFirstRowY,
              "the last word runs into the pieces");

// The `level` item's word, its top-left in viewport pixels. The item reaches a glyph above and two
// glyphs below its word's top (LevelSelector's arrows).
inline constexpr int kPalettePreviewLevelX = 48;
inline constexpr int kPalettePreviewLevelY = 128;

static_assert(kPalettePreviewLevelY - kPalettePreviewGlyph >=
                  kPiecePreviewSecondRowY + kPiecePreviewShapeSpan,
              "the level item's up arrow runs into the pieces");
static_assert(kPalettePreviewLevelY + 2 * kPalettePreviewGlyph <= kScreenHeightPx,
              "the level item's down arrow runs off the screen");
static_assert(kPalettePreviewLevelX >= kPreviewWallLeftX + kPreviewBrickSide,
              "the level item runs into the left wall");
static_assert(kPalettePreviewLevelX + kLevelSelectorWidth <= kPreviewWallRightX,
              "the level item runs into the right wall");

// The words, then the pieces, then - when `level` holds one, 0-9 - the `level` item, through `ramp`'s
// palettes. The words are named by position, as Glyphs names them; the pieces
// `preview-piece-<kind>-<part>`; the item's arrows `preview-level-left`, `-right`, `-up` and `-down`.
[[nodiscard]] Sprites PalettePreview(const TileAtlas& atlas, std::uint8_t ramp,
                                     std::optional<std::uint8_t> level = std::nullopt);

}  // namespace kirpich::render
