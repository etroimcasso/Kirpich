// Sprite layout grids: the five shared (y, x) offset frames, plus the PieceKind enum they unlock.
//
// The engine arrays (src/data/sprite_grids.h) are swept in full against the parser-emitted fixture
// (tests/fixtures/sprite_grids_expected.h) so a defect in either can't hide, and each grid's closed
// -form geometry is asserted independently. PieceKind's values and its decode from a piece byte are
// pinned to docs/contracts/sprite-grids.md.

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/piece.h>
#include <kirpich/piece_kind.h>

#include "data/sprite_grids.h"
#include "fixtures/sprite_grids_expected.h"

namespace {

using kirpich::Piece;
using kirpich::PieceKind;
using kirpich::SpriteGridOffset;

using kirpich::kSpriteGrid1x8;
using kirpich::kSpriteGrid3x3;
using kirpich::kSpriteGrid4x4;
using kirpich::kSpriteGrid7x2;
using kirpich::kSpriteGrid8x4Notched;

constexpr std::uint8_t u8(std::size_t v) { return static_cast<std::uint8_t>(v); }

template <std::size_t N>
void sweep(const std::array<SpriteGridOffset, N>& engine,
           const std::array<SpriteGridOffset, N>& fixture, const char* name) {
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_EQ(engine[i].y, fixture[i].y) << name << " y at " << i;
        EXPECT_EQ(engine[i].x, fixture[i].x) << name << " x at " << i;
    }
}

// 1. Full corpus sweep: every one of the 75 pairs equals the parser-emitted fixture, field for field.
TEST(SpriteGrids, SweepAgainstFixture) {
    sweep(kSpriteGrid4x4, kirpich::fixtures::kExpectedSpriteGrid4x4, "4x4");
    sweep(kSpriteGrid1x8, kirpich::fixtures::kExpectedSpriteGrid1x8, "1x8");
    sweep(kSpriteGrid7x2, kirpich::fixtures::kExpectedSpriteGrid7x2, "7x2");
    sweep(kSpriteGrid8x4Notched, kirpich::fixtures::kExpectedSpriteGrid8x4Notched, "8x4Notched");
    sweep(kSpriteGrid3x3, kirpich::fixtures::kExpectedSpriteGrid3x3, "3x3");
}

// 2. Array lengths and element size; the five grids total 150 bytes (75 pairs x 2 bytes).
TEST(SpriteGrids, ShapesAndSizes) {
    static_assert(sizeof(SpriteGridOffset) == 2);
    static_assert(kSpriteGrid4x4.size() == 16);
    static_assert(kSpriteGrid1x8.size() == 8);
    static_assert(kSpriteGrid7x2.size() == 14);
    static_assert(kSpriteGrid8x4Notched.size() == 28);
    static_assert(kSpriteGrid3x3.size() == 9);

    const std::size_t total_bytes =
        (kSpriteGrid4x4.size() + kSpriteGrid1x8.size() + kSpriteGrid7x2.size() +
         kSpriteGrid8x4Notched.size() + kSpriteGrid3x3.size()) *
        sizeof(SpriteGridOffset);
    EXPECT_EQ(total_bytes, 150u);
}

// 3. The four regular grids follow closed-form row-major geometry, 8 pixels per step.
TEST(SpriteGrids, RegularGeometry) {
    for (std::size_t i = 0; i < kSpriteGrid4x4.size(); ++i) {
        EXPECT_EQ(kSpriteGrid4x4[i].y, u8(8 * (i / 4))) << "4x4 y at " << i;
        EXPECT_EQ(kSpriteGrid4x4[i].x, u8(8 * (i % 4))) << "4x4 x at " << i;
    }
    for (std::size_t i = 0; i < kSpriteGrid1x8.size(); ++i) {
        EXPECT_EQ(kSpriteGrid1x8[i].y, u8(0)) << "1x8 y at " << i;
        EXPECT_EQ(kSpriteGrid1x8[i].x, u8(8 * i)) << "1x8 x at " << i;
    }
    for (std::size_t i = 0; i < kSpriteGrid7x2.size(); ++i) {
        EXPECT_EQ(kSpriteGrid7x2[i].y, u8(8 * (i / 2))) << "7x2 y at " << i;
        EXPECT_EQ(kSpriteGrid7x2[i].x, u8(8 * (i % 2))) << "7x2 x at " << i;
    }
    for (std::size_t i = 0; i < kSpriteGrid3x3.size(); ++i) {
        EXPECT_EQ(kSpriteGrid3x3[i].y, u8(8 * (i / 3))) << "3x3 y at " << i;
        EXPECT_EQ(kSpriteGrid3x3[i].x, u8(8 * (i % 3))) << "3x3 x at " << i;
    }
}

// 4. The notched grid: rows 0-1 hold 2 pairs each starting at x=$08; rows 2-7 hold 4 pairs at x=$00.
TEST(SpriteGrids, NotchedGeometry) {
    for (std::size_t r = 0; r < 2; ++r) {
        for (std::size_t c = 0; c < 2; ++c) {
            const std::size_t idx = 2 * r + c;
            EXPECT_EQ(kSpriteGrid8x4Notched[idx].y, u8(8 * r)) << "notched y at " << idx;
            EXPECT_EQ(kSpriteGrid8x4Notched[idx].x, u8(8 * (c + 1))) << "notched x at " << idx;
        }
    }
    for (std::size_t r = 2; r < 8; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t idx = 4 + 4 * (r - 2) + c;
            EXPECT_EQ(kSpriteGrid8x4Notched[idx].y, u8(8 * r)) << "notched y at " << idx;
            EXPECT_EQ(kSpriteGrid8x4Notched[idx].x, u8(8 * c)) << "notched x at " << idx;
        }
    }
}

// 5. PieceKind: exactly seven shapes, values 0..6 in L, J, I, O, S, Z, T order.
TEST(SpriteGrids, PieceKindValuesMatchContract) {
    EXPECT_EQ(static_cast<int>(PieceKind::L), 0);
    EXPECT_EQ(static_cast<int>(PieceKind::J), 1);
    EXPECT_EQ(static_cast<int>(PieceKind::I), 2);
    EXPECT_EQ(static_cast<int>(PieceKind::O), 3);
    EXPECT_EQ(static_cast<int>(PieceKind::S), 4);
    EXPECT_EQ(static_cast<int>(PieceKind::Z), 5);
    EXPECT_EQ(static_cast<int>(PieceKind::T), 6);
}

// 6. Piece::kind() decodes to PieceKind(raw >> 2) across the whole valid range; rotation unchanged.
TEST(SpriteGrids, PieceKindFromPieceByte) {
    for (int r = 0; r <= 27; ++r) {
        const Piece p{static_cast<std::uint8_t>(r)};
        EXPECT_EQ(p.kind(), static_cast<PieceKind>(r >> 2)) << "raw " << r;
        EXPECT_EQ(p.rotation(), static_cast<std::uint8_t>(r & 0x03)) << "raw " << r;
    }
}

}  // namespace
