#include "render/components/glyphs.h"

#include <optional>
#include <string>

#include <kirpich/char_tile.h>

#include "render/glyph_extras.h"  // kAsteriskCell

namespace kirpich::render {

namespace {

// Text draws above the art it labels.
constexpr int kGlyphZ = 20;

// The font glyph a character draws with, or nothing when the font has no glyph for it.
[[nodiscard]] std::optional<std::uint8_t> glyphIndexFor(char c) noexcept {
    if (c >= 'a' && c <= 'z') {
        return static_cast<std::uint8_t>(static_cast<int>(CharTile::LETTER_A) + (c - 'a'));
    }
    if (c >= 'A' && c <= 'Z') {
        return static_cast<std::uint8_t>(static_cast<int>(CharTile::LETTER_A) + (c - 'A'));
    }
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(static_cast<int>(CharTile::DIGIT_0) + (c - '0'));
    }
    if (c == '.') return static_cast<std::uint8_t>(CharTile::PERIOD);
    if (c == '-') return static_cast<std::uint8_t>(CharTile::HYPHEN);
    return std::nullopt;
}

// Which of the ramp's font object palettes a run draws through.
enum class Ink : std::uint8_t {
    INK,       // fontSprite - the darkest shade
    DIM,       // fontSpriteDim - the light shade
    SELECTED,  // fontSpriteSelected - the dark shade
};

[[nodiscard]] retropp::PaletteId fontPaletteFor(Ink ink, const TileAtlas& atlas,
                                                std::uint8_t ramp) noexcept {
    const RampPalettes& palettes = palettesFor(atlas, ramp);
    switch (ink) {
        case Ink::DIM:
            return palettes.fontSpriteDim;
        case Ink::SELECTED:
            return palettes.fontSpriteSelected;
        case Ink::INK:
            break;
    }
    return palettes.fontSprite;
}

// Where a character's art is: a cell of the font, a cell of the extras sheet, or nowhere.
[[nodiscard]] std::optional<ResolvedTile> glyphArt(char c, Ink ink, const TileAtlas& atlas,
                                                   std::uint8_t ramp) noexcept {
    const retropp::PaletteId palette = fontPaletteFor(ink, atlas, ramp);
    if (c == '*') {
        return ResolvedTile{.atlas = atlas.glyphExtras, .cell = kAsteriskCell, .palette = palette};
    }
    const std::optional<std::uint8_t> glyph = glyphIndexFor(c);
    if (!glyph) {
        return std::nullopt;
    }
    ResolvedTile art = resolveSpriteTile(*glyph, TileSheet::GAMEPLAY, /*palette1=*/false, atlas, ramp);
    art.palette      = palette;
    return art;
}

// Every form: the run through one of the ramp's font object palettes.
Sprites glyphRun(std::string_view text, int x, int y, int pitch, const TileAtlas& atlas,
                 std::uint8_t ramp, Ink ink) {
    Sprites out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::optional<ResolvedTile> found = glyphArt(text[i], ink, atlas, ramp);
        if (!found) continue;

        const int          at  = x + static_cast<int>(i) * pitch;
        const ResolvedTile art = *found;
        out.push_back(retropp::Sprite{
            .key     = retropp::ObjectKey{"glyph-" + std::to_string(at) + "-" + std::to_string(y)},
            .x       = at,
            .y       = y,
            .z       = kGlyphZ,
            .atlas   = art.atlas,
            .tile    = art.cell,
            .palette = art.palette,
        });
    }
    return out;
}

}  // namespace

Sprites Glyphs(std::string_view text, int x, int y, int pitch, const TileAtlas& atlas,
               std::uint8_t ramp) {
    return glyphRun(text, x, y, pitch, atlas, ramp, Ink::INK);
}

Sprites DimGlyphs(std::string_view text, int x, int y, int pitch, const TileAtlas& atlas,
                  std::uint8_t ramp) {
    return glyphRun(text, x, y, pitch, atlas, ramp, Ink::DIM);
}

Sprites SelectedGlyphs(std::string_view text, int x, int y, int pitch, const TileAtlas& atlas,
                       std::uint8_t ramp) {
    return glyphRun(text, x, y, pitch, atlas, ramp, Ink::SELECTED);
}

std::vector<std::string_view> wrapText(std::string_view text, std::size_t width) {
    std::vector<std::string_view> lines;
    std::size_t                   start = 0;

    while (start < text.size()) {
        if (text.size() - start <= width) {
            lines.push_back(text.substr(start));
            break;
        }
        std::size_t cut = text.rfind(' ', start + width);
        if (cut == std::string_view::npos || cut <= start) {
            cut = start + width;  // one long word: let it take the whole line
        }
        lines.push_back(text.substr(start, cut - start));
        start = cut < text.size() && text[cut] == ' ' ? cut + 1 : cut;
    }
    return lines;
}

}  // namespace kirpich::render
