// Miscellaneous data: the raw-OAM object tables, cursor coordinate tables, win-screen strings, and
// the demo/completed-row constants.
//
// The engine tables (src/data/misc.h) are swept in full against the parser-emitted fixture
// (tests/fixtures/misc_expected.h): the fixture holds the ROM's raw bytes, and the sweeps here
// re-derive each OamObject / SpriteCoordinate / string byte from those bytes and compare to the
// generated arrays, so a defect in either the typed surface or the parser cannot hide. The remaining
// tests pin the music-type index math, the PauseText charmap encoding (double-entry against the
// port's own encoder), the two byte-equivalence sizes, and the corpus corner cases. Expectations come
// from docs/contracts/misc.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include <kirpich/char_tile.h>
#include <kirpich/music_type.h>

#include "data/charmap.h"
#include "data/misc.h"
#include "fixtures/misc_expected.h"

namespace {

using kirpich::MusicType;
using kirpich::OamObject;
using kirpich::SpriteCoordinate;
using kirpich::fixtures::kExpectedMiscCoordBytes;
using kirpich::fixtures::kExpectedMiscCoordTables;
using kirpich::fixtures::kExpectedMiscOamBytes;
using kirpich::fixtures::kExpectedMiscOamTables;
using kirpich::fixtures::kExpectedMiscTextBytes;
using kirpich::fixtures::kExpectedMiscTextStrings;
using kirpich::fixtures::kExpectedPauseTextBytes;

// The OAM tables in kExpectedMiscOamTables order, so the sweep can walk them uniformly.
const std::array<std::span<const OamObject>, 4> kOamTables{{
    kirpich::kMarioLuigiFaceObjects,
    kirpich::kMarioFaceObjects,
    kirpich::kLuigiFaceObjects,
    kirpich::kPushStartObjects,
}};

// The coordinate tables in kExpectedMiscCoordTables order.
const std::array<std::span<const SpriteCoordinate>, 6> kCoordTables{{
    kirpich::kTypeALevelCursorCoordinates,
    kirpich::kTypeBLevelCursorCoordinates,
    kirpich::kTypeBStartHeightCursorCoordinates,
    kirpich::kMarioStartHeightCursorCoordinates,
    kirpich::kLuigiStartHeightCursorCoordinates,
    kirpich::kMusicTypeSpriteCoordinates,
}};

// The four win-screen strings in kExpectedMiscTextStrings order.
const std::array<std::span<const std::uint8_t>, 4> kTextTables{{
    kirpich::kDeuceText,
    kirpich::kMarioWinsText,
    kirpich::kLuigiWinsText,
    kirpich::kAdvantageText,
}};

TEST(Misc, MiscConstantsPins) {
    // The three scalars, against the fixture and their literal contract values.
    EXPECT_EQ(kirpich::kDemoRecordingEnabledMagic, kirpich::fixtures::kExpectedDemoRecordingMagic);
    EXPECT_EQ(kirpich::kDemoRecordingEnabledMagic, 0xFF);
    EXPECT_EQ(kirpich::kCompletedRowCheckFirstRow, kirpich::fixtures::kExpectedCompletedRowCheckFirstRow);
    EXPECT_EQ(kirpich::kCompletedRowCheckFirstRow, 2);
    EXPECT_EQ(kirpich::kCompletedRowCheckRowCount, kirpich::fixtures::kExpectedCompletedRowCheckRowCount);
    EXPECT_EQ(kirpich::kCompletedRowCheckRowCount, 16);
    // The quirk invariant: the scan skips exactly the top two rows (2 + 16 == 18 field rows).
    EXPECT_EQ(kirpich::kCompletedRowCheckFirstRow + kirpich::kCompletedRowCheckRowCount, 18);

    // Byte-equivalence: each typed record is exactly its ROM record width.
    EXPECT_EQ(sizeof(OamObject), 4u);
    EXPECT_EQ(sizeof(SpriteCoordinate), 2u);

    // Table / string sizes.
    EXPECT_EQ(kirpich::kMarioLuigiFaceObjects.size(), 8u);
    EXPECT_EQ(kirpich::kMarioFaceObjects.size(), 4u);
    EXPECT_EQ(kirpich::kLuigiFaceObjects.size(), 4u);
    EXPECT_EQ(kirpich::kPushStartObjects.size(), 9u);
    EXPECT_EQ(kirpich::kTypeALevelCursorCoordinates.size(), 10u);
    EXPECT_EQ(kirpich::kTypeBLevelCursorCoordinates.size(), 10u);
    EXPECT_EQ(kirpich::kTypeBStartHeightCursorCoordinates.size(), 6u);
    EXPECT_EQ(kirpich::kMarioStartHeightCursorCoordinates.size(), 6u);
    EXPECT_EQ(kirpich::kLuigiStartHeightCursorCoordinates.size(), 6u);
    EXPECT_EQ(kirpich::kMusicTypeSpriteCoordinates.size(), 4u);
    EXPECT_EQ(kirpich::kDeuceText.size(), 6u);
    EXPECT_EQ(kirpich::kMarioWinsText.size(), 11u);
    EXPECT_EQ(kirpich::kLuigiWinsText.size(), 11u);
    EXPECT_EQ(kirpich::kAdvantageText.size(), 9u);
    EXPECT_EQ(kirpich::kPauseText.size(), 5u);
}

// The moment-of-truth for Section A: every object of every OAM table, re-derived from the raw fixture
// bytes {y, x, tile, attr} and compared to the accessor. 4 tables / 25 objects.
TEST(Misc, OamObjectSweep) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kExpectedMiscOamTables.size(); ++i) {
        const auto& slice = kExpectedMiscOamTables[i];
        const std::span<const OamObject> table = kOamTables[i];
        ASSERT_EQ(table.size(), slice.count) << "oam table " << i;
        for (std::size_t r = 0; r < slice.count; ++r) {
            const std::uint8_t* b = &kExpectedMiscOamBytes[slice.byte_offset + r * 4];
            const std::uint8_t attr = b[3];
            EXPECT_TRUE(attr == 0x00 || attr == 0x20)
                << "table " << i << " obj " << r << " attr 0x" << std::hex << int(attr);
            const OamObject expected{ .y = b[0], .x = b[1], .tile = b[2], .xflip = (attr == 0x20) };
            EXPECT_EQ(table[r], expected) << "table " << i << " obj " << r;
            ++total;
        }
    }
    EXPECT_EQ(total, 25u);
}

// The moment-of-truth for Section B: every pair of every coordinate table vs the raw fixture bytes.
// 6 tables / 42 pairs.
TEST(Misc, CursorCoordinateSweep) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kExpectedMiscCoordTables.size(); ++i) {
        const auto& slice = kExpectedMiscCoordTables[i];
        const std::span<const SpriteCoordinate> table = kCoordTables[i];
        ASSERT_EQ(table.size(), slice.count) << "coord table " << i;
        for (std::size_t r = 0; r < slice.count; ++r) {
            const std::uint8_t* b = &kExpectedMiscCoordBytes[slice.byte_offset + r * 2];
            const SpriteCoordinate expected{ .y = b[0], .x = b[1] };
            EXPECT_EQ(table[r], expected) << "coord table " << i << " pair " << r;
            ++total;
        }
    }
    EXPECT_EQ(total, 42u);
}

// The music-type accessor's -$1C index math, over all four enumerators.
TEST(Misc, MusicTypeCoordinateLink) {
    const std::array<MusicType, 4> kTypes{{
        MusicType::MUSIC_A, MusicType::MUSIC_B, MusicType::MUSIC_C, MusicType::OFF,
    }};
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
        EXPECT_EQ(kirpich::musicTypeSpriteCoordinate(kTypes[i]), kirpich::kMusicTypeSpriteCoordinates[i])
            << "music type index " << i;
    }
    // A concrete pin on the resolved pair (the fourth entry).
    const SpriteCoordinate off{ .y = 128, .x = 119 };
    EXPECT_EQ(kirpich::musicTypeSpriteCoordinate(MusicType::OFF), off);
}

// Section C: the four win strings against the fixture, and PauseText by double entry (fixture bytes
// AND the port's own charmap encoder must agree with the typed CharTile array).
TEST(Misc, TextSweep) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kExpectedMiscTextStrings.size(); ++i) {
        const auto& slice = kExpectedMiscTextStrings[i];
        const std::span<const std::uint8_t> str = kTextTables[i];
        ASSERT_EQ(str.size(), slice.count) << "string " << i;
        for (std::size_t r = 0; r < slice.count; ++r) {
            EXPECT_EQ(str[r], kExpectedMiscTextBytes[slice.byte_offset + r]) << "string " << i << " byte " << r;
            ++total;
        }
    }
    EXPECT_EQ(total, 37u);

    // PauseText vs the raw fixture bytes.
    ASSERT_EQ(kirpich::kPauseText.size(), kExpectedPauseTextBytes.size());
    for (std::size_t i = 0; i < kirpich::kPauseText.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(kirpich::kPauseText[i]), kExpectedPauseTextBytes[i])
            << "pause byte " << i;
    }
    // PauseText vs the port-side greedy charmap encoder (the same character-map table).
    const auto encoded = kirpich::encodeCharmapText("pause");
    ASSERT_TRUE(encoded.has_value());
    ASSERT_EQ(encoded->size(), kirpich::kPauseText.size());
    for (std::size_t i = 0; i < encoded->size(); ++i) {
        EXPECT_EQ((*encoded)[i], kirpich::kPauseText[i]) << "pause encode " << i;
    }
}

TEST(Misc, MiscCornerPins) {
    // First/last object of each OAM table.
    EXPECT_EQ(kirpich::kMarioLuigiFaceObjects.front(),
              (OamObject{ .y = 64, .x = 40, .tile = 0xAE, .xflip = false }));
    EXPECT_EQ(kirpich::kMarioLuigiFaceObjects.back(),
              (OamObject{ .y = 128, .x = 48, .tile = 0xC1, .xflip = true }));
    EXPECT_EQ(kirpich::kMarioFaceObjects.front(),
              (OamObject{ .y = 24, .x = 132, .tile = 0xAE, .xflip = false }));
    EXPECT_EQ(kirpich::kLuigiFaceObjects.front(),
              (OamObject{ .y = 24, .x = 132, .tile = 0xC0, .xflip = false }));
    EXPECT_EQ(kirpich::kPushStartObjects.front(),
              (OamObject{ .y = 66, .x = 48, .tile = 0x0D, .xflip = false }));
    EXPECT_EQ(kirpich::kPushStartObjects.back(),
              (OamObject{ .y = 66, .x = 120, .tile = 0x1D, .xflip = false }));

    // PUSH START: every object shares y = 66 and none is x-flipped.
    for (const OamObject& o : kirpich::kPushStartObjects) {
        EXPECT_EQ(o.y, 66);
        EXPECT_FALSE(o.xflip);
    }

    // x-flip appears only on the right half of each face pair (the odd index).
    for (std::size_t i = 0; i < kirpich::kMarioLuigiFaceObjects.size(); ++i) {
        EXPECT_EQ(kirpich::kMarioLuigiFaceObjects[i].xflip, (i % 2 == 1)) << "mario/luigi face obj " << i;
    }
    for (std::size_t i = 0; i < kirpich::kMarioFaceObjects.size(); ++i) {
        EXPECT_EQ(kirpich::kMarioFaceObjects[i].xflip, (i % 2 == 1)) << "mario face obj " << i;
        EXPECT_EQ(kirpich::kLuigiFaceObjects[i].xflip, (i % 2 == 1)) << "luigi face obj " << i;
    }

    // Boundary coordinate pairs across the six tables.
    EXPECT_EQ(kirpich::kTypeALevelCursorCoordinates.front(), (SpriteCoordinate{ .y = 64, .x = 48 }));
    EXPECT_EQ(kirpich::kTypeALevelCursorCoordinates.back(), (SpriteCoordinate{ .y = 80, .x = 112 }));
    EXPECT_EQ(kirpich::kTypeBLevelCursorCoordinates.front(), (SpriteCoordinate{ .y = 64, .x = 24 }));
    EXPECT_EQ(kirpich::kTypeBStartHeightCursorCoordinates.back(), (SpriteCoordinate{ .y = 80, .x = 144 }));
    EXPECT_EQ(kirpich::kLuigiStartHeightCursorCoordinates.front(), (SpriteCoordinate{ .y = 120, .x = 96 }));
    EXPECT_EQ(kirpich::kMusicTypeSpriteCoordinates.back(), (SpriteCoordinate{ .y = 128, .x = 119 }));
}

}  // namespace
