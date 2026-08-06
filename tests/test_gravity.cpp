// Gravity: the per-level drop interval table and the lookup that picks an interval.
//
// The engine table (src/data/gravity.h) is swept in full against the parser-emitted fixture
// (tests/fixtures/gravity_expected.h), which holds the same values as raw bytes so a defect in the
// typed surface can't hide. Both lookup modes are then swept across their whole domain — every level
// 0-20 in normal mode, and every level 0-20 in heart mode, which pins the level shift and the cap
// level by level. Expectations come from docs/contracts/gravity.md.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/gravity.h"
#include "fixtures/gravity_expected.h"

namespace {

using kirpich::framesPerDrop;
using kirpich::kFramesPerDrop;
using kirpich::kHeartModeLevelBoost;
using kirpich::kMaxLevel;
using kirpich::fixtures::kExpectedFramesPerDrop;

// 1. Full corpus sweep: all 21 rows equal the fixture, and each row sits at the level it names.
TEST(Gravity, SweepAgainstFixture) {
    static_assert(kFramesPerDrop.size() == kExpectedFramesPerDrop.size());

    for (std::size_t level = 0; level < kFramesPerDrop.size(); ++level) {
        EXPECT_EQ(kFramesPerDrop[level].frames, kExpectedFramesPerDrop[level])
            << "frames at level " << level;
        EXPECT_EQ(kFramesPerDrop[level].level, static_cast<std::uint8_t>(level))
            << "level field at row " << level;
    }
}

// 2. The values the contract pins by name: the two annotated ends and the level 9 -> 10 cliff.
TEST(Gravity, BoundaryPins) {
    EXPECT_EQ(kFramesPerDrop[0].frames, 52);
    EXPECT_EQ(kFramesPerDrop[9].frames, 10);
    EXPECT_EQ(kFramesPerDrop[10].frames, 9);
    EXPECT_EQ(kFramesPerDrop[20].frames, 2);

    EXPECT_EQ(kMaxLevel, 20);
    EXPECT_EQ(kFramesPerDrop.size(), std::size_t{kMaxLevel} + 1);

    // Gravity never slows down as the level rises.
    for (std::size_t level = 1; level < kFramesPerDrop.size(); ++level) {
        EXPECT_LE(kFramesPerDrop[level].frames, kFramesPerDrop[level - 1].frames)
            << "interval grew at level " << level;
    }
}

// 3. Normal mode: the level indexes the table directly, with no shift and no cap.
TEST(Gravity, LookupNormalModeFullDomain) {
    for (std::uint8_t level = 0; level <= kMaxLevel; ++level) {
        EXPECT_EQ(framesPerDrop(level, false), kExpectedFramesPerDrop[level])
            << "normal mode at level " << static_cast<int>(level);
    }
}

// 4. Heart mode: the lookup shifts up kHeartModeLevelBoost levels and stops at kMaxLevel. Levels
//    0-10 pin the shift; 11-20 pin the cap.
TEST(Gravity, LookupHeartModeFullDomain) {
    for (std::uint8_t level = 0; level <= kMaxLevel; ++level) {
        const std::size_t boosted = std::size_t{level} + kHeartModeLevelBoost;
        const std::size_t expected_index = boosted > kMaxLevel ? kMaxLevel : boosted;

        EXPECT_EQ(framesPerDrop(level, true), kExpectedFramesPerDrop[expected_index])
            << "heart mode at level " << static_cast<int>(level);
    }

    // The cap's boundary: a boosted level of exactly kMaxLevel passes through, and every level past
    // it lands on the same row, so the fastest interval is reached at level 10 and held.
    EXPECT_EQ(framesPerDrop(10, true), kExpectedFramesPerDrop[kMaxLevel]);
    EXPECT_EQ(framesPerDrop(11, true), kExpectedFramesPerDrop[kMaxLevel]);
    EXPECT_EQ(framesPerDrop(kMaxLevel, true), kExpectedFramesPerDrop[kMaxLevel]);
}

}  // namespace
