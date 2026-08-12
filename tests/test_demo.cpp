// Demo data: the two attract-mode joypad recordings and the shared piece sequence.
//
// Each recorded step's held state surfaces as a set of game actions (src/data/demo.h). The sweep
// bridges the two derivations: the flat byte arrays in tests/fixtures/demo_expected.h hold each blob's
// serialization exactly as the original stores it (a raw Game Boy joypad byte per step), and this test
// resolves that byte to the action set the composed record must carry, through the same button->action
// mapping the game's input handler defines. A defect in the header or a wrong mapping fails the sweep.
//
// Nothing here reinvents the engine input surface: the records already carry engine action sets, and
// the test reads them through the engine's own ActionSet.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <retropp/input.h>

#include <kirpich/action.h>
#include <kirpich/piece.h>

#include "data/demo.h"
#include "fixtures/demo_expected.h"

namespace {

using kirpich::Action;
using kirpich::DemoInputRecord;
using kirpich::kDemoPieceCount;
using kirpich::kDemoPieceList;
using kirpich::kTypeADemoInputCount;
using kirpich::kTypeADemoInputs;
using kirpich::kTypeBDemoInputCount;
using kirpich::kTypeBDemoInputs;
using kirpich::Piece;
using kirpich::fixtures::kExpectedDemoPieceListBytes;
using kirpich::fixtures::kExpectedTypeADemoBytes;
using kirpich::fixtures::kExpectedTypeBDemoBytes;

// The Game Boy joypad bits and the actions they drive during gameplay (RotateAndShiftPiece,
// tetris.asm:5910-6028, + the soft-drop path). Independently encoded here so it cross-checks the
// parser's mapping rather than sharing it.
constexpr std::uint8_t kBitA = 0x01;      // rotate clockwise
constexpr std::uint8_t kBitB = 0x02;      // rotate counter-clockwise
constexpr std::uint8_t kBitRight = 0x10;  // shift right
constexpr std::uint8_t kBitLeft = 0x20;   // shift left
constexpr std::uint8_t kBitDown = 0x80;   // soft drop
constexpr std::uint8_t kMappedBits = kBitA | kBitB | kBitRight | kBitLeft | kBitDown;

// The action set a raw Game Boy joypad byte resolves to.
retropp::ActionSet expectedHeld(std::uint8_t gb) {
    retropp::ActionSet set;
    if (gb & kBitA) set.set(retropp::actionId(Action::RotateClockwise), true);
    if (gb & kBitB) set.set(retropp::actionId(Action::RotateCounterClockwise), true);
    if (gb & kBitRight) set.set(retropp::actionId(Action::MoveRight), true);
    if (gb & kBitLeft) set.set(retropp::actionId(Action::MoveLeft), true);
    if (gb & kBitDown) set.set(retropp::actionId(Action::SoftDrop), true);
    return set;
}

// 1. Counts and shapes. The two record arrays and the piece list are the declared sizes, a Piece is one
//    byte, and the flat fixtures span the matching byte counts.
TEST(Demo, ConstantsAndShape) {
    EXPECT_EQ(kTypeADemoInputCount, 128);
    EXPECT_EQ(kTypeBDemoInputCount, 80);
    EXPECT_EQ(kDemoPieceCount, 48);

    static_assert(sizeof(Piece) == 1, "Piece must be one byte");

    EXPECT_EQ(kTypeADemoInputs.size(), std::size_t{kTypeADemoInputCount});
    EXPECT_EQ(kTypeBDemoInputs.size(), std::size_t{kTypeBDemoInputCount});
    EXPECT_EQ(kDemoPieceList.size(), std::size_t{kDemoPieceCount});

    EXPECT_EQ(kExpectedTypeADemoBytes.size(), std::size_t{kTypeADemoInputCount} * 2);
    EXPECT_EQ(kExpectedTypeBDemoBytes.size(), std::size_t{kTypeBDemoInputCount} * 2);
    EXPECT_EQ(kExpectedDemoPieceListBytes.size(), std::size_t{kDemoPieceCount});
}

// 2. Type A full-corpus sweep: every record's action set matches the one the raw held byte resolves to,
//    and its frame count matches the raw frame byte.
TEST(Demo, TypeAFullCorpusSweep) {
    for (std::size_t i = 0; i < kTypeADemoInputs.size(); ++i) {
        EXPECT_EQ(kTypeADemoInputs[i].held, expectedHeld(kExpectedTypeADemoBytes[i * 2]))
            << "record " << i << " held";
        EXPECT_EQ(kTypeADemoInputs[i].frames, kExpectedTypeADemoBytes[i * 2 + 1])
            << "record " << i << " frames";
    }
}

// 3. Type B full-corpus sweep: the same over all 80 records / 160 bytes.
TEST(Demo, TypeBFullCorpusSweep) {
    for (std::size_t i = 0; i < kTypeBDemoInputs.size(); ++i) {
        EXPECT_EQ(kTypeBDemoInputs[i].held, expectedHeld(kExpectedTypeBDemoBytes[i * 2]))
            << "record " << i << " held";
        EXPECT_EQ(kTypeBDemoInputs[i].frames, kExpectedTypeBDemoBytes[i * 2 + 1])
            << "record " << i << " frames";
    }
}

// 4. Piece-list full-corpus sweep: every Piece byte equals the fixture, and every piece is a
//    spawn-orientation spec (kind 0-6, rotation 0) across the whole corpus.
TEST(Demo, PieceListFullCorpusSweep) {
    for (std::size_t i = 0; i < kDemoPieceList.size(); ++i) {
        EXPECT_EQ(kDemoPieceList[i].raw, kExpectedDemoPieceListBytes[i]) << "piece " << i;
        EXPECT_LT(static_cast<std::uint8_t>(kDemoPieceList[i].kind()), 7) << "piece " << i << " kind";
        EXPECT_EQ(kDemoPieceList[i].rotation(), 0) << "piece " << i << " rotation";
    }
}

// 5. Corpus action invariant: the raw held bytes press only the five mapped bits, and the recordings
//    never rotate counter-clockwise (the demos press A but never B). Checked in action space.
TEST(Demo, CorpusActionInvariant) {
    for (const DemoInputRecord& r : kTypeADemoInputs) {
        EXPECT_FALSE(r.held.test(retropp::actionId(Action::RotateCounterClockwise)));
    }
    for (const DemoInputRecord& r : kTypeBDemoInputs) {
        EXPECT_FALSE(r.held.test(retropp::actionId(Action::RotateCounterClockwise)));
    }
    for (std::size_t i = 0; i < kExpectedTypeADemoBytes.size(); i += 2) {
        EXPECT_EQ(kExpectedTypeADemoBytes[i] & ~kMappedBits, 0)
            << "Type A held 0x" << std::hex << static_cast<int>(kExpectedTypeADemoBytes[i]);
    }
    for (std::size_t i = 0; i < kExpectedTypeBDemoBytes.size(); i += 2) {
        EXPECT_EQ(kExpectedTypeBDemoBytes[i] & ~kMappedBits, 0)
            << "Type B held 0x" << std::hex << static_cast<int>(kExpectedTypeBDemoBytes[i]);
    }
}

// 6. Corner and consumption-range pins. First/last records of each stream and first/last pieces by
//    value, and the two consumed piece-index ranges (demo #2 reads 0-15, demo #1 reads 17-29) sit
//    inside the piece list.
TEST(Demo, CornerAndConsumptionPins) {
    // Type A: record 0 = no input for 0x2A frames; record 1 = move left (LEFT, 0x20) for one frame.
    EXPECT_EQ(kTypeADemoInputs.front(), (DemoInputRecord{expectedHeld(0x00), 0x2A}));
    EXPECT_EQ(kTypeADemoInputs[1], (DemoInputRecord{expectedHeld(0x20), 0x01}));
    EXPECT_TRUE(kTypeADemoInputs[1].held.test(retropp::actionId(Action::MoveLeft)));
    EXPECT_EQ(kTypeADemoInputs.back(), (DemoInputRecord{expectedHeld(0x00), 0x00}));  // trailing

    EXPECT_EQ(kTypeBDemoInputs.front(), (DemoInputRecord{expectedHeld(0x00), 0x4D}));

    EXPECT_EQ(kDemoPieceList.front().raw, 0x10);
    EXPECT_EQ(kDemoPieceList.back().raw, 0x08);

    // Both demos draw from this one list; the highest index either reads is 29, well inside the 48.
    EXPECT_LT(std::size_t{15}, kDemoPieceList.size());   // demo #2 (Type A): indices 0-15
    EXPECT_LT(std::size_t{29}, kDemoPieceList.size());   // demo #1 (Type B): indices 17-29
}

}  // namespace
