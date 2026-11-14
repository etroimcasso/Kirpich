#pragma once

// What a palette looks like in use, as sprites: text in its three inks, then the seven pieces.
//
// The words `selected`, `unselected` and `dimmed`, top to bottom, are each drawn through the font
// palette they name - SelectedGlyphs, Glyphs and DimGlyphs - so the three kinds of text the game draws
// can be told apart, or not, in the palette being judged. Each word is centered across the screen,
// kPalettePreviewWordPitch below the one before. The pieces are PiecePreview at its own fixed place,
// named `preview-piece`, the same shapes where the Palette screen draws them.
//
// A screen shows it while the player holds Select to judge a palette, usually over PreviewBackdrop
// (render/components/preview_backdrop.h). Draw it through the palette being judged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/components/piece_preview.h"  // kPiecePreviewFirstRowY
#include "render/sprites.h"                   // kScreenWidthPx
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

// The words, then the pieces, through `ramp`'s palettes. The words are named by position, as Glyphs
// names them; the pieces `preview-piece-<kind>-<part>`.
[[nodiscard]] Sprites PalettePreview(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
