// Scoring: the award/bonus tables, the transcribed constants, and the four scoring functions.
//
// The engine tables (src/data/scoring.h) are swept in full against the parser-emitted fixture
// (tests/fixtures/scoring_expected.h), which holds the ROM's wire bytes - the tests decode the
// BCD here themselves, so every expected value flows from the ROM spelling rather than being
// hand-typed. The four functions are then swept across their whole domains: all kinds x levels
// for the award, every tier edge for the rockets, the full 10000 x 21 grid for the level-up
// rule, and the field-height range for the soft-drop quirk. Expectations come from
// docs/contracts/scoring.md.

#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include "data/scoring.h"
#include "fixtures/scoring_expected.h"

namespace {

using kirpich::kBonusEndings;
using kirpich::kLevelCap;
using kirpich::kLineClearScores;
using kirpich::kLinesPerLevel;
using kirpich::kScoreSaturation;
using kirpich::kSoftDropPointsPerRow;
using kirpich::kTypeBLineGoal;
using kirpich::LineClearKind;
using kirpich::lineClearAward;
using kirpich::rocketSpriteForScore;
using kirpich::shouldLevelUp;
using kirpich::softDropAward;
using kirpich::fixtures::kExpectedBonusThresholdBcd;
using kirpich::fixtures::kExpectedLevelCapByte;
using kirpich::fixtures::kExpectedLineClearScoreBcd;
using kirpich::fixtures::kExpectedRocketSprites;
using kirpich::fixtures::kExpectedSaturationByte;
using kirpich::fixtures::kExpectedSoftDropOperand;
using kirpich::fixtures::kExpectedTypeBLineGoalBcd;

// Reads a BCD wire value as the decimal number its hex digits spell - the fixture-side decode
// the sweeps compare the typed surface against.
constexpr std::uint32_t bcdDecode(std::uint32_t wire, int nibbles) {
    std::uint32_t value = 0;
    for (int shift = nibbles - 1; shift >= 0; --shift) {
        value = value * 10 + ((wire >> (shift * 4)) & 0xF);
    }
    return value;
}
static_assert(bcdDecode(0x1200, 4) == 1200);
static_assert(bcdDecode(0x25, 2) == 25);

// 1. Full corpus sweep: all 4 award rows equal the fixture's decode, each at the kind it names.
TEST(Scoring, LineClearScoreSweepAgainstFixture) {
    static_assert(kLineClearScores.size() == kExpectedLineClearScoreBcd.size());

    for (std::size_t kind = 0; kind < kLineClearScores.size(); ++kind) {
        EXPECT_EQ(kLineClearScores[kind].points, bcdDecode(kExpectedLineClearScoreBcd[kind], 4))
            << "points at kind " << kind;
        EXPECT_EQ(kLineClearScores[kind].kind, static_cast<LineClearKind>(kind))
            << "kind field at row " << kind;
    }
}

// 2. Full corpus sweep: all 3 bonus tiers equal the fixture - threshold decoded from the score's
//    top byte (x 10000, its place value), sprite byte verbatim - in strictly descending order.
TEST(Scoring, BonusEndingSweepAgainstFixture) {
    static_assert(kBonusEndings.size() == kExpectedBonusThresholdBcd.size());
    static_assert(kBonusEndings.size() == kExpectedRocketSprites.size());

    for (std::size_t tier = 0; tier < kBonusEndings.size(); ++tier) {
        EXPECT_EQ(kBonusEndings[tier].min_score,
                  bcdDecode(kExpectedBonusThresholdBcd[tier], 2) * 10000u)
            << "threshold at tier " << tier;
        EXPECT_EQ(kBonusEndings[tier].rocket_sprite, kExpectedRocketSprites[tier])
            << "sprite at tier " << tier;
    }
    for (std::size_t tier = 1; tier < kBonusEndings.size(); ++tier) {
        EXPECT_LT(kBonusEndings[tier].min_score, kBonusEndings[tier - 1].min_score)
            << "tiers must descend at tier " << tier;
    }
}

// 3. The transcribed constants, pinned by value and tied back to their wire bytes.
TEST(Scoring, ScoringConstantsPins) {
    EXPECT_EQ(kLevelCap, 20);
    EXPECT_EQ(kLevelCap, kExpectedLevelCapByte);  // hLevel is plain binary: $14 IS 20

    EXPECT_EQ(kTypeBLineGoal, 25);
    EXPECT_EQ(kTypeBLineGoal, bcdDecode(kExpectedTypeBLineGoalBcd, 2));

    EXPECT_EQ(kSoftDropPointsPerRow, 1);
    EXPECT_EQ(kSoftDropPointsPerRow, bcdDecode(kExpectedSoftDropOperand, 4));

    // The saturation byte fills all three score bytes: 99 in the 1s, 100s, and 10000s places.
    EXPECT_EQ(kScoreSaturation, 999999u);
    EXPECT_EQ(kScoreSaturation, bcdDecode(kExpectedSaturationByte, 2) * 10101u);

    EXPECT_EQ(kLinesPerLevel, 10);
}

// 4. Award math across the whole domain: every kind at every level is base x (level + 1),
//    with the corner values pinned by name.
TEST(Scoring, LineClearAwardFullDomain) {
    for (std::size_t kind = 0; kind < kLineClearScores.size(); ++kind) {
        const std::uint32_t base = bcdDecode(kExpectedLineClearScoreBcd[kind], 4);
        for (std::uint8_t level = 0; level <= kLevelCap; ++level) {
            EXPECT_EQ(lineClearAward(static_cast<LineClearKind>(kind), level),
                      base * (std::uint32_t{level} + 1))
                << "kind " << kind << " at level " << static_cast<int>(level);
        }
    }

    EXPECT_EQ(lineClearAward(LineClearKind::SINGLE, 0), 40u);       // the smallest award
    EXPECT_EQ(lineClearAward(LineClearKind::TETRIS, kLevelCap), 25200u);  // the largest
}

// 5. Rocket selection at every tier edge: one below and the first score of each tier, plus the
//    floor and the saturated ceiling.
TEST(Scoring, RocketSpriteFullBoundarySet) {
    EXPECT_EQ(rocketSpriteForScore(0), std::nullopt);
    EXPECT_EQ(rocketSpriteForScore(99999), std::nullopt);
    EXPECT_EQ(rocketSpriteForScore(100000), kExpectedRocketSprites[2]);
    EXPECT_EQ(rocketSpriteForScore(149999), kExpectedRocketSprites[2]);
    EXPECT_EQ(rocketSpriteForScore(150000), kExpectedRocketSprites[1]);
    EXPECT_EQ(rocketSpriteForScore(199999), kExpectedRocketSprites[1]);
    EXPECT_EQ(rocketSpriteForScore(200000), kExpectedRocketSprites[0]);
    EXPECT_EQ(rocketSpriteForScore(kScoreSaturation), kExpectedRocketSprites[0]);
}

// 6. The level-up rule across its entire input domain - every line count the four-digit counter
//    can hold at every reachable level - against an independent restatement of the contract:
//    floor(lines / 10) must exceed the level, below the cap, below 1000 lines.
TEST(Scoring, ShouldLevelUpFullDomain) {
    for (std::uint16_t lines = 0; lines <= 9999; ++lines) {
        for (std::uint8_t level = 0; level <= kLevelCap; ++level) {
            const bool expected = lines < 1000 && level < 20 && lines / 10 > level;
            ASSERT_EQ(shouldLevelUp(lines, level), expected)
                << "lines " << lines << " at level " << static_cast<int>(level);
        }
    }

    EXPECT_TRUE(shouldLevelUp(999, 0));    // just under the cutoff still levels
    EXPECT_FALSE(shouldLevelUp(1000, 0));  // the thousands-digit quirk: never again
    EXPECT_TRUE(shouldLevelUp(100, 9));    // floor(100/10) = 10 > 9
    EXPECT_FALSE(shouldLevelUp(100, 10));  // ... but not past 10 itself
    EXPECT_FALSE(shouldLevelUp(999, kLevelCap));  // the cap is terminal even under the cutoff
}

// 7. The soft-drop award across the field-height domain: one point per row minus one - the
//    original's own preserved off-by-one - and zero for zero rows.
TEST(Scoring, SoftDropAwardDomain) {
    EXPECT_EQ(softDropAward(0), 0u);  // the original never awards a zero counter; no-op here too
    EXPECT_EQ(softDropAward(1), 0u);  // the minus-one quirk: one row scores nothing
    for (std::uint8_t rows = 1; rows <= 18; ++rows) {
        EXPECT_EQ(softDropAward(rows), std::uint32_t{rows} - 1u)
            << "rows " << static_cast<int>(rows);
    }
}

}  // namespace
