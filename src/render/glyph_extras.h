#pragma once

// Glyphs the font lacks, drawn here and uploaded as a sheet of their own.
//
// The game's font has letters, digits, a period and a hyphen. A glyph the port needs beyond those is
// an 8x8 picture in the art below, '#' for ink and '.' for nothing, and becomes an index plane in the
// font's own convention: index 0 is the ink and index 1 is the background, so a glyph drawn through
// a font palette inks exactly as a letter does. uploadTileAtlas uploads the sheet once
// (TileAtlas::glyphExtras); Glyphs draws a character from it the way it draws one from the font.
//
// The sheet is the glyphs side by side, one 8x8 cell each, cell `i` at column `i * kGlyphExtraSide`:
//
//   cell 0  the asterisk, which a screen's title carries while what the screen edits is unsaved;
//   cell 1  the colon, which stands between a label and its value, as in the rotation editor's
//           `0: 12`.
//
// To add a glyph: draw its art here, give it the next cell, raise kGlyphExtraCount, add it to
// glyphExtraSheet, and map its character in render/components/glyphs.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kirpich::render {

inline constexpr int kGlyphExtraSide = 8;

// How many glyphs the sheet holds.
inline constexpr int kGlyphExtraCount = 2;

using GlyphArt = std::array<std::string_view, kGlyphExtraSide>;

inline constexpr GlyphArt kAsteriskArt{{
    "........",
    "...#....",
    ".#.#.#..",
    "..###...",
    ".#.#.#..",
    "...#....",
    "........",
    "........",
}};

inline constexpr GlyphArt kColonArt{{
    "........",
    "........",
    "...##...",
    "...##...",
    "........",
    "...##...",
    "...##...",
    "........",
}};

// Each glyph's cell in the sheet.
inline constexpr std::uint16_t kAsteriskCell = 0;
inline constexpr std::uint16_t kColonCell    = 1;

// One glyph's index plane, row by row: 0 where the art has '#', 1 elsewhere.
using GlyphPlane = std::array<std::uint8_t, kGlyphExtraSide * kGlyphExtraSide>;

[[nodiscard]] constexpr GlyphPlane glyphIndices(const GlyphArt& art) noexcept {
    GlyphPlane plane{};
    for (std::size_t row = 0; row < art.size(); ++row) {
        for (std::size_t col = 0; col < art[row].size(); ++col) {
            plane[row * kGlyphExtraSide + col] = art[row][col] == '#' ? 0 : 1;
        }
    }
    return plane;
}

[[nodiscard]] constexpr GlyphPlane asteriskIndices() noexcept {
    return glyphIndices(kAsteriskArt);
}

[[nodiscard]] constexpr GlyphPlane colonIndices() noexcept {
    return glyphIndices(kColonArt);
}

// The sheet's width and height in pixels, and the whole sheet as one index plane, row by row: every
// glyph's plane side by side, cell `i` starting at column `i * kGlyphExtraSide`.
inline constexpr int kGlyphExtraSheetWidth  = kGlyphExtraCount * kGlyphExtraSide;
inline constexpr int kGlyphExtraSheetHeight = kGlyphExtraSide;

using GlyphSheet = std::array<std::uint8_t, kGlyphExtraSheetWidth * kGlyphExtraSheetHeight>;

[[nodiscard]] constexpr GlyphSheet glyphExtraSheet() noexcept {
    const std::array<GlyphPlane, kGlyphExtraCount> planes{asteriskIndices(), colonIndices()};
    GlyphSheet sheet{};
    for (std::size_t cell = 0; cell < planes.size(); ++cell) {
        for (std::size_t row = 0; row < static_cast<std::size_t>(kGlyphExtraSide); ++row) {
            for (std::size_t col = 0; col < static_cast<std::size_t>(kGlyphExtraSide); ++col) {
                sheet[row * kGlyphExtraSheetWidth + cell * kGlyphExtraSide + col] =
                    planes[cell][row * kGlyphExtraSide + col];
            }
        }
    }
    return sheet;
}

static_assert([] {
    for (const GlyphArt& art : {kAsteriskArt, kColonArt}) {
        for (const std::string_view row : art) {
            if (row.size() != kGlyphExtraSide) return false;
            for (const char c : row) {
                if (c != '#' && c != '.') return false;
            }
        }
    }
    return true;
}(), "every row of a glyph's art is eight characters of '#' and '.'");

static_assert(kColonCell + 1 == kGlyphExtraCount, "every glyph has a cell inside the sheet");

}  // namespace kirpich::render
