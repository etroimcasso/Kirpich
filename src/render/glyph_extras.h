#pragma once

// Glyphs the font lacks, drawn here and uploaded as a sheet of their own.
//
// The game's font has letters, digits, a period and a hyphen. A glyph the port needs beyond those is
// an 8x8 picture in the art below, '#' for ink and '.' for nothing, and becomes an index plane in the
// font's own convention: index 0 is the ink and index 1 is the background, so a glyph drawn through
// a font palette inks exactly as a letter does. uploadTileAtlas uploads the plane once
// (TileAtlas::glyphExtras); Glyphs draws a character from it the way it draws one from the font.
//
// Cell 0 is the asterisk, which a screen's title carries while what the screen edits is unsaved.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kirpich::render {

inline constexpr int kGlyphExtraSide = 8;

inline constexpr std::array<std::string_view, kGlyphExtraSide> kAsteriskArt{{
    "........",
    "...#....",
    ".#.#.#..",
    "..###...",
    ".#.#.#..",
    "...#....",
    "........",
    "........",
}};

// The asterisk's cell in the extras sheet.
inline constexpr std::uint16_t kAsteriskCell = 0;

// The asterisk as an index plane, row by row: 0 where the art has '#', 1 elsewhere.
[[nodiscard]] constexpr std::array<std::uint8_t, kGlyphExtraSide * kGlyphExtraSide>
asteriskIndices() noexcept {
    std::array<std::uint8_t, kGlyphExtraSide * kGlyphExtraSide> plane{};
    for (std::size_t row = 0; row < kAsteriskArt.size(); ++row) {
        for (std::size_t col = 0; col < kAsteriskArt[row].size(); ++col) {
            plane[row * kGlyphExtraSide + col] = kAsteriskArt[row][col] == '#' ? 0 : 1;
        }
    }
    return plane;
}

static_assert([] {
    for (const std::string_view row : kAsteriskArt) {
        if (row.size() != kGlyphExtraSide) return false;
        for (const char c : row) {
            if (c != '#' && c != '.') return false;
        }
    }
    return true;
}(), "every row of a glyph's art is eight characters of '#' and '.'");

}  // namespace kirpich::render
