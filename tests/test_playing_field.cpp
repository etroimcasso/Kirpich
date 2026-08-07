// Playing field: the 18x10 geometry and the wipe schedule that redraws the field one row per frame.
//
// The engine's four geometry/domain constants (src/data/playing_field.h) and the counter->row
// mapping are swept in full against the parser-emitted fixture (tests/fixtures/
// playing_field_expected.h), which holds the original's raw address triples so the closed-form
// geometry is checked against source values rather than against the port's own re-derivation of
// them. Expectations come from docs/contracts/playing-field.md.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "data/playing_field.h"
#include "fixtures/playing_field_expected.h"

namespace {

using kirpich::kPlayingFieldCols;
using kirpich::kPlayingFieldRows;
using kirpich::kPlayingFieldWipeCounterFirst;
using kirpich::kPlayingFieldWipeCounterLast;
using kirpich::playingFieldRowForWipeCounter;
using kirpich::fixtures::kExpectedPlayingFieldWipes;

// The DMG base addresses and BG-map row stride the wipe routines use. Test-local and
// contract-sourced (docs/contracts/playing-field.md): the port itself carries none of these - they
// describe the original's memory map - so the sweep checks the fixture against the closed form the
// contract states, not against a copy of the port's own arithmetic.
constexpr std::uint16_t kVramBase = 0x9802;
constexpr std::uint16_t kWramBase = 0xC802;
constexpr std::uint16_t kRowStride = 0x20;

// 1. The constants match the contract, and the fixture spans the whole counter domain.
TEST(PlayingField, ConstantsMatchContract) {
    EXPECT_EQ(kPlayingFieldRows, 18);
    EXPECT_EQ(kPlayingFieldCols, 10);
    EXPECT_EQ(kPlayingFieldWipeCounterFirst, 2);
    EXPECT_EQ(kPlayingFieldWipeCounterLast, 19);

    EXPECT_EQ(kExpectedPlayingFieldWipes.size(), std::size_t{kPlayingFieldRows});
    EXPECT_EQ(kExpectedPlayingFieldWipes.size(),
              std::size_t{kPlayingFieldWipeCounterLast - kPlayingFieldWipeCounterFirst + 1});
}

// 2. Full-corpus sweep: every fixture row's addresses match the closed form, VRAM and WRAM advance
//    in lockstep, and the counters are the contiguous ascending run 2..19.
TEST(PlayingField, WipeFixtureSweepClosedForm) {
    for (std::size_t i = 0; i < kExpectedPlayingFieldWipes.size(); ++i) {
        const auto& row = kExpectedPlayingFieldWipes[i];

        EXPECT_EQ(row.counter, static_cast<std::uint8_t>(kPlayingFieldWipeCounterFirst + i))
            << "counter at fixture index " << i;

        const auto offset =
            static_cast<std::uint16_t>((kPlayingFieldWipeCounterLast - row.counter) * kRowStride);
        EXPECT_EQ(row.vram, static_cast<std::uint16_t>(kVramBase + offset))
            << "vram at counter " << static_cast<int>(row.counter);
        EXPECT_EQ(row.wram, static_cast<std::uint16_t>(kWramBase + offset))
            << "wram at counter " << static_cast<int>(row.counter);
        EXPECT_EQ(row.vram - kVramBase, row.wram - kWramBase)
            << "vram/wram stride mismatch at counter " << static_cast<int>(row.counter);
    }
}

// 3. The row mapping matches the fixture across the whole domain, decreasing as the counter rises
//    (the wipe walks bottom to top).
TEST(PlayingField, RowForWipeCounterMatchesFixture) {
    for (const auto& row : kExpectedPlayingFieldWipes) {
        const std::uint16_t expected_row = (row.vram - kVramBase) / kRowStride;
        EXPECT_EQ(playingFieldRowForWipeCounter(row.counter), expected_row)
            << "row for counter " << static_cast<int>(row.counter);
    }

    // Strictly decreasing: each higher counter redraws a higher row (a lower row index).
    for (std::size_t i = 1; i < kExpectedPlayingFieldWipes.size(); ++i) {
        const std::uint8_t prev =
            playingFieldRowForWipeCounter(kExpectedPlayingFieldWipes[i - 1].counter);
        const std::uint8_t curr =
            playingFieldRowForWipeCounter(kExpectedPlayingFieldWipes[i].counter);
        EXPECT_LT(curr, prev) << "row did not decrease at fixture index " << i;
    }
}

// 4. The two ends of the walk, pinned to concrete rows and addresses.
TEST(PlayingField, WipeBoundaryPins) {
    // counter 2 -> bottom row (17), addresses $9A22 / $CA22.
    EXPECT_EQ(playingFieldRowForWipeCounter(2), 17);
    EXPECT_EQ(kExpectedPlayingFieldWipes.front().counter, 2);
    EXPECT_EQ(kExpectedPlayingFieldWipes.front().vram, 0x9A22);
    EXPECT_EQ(kExpectedPlayingFieldWipes.front().wram, 0xCA22);

    // counter 19 -> top row (0), addresses $9802 / $C802.
    EXPECT_EQ(playingFieldRowForWipeCounter(19), 0);
    EXPECT_EQ(kExpectedPlayingFieldWipes.back().counter, 19);
    EXPECT_EQ(kExpectedPlayingFieldWipes.back().vram, 0x9802);
    EXPECT_EQ(kExpectedPlayingFieldWipes.back().wram, 0xC802);
}

}  // namespace
