// Charmap: the character-sequence -> glyph table (charmap.asm), its exact-sequence lookup, and
// the RGBDS-style greedy-longest-match text encoder.
//
// The engine table (kirpich::getCharmap) is swept in full against the parser-emitted fixture
// (tests/fixtures/charmap_expected.h). The fixture keeps RAW tile bytes on purpose — the sweep
// casts each CharTile back to its byte, so a defect in the table, the entry type, or the CharTile
// enum values can't hide behind the type system. Sequences that are not printable ASCII are
// written here as \xHH byte escapes (matching the emitted table), so this file is pure ASCII and
// encodes identically under every CI toolchain.

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <kirpich/char_tile.h>
#include <kirpich/charmap.h>

#include "data/charmap.h"
#include "fixtures/charmap_expected.h"

namespace {

using kirpich::CharmapEntry;
using kirpich::CharTile;
using kirpich::encodeCharmapText;
using kirpich::getCharmap;
using kirpich::getCharmapTile;

using Tiles = std::vector<CharTile>;

constexpr std::uint8_t raw(CharTile tile) {
    return static_cast<std::uint8_t>(tile);
}

// The multi-byte sequences, spelled as their exact UTF-8 bytes (ASCII-only source).
constexpr std::string_view kMultSign = "\xC3\x97";          // U+00D7
constexpr std::string_view kRightDblQuote = "\xE2\x80\x9D"; // U+201D
constexpr std::string_view kLigature = ".\xE2\x80\x9D";     // "." + U+201D -> $9D

// 1. Full 47-row sweep: engine table equals the parser-emitted fixture, field for field. The
//    fixture's tiles are raw bytes, so this sweep also pins every CharTile enumerator's value.
TEST(Charmap, CharmapMatchesFixture) {
    const std::span<const CharmapEntry> table = getCharmap();
    ASSERT_EQ(table.size(), kirpich::fixtures::kCharmapExpected.size());
    ASSERT_EQ(table.size(), 47u);

    std::set<std::string_view> sequences;
    std::set<CharTile> tiles;
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i].sequence, kirpich::fixtures::kCharmapExpected[i].sequence) << "row " << i;
        EXPECT_EQ(raw(table[i].tile), kirpich::fixtures::kCharmapExpected[i].tile) << "row " << i;
        sequences.insert(table[i].sequence);
        tiles.insert(table[i].tile);
    }
    EXPECT_EQ(sequences.size(), 47u) << "all sequences must be unique";
    EXPECT_EQ(tiles.size(), 47u) << "all tile values must be unique";
}

// 2. Digit-identity ("0".."9" -> $00..$09) and the contiguous letter block ("a".."z" -> $0A..$23).
TEST(Charmap, CharmapDigitAndLetterIdentity) {
    for (int i = 0; i < 10; ++i) {
        const std::string key(1, static_cast<char>('0' + i));
        const auto tile = getCharmapTile(key);
        ASSERT_TRUE(tile.has_value()) << "digit " << key;
        EXPECT_EQ(raw(*tile), static_cast<std::uint8_t>(0x00 + i)) << "digit " << key;
    }
    for (int i = 0; i < 26; ++i) {
        const std::string key(1, static_cast<char>('a' + i));
        const auto tile = getCharmapTile(key);
        ASSERT_TRUE(tile.has_value()) << "letter " << key;
        EXPECT_EQ(raw(*tile), static_cast<std::uint8_t>(0x0A + i)) << "letter " << key;
    }
}

// 3. Every sequence is found by exact lookup; unmapped sequences miss.
TEST(Charmap, GetCharmapTileFindsEverySequence) {
    for (const CharmapEntry& entry : getCharmap()) {
        const auto tile = getCharmapTile(entry.sequence);
        ASSERT_TRUE(tile.has_value()) << "sequence not found: " << entry.sequence;
        EXPECT_EQ(*tile, entry.tile);
    }
    EXPECT_FALSE(getCharmapTile("A").has_value());   // uppercase is not mapped
    EXPECT_FALSE(getCharmapTile("!").has_value());   // unmapped punctuation
    EXPECT_FALSE(getCharmapTile("").has_value());    // empty sequence
}

// 4. Greedy longest match: the ".”" ligature wins over "." + ”".
TEST(Charmap, EncodeAppliesLongestMatch) {
    EXPECT_EQ(encodeCharmapText(kLigature),
              std::optional<Tiles>(Tiles{CharTile::PERIOD_RIGHT_DOUBLE_QUOTE}));
    EXPECT_EQ(encodeCharmapText("."), std::optional<Tiles>(Tiles{CharTile::PERIOD}));
    EXPECT_EQ(encodeCharmapText(kRightDblQuote),
              std::optional<Tiles>(Tiles{CharTile::RIGHT_DOUBLE_QUOTE}));

    // "pazhitnov.”" (tetris.asm:7002) ends in the single ligature glyph, not PERIOD + quote.
    const std::string name = "pazhitnov" + std::string(kLigature);
    const auto encoded = encodeCharmapText(name);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, (Tiles{CharTile::LETTER_P, CharTile::LETTER_A, CharTile::LETTER_Z,
                               CharTile::LETTER_H, CharTile::LETTER_I, CharTile::LETTER_T,
                               CharTile::LETTER_N, CharTile::LETTER_O, CharTile::LETTER_V,
                               CharTile::PERIOD_RIGHT_DOUBLE_QUOTE}));
    ASSERT_EQ(encoded->size(), 10u);          // 9 letters + one ligature glyph, not 11
    EXPECT_EQ(encoded->back(), CharTile::PERIOD_RIGHT_DOUBLE_QUOTE);
    EXPECT_EQ((*encoded)[encoded->size() - 2], CharTile::LETTER_V);
}

// 5. Known upstream strings encode to their exact glyph runs.
TEST(Charmap, EncodeKnownStrings) {
    EXPECT_EQ(encodeCharmapText("pause"),
              std::optional<Tiles>(Tiles{CharTile::LETTER_P, CharTile::LETTER_A,
                                         CharTile::LETTER_U, CharTile::LETTER_S,
                                         CharTile::LETTER_E}));  // tetris.asm:4575

    // " 0 <U+00D7> 40   " (tetris.asm:6491): spaces, digit, multiplication sign, digits, spaces.
    const std::string line = " 0 " + std::string(kMultSign) + " 40   ";
    EXPECT_EQ(encodeCharmapText(line),
              std::optional<Tiles>(Tiles{CharTile::SPACE, CharTile::DIGIT_0, CharTile::SPACE,
                                         CharTile::MULTIPLICATION_SIGN, CharTile::SPACE,
                                         CharTile::DIGIT_4, CharTile::DIGIT_0, CharTile::SPACE,
                                         CharTile::SPACE, CharTile::SPACE}));
}

// 6. All-or-nothing: any unmapped character makes the whole encode fail, no partial output.
TEST(Charmap, EncodeRejectsUnmappableInput) {
    EXPECT_FALSE(encodeCharmapText("A").has_value());
    EXPECT_FALSE(encodeCharmapText("pause!").has_value());
}

// 7. Empty input succeeds with an empty encoding.
TEST(Charmap, EncodeEmptyYieldsEmpty) {
    const auto encoded = encodeCharmapText("");
    ASSERT_TRUE(encoded.has_value());
    EXPECT_TRUE(encoded->empty());
}

// 8. CharTile boundary values, pinned by name against the contract.
TEST(Charmap, CharTileValuesMatchContract) {
    EXPECT_EQ(raw(CharTile::DIGIT_0), 0x00);
    EXPECT_EQ(raw(CharTile::DIGIT_9), 0x09);
    EXPECT_EQ(raw(CharTile::LETTER_A), 0x0A);
    EXPECT_EQ(raw(CharTile::LETTER_Z), 0x23);
    EXPECT_EQ(raw(CharTile::PERIOD), 0x24);
    EXPECT_EQ(raw(CharTile::MULTIPLICATION_SIGN), 0x26);
    EXPECT_EQ(raw(CharTile::SPACE), 0x2F);
    EXPECT_EQ(raw(CharTile::COPYRIGHT), 0x33);
    EXPECT_EQ(raw(CharTile::ELLIPSIS), 0x60);
    EXPECT_EQ(raw(CharTile::RIGHT_DOUBLE_QUOTE), 0x9B);
    EXPECT_EQ(raw(CharTile::COMMA), 0x9C);
    EXPECT_EQ(raw(CharTile::PERIOD_RIGHT_DOUBLE_QUOTE), 0x9D);
}

}  // namespace
