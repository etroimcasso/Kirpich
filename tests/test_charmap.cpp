// Charmap: the character-sequence -> tile-index table (charmap.asm), its exact-sequence lookup,
// and the RGBDS-style greedy-longest-match text encoder.
//
// The engine table (kirpich::getCharmap) is swept in full against the parser-emitted fixture
// (tests/fixtures/charmap_expected.h) so a defect in either can't hide. Sequences that are not
// printable ASCII are written here as \xHH byte escapes (matching the emitted table), so this file
// is pure ASCII and encodes identically under every CI toolchain.

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <kirpich/charmap.h>

#include "data/charmap.h"
#include "fixtures/charmap_expected.h"

namespace {

using kirpich::CharmapEntry;
using kirpich::encodeCharmapText;
using kirpich::getCharmap;
using kirpich::getCharmapTile;

using Tiles = std::vector<std::uint8_t>;

// The multi-byte sequences, spelled as their exact UTF-8 bytes (ASCII-only source).
constexpr std::string_view kMultSign = "\xC3\x97";          // U+00D7
constexpr std::string_view kRightDblQuote = "\xE2\x80\x9D"; // U+201D
constexpr std::string_view kLigature = ".\xE2\x80\x9D";     // "." + U+201D -> $9D

// 1. Full 47-row sweep: engine table equals the parser-emitted fixture, field for field.
TEST(Charmap, CharmapMatchesFixture) {
    const std::span<const CharmapEntry> table = getCharmap();
    ASSERT_EQ(table.size(), kirpich::fixtures::kCharmapExpected.size());
    ASSERT_EQ(table.size(), 47u);

    std::set<std::string_view> sequences;
    std::set<std::uint8_t> tiles;
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i].sequence, kirpich::fixtures::kCharmapExpected[i].sequence) << "row " << i;
        EXPECT_EQ(table[i].tile, kirpich::fixtures::kCharmapExpected[i].tile) << "row " << i;
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
        EXPECT_EQ(*tile, static_cast<std::uint8_t>(0x00 + i)) << "digit " << key;
    }
    for (int i = 0; i < 26; ++i) {
        const std::string key(1, static_cast<char>('a' + i));
        const auto tile = getCharmapTile(key);
        ASSERT_TRUE(tile.has_value()) << "letter " << key;
        EXPECT_EQ(*tile, static_cast<std::uint8_t>(0x0A + i)) << "letter " << key;
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
    EXPECT_EQ(encodeCharmapText(kLigature), std::optional<Tiles>(Tiles{0x9D}));
    EXPECT_EQ(encodeCharmapText("."), std::optional<Tiles>(Tiles{0x24}));
    EXPECT_EQ(encodeCharmapText(kRightDblQuote), std::optional<Tiles>(Tiles{0x9B}));

    // "pazhitnov.”" (tetris.asm:7002) ends in the single ligature tile $9D, not $24 $9B.
    const std::string name = "pazhitnov" + std::string(kLigature);
    const auto encoded = encodeCharmapText(name);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, (Tiles{0x19, 0x0A, 0x23, 0x11, 0x12, 0x1D, 0x17, 0x18, 0x1F, 0x9D}));
    ASSERT_EQ(encoded->size(), 10u);          // 9 letters + one ligature tile, not 11
    EXPECT_EQ(encoded->back(), 0x9D);
    EXPECT_EQ((*encoded)[encoded->size() - 2], 0x1F);
}

// 5. Known upstream strings encode to their exact tile runs.
TEST(Charmap, EncodeKnownStrings) {
    EXPECT_EQ(encodeCharmapText("pause"),
              std::optional<Tiles>(Tiles{0x19, 0x0A, 0x1E, 0x1C, 0x0E}));  // tetris.asm:4575

    // " 0 <U+00D7> 40   " (tetris.asm:6491): spaces, digit, multiplication sign, digits, spaces.
    const std::string line = " 0 " + std::string(kMultSign) + " 40   ";
    EXPECT_EQ(encodeCharmapText(line),
              std::optional<Tiles>(Tiles{0x2F, 0x00, 0x2F, 0x26, 0x2F, 0x04, 0x00,
                                         0x2F, 0x2F, 0x2F}));
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

}  // namespace
