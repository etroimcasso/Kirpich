// Composed sprites: the SpriteId identity space, the 94 composed OAM layouts, and the piece-region
// and rocket-tier links to the rest of the data layer.
//
// The engine table (src/data/sprites.h) is swept in full against the parser-emitted fixture
// (tests/fixtures/sprites_expected.h): the fixture holds the ROM's raw serialization - the
// SpriteList targets, the record rows, the concatenated tile streams, and the grid pairs - and the
// sweep here re-runs the escape state machine over those bytes and compares the result to kSprites,
// so a defect in either the composed surface or the parser cannot hide. The five grids are also
// re-derived from their closed-form geometry (preserving the retired grid unit's coverage), and the
// piece-rotation and rocket-tier identities are tied to Piece and kBonusEndings. Expectations come
// from docs/contracts/sprites.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <kirpich/piece.h>
#include <kirpich/piece_kind.h>
#include <kirpich/sprite_id.h>

#include "data/scoring.h"
#include "data/sprites.h"
#include "fixtures/sprites_expected.h"

namespace {

using kirpich::kBonusEndings;
using kirpich::kSprites;
using kirpich::getSprite;
using kirpich::Piece;
using kirpich::PieceKind;
using kirpich::Sprite;
using kirpich::SpriteId;
using kirpich::SpritePart;
using kirpich::fixtures::kExpectedGridPairBytes;
using kirpich::fixtures::kExpectedGridRows;
using kirpich::fixtures::kExpectedSpriteListTargets;
using kirpich::fixtures::kExpectedSpriteRecordRows;
using kirpich::fixtures::kExpectedSpriteStreamBytes;
using kirpich::fixtures::SpriteGridRow;
using kirpich::fixtures::SpriteRecordRow;

constexpr std::size_t kSpriteCount = 94;

// The record whose address is the given SpriteList target (records are unique by address).
const SpriteRecordRow& findRecord(std::uint16_t target) {
    for (const SpriteRecordRow& row : kExpectedSpriteRecordRows) {
        if (row.record_address == target) {
            return row;
        }
    }
    ADD_FAILURE() << "no record row for target 0x" << std::hex << target;
    return kExpectedSpriteRecordRows[0];
}

// The grid at a given ROM address.
const SpriteGridRow& findGrid(std::uint16_t address) {
    for (const SpriteGridRow& grid : kExpectedGridRows) {
        if (grid.grid_address == address) {
            return grid;
        }
    }
    ADD_FAILURE() << "no grid row for address 0x" << std::hex << address;
    return kExpectedGridRows[0];
}

// The k-th (y, x) pair of a grid, read out of the flat pair-byte array.
std::pair<std::uint8_t, std::uint8_t> gridPair(const SpriteGridRow& grid, std::size_t k) {
    const std::size_t base = 2 * (grid.pair_offset + k);
    return {kExpectedGridPairBytes[base], kExpectedGridPairBytes[base + 1]};
}

// Compose one identity's parts straight from the raw fixture, independent of kSprites.
std::vector<SpritePart> composeFromFixture(std::size_t index) {
    const SpriteRecordRow& rec = findRecord(kExpectedSpriteListTargets[index]);
    const SpriteGridRow& grid = findGrid(rec.grid_address);
    std::vector<SpritePart> parts;
    std::size_t pair = 0;
    const std::size_t end = std::size_t{rec.stream_offset} + rec.stream_length;
    for (std::size_t j = rec.stream_offset; j < end;) {
        const std::uint8_t b = kExpectedSpriteStreamBytes[j];
        if (b == 0xFF) {
            break;
        }
        if (b == 0xFD) {
            const std::uint8_t tile = kExpectedSpriteStreamBytes[j + 1];
            const auto [y, x] = gridPair(grid, pair++);
            parts.push_back(SpritePart{y, x, true, tile});
            j += 2;
        } else if (b == 0xFE) {
            ++pair;
            j += 1;
        } else {
            const auto [y, x] = gridPair(grid, pair++);
            parts.push_back(SpritePart{y, x, false, b});
            j += 1;
        }
    }
    return parts;
}

constexpr std::uint8_t idValue(SpriteId id) { return static_cast<std::uint8_t>(id); }

// 1. The identity space: 94 enumerators, boundary values pinned, every row at the index it names.
TEST(Sprites, SpriteIdBoundaryPins) {
    static_assert(kSprites.size() == kSpriteCount);

    EXPECT_EQ(idValue(SpriteId::L_0), 0x00);
    EXPECT_EQ(idValue(SpriteId::T_3), 0x1B);
    EXPECT_EQ(idValue(SpriteId::A_TYPE), 0x1C);
    EXPECT_EQ(idValue(SpriteId::DIGIT_0), 0x20);
    EXPECT_EQ(idValue(SpriteId::BURAN), 0x2C);
    EXPECT_EQ(idValue(SpriteId::ROCKET_L), 0x58);
    EXPECT_EQ(idValue(SpriteId::ROCKET_EXHAUST_2), 0x5D);

    for (std::size_t i = 0; i < kSprites.size(); ++i) {
        EXPECT_EQ(static_cast<std::size_t>(kSprites[i].id), i) << "id at row " << i;
    }
}

// 2. The moment-of-truth: re-compose every identity from the raw fixture (escape state machine +
//    grid walk) and compare field-for-field against kSprites - all 94 ids, every part.
TEST(Sprites, ComposedSpriteFullSweep) {
    for (std::size_t i = 0; i < kSpriteCount; ++i) {
        const SpriteRecordRow& rec = findRecord(kExpectedSpriteListTargets[i]);
        const std::vector<SpritePart> expected = composeFromFixture(i);
        const Sprite& s = kSprites[i];

        ASSERT_EQ(static_cast<std::size_t>(s.id), i) << "id at " << i;
        EXPECT_EQ(s.offset_y, static_cast<std::int8_t>(rec.offset_y_byte)) << "offset_y at " << i;
        EXPECT_EQ(s.offset_x, static_cast<std::int8_t>(rec.offset_x_byte)) << "offset_x at " << i;
        ASSERT_EQ(s.parts.size(), expected.size()) << "part count at " << i;
        for (std::size_t k = 0; k < expected.size(); ++k) {
            EXPECT_EQ(s.parts[k], expected[k]) << "part " << k << " of sprite " << i;
        }
    }
}

// 3. Signed record offsets decode from the raw bytes, pinned across the sprite classes and swept
//    to stay inside the audited ranges (y in [-40, 0], x in [-24, 0]).
TEST(Sprites, SpriteRecordOffsetPins) {
    EXPECT_EQ(getSprite(SpriteId::L_0).offset_y, -17);   // -0x11
    EXPECT_EQ(getSprite(SpriteId::L_0).offset_x, -16);   // -0x10
    EXPECT_EQ(getSprite(SpriteId::A_TYPE).offset_y, 0);
    EXPECT_EQ(getSprite(SpriteId::A_TYPE).offset_x, -24);  // -0x18
    EXPECT_EQ(getSprite(SpriteId::DIGIT_0).offset_y, 0);
    EXPECT_EQ(getSprite(SpriteId::DIGIT_0).offset_x, 0);
    EXPECT_EQ(getSprite(SpriteId::BURAN).offset_y, -32);   // -0x20
    EXPECT_EQ(getSprite(SpriteId::BURAN).offset_x, -16);
    EXPECT_EQ(getSprite(SpriteId::ROCKET_L).offset_y, -40);  // -0x28
    EXPECT_EQ(getSprite(SpriteId::ROCKET_L).offset_x, -8);   // -0x08

    for (const Sprite& s : kSprites) {
        EXPECT_GE(s.offset_y, -40) << "y under range at id " << idValue(s.id);
        EXPECT_LE(s.offset_y, 0) << "y over range at id " << idValue(s.id);
        EXPECT_GE(s.offset_x, -24) << "x under range at id " << idValue(s.id);
        EXPECT_LE(s.offset_x, 0) << "x over range at id " << idValue(s.id);
    }
}

// 4. The four aliased identities are equal to their canonical partner in geometry (offsets + parts)
//    while differing in id - the by-value duplication that keeps every id self-contained.
TEST(Sprites, AliasedSpritesEqualByValue) {
    const std::array<std::pair<std::size_t, std::size_t>, 4> aliases = {{
        {0x2D, 0x2B}, {0x42, 0x3F}, {0x43, 0x44}, {0x5B, 0x5A},
    }};
    for (const auto& [alias, canon] : aliases) {
        const Sprite& a = kSprites[alias];
        const Sprite& c = kSprites[canon];
        EXPECT_NE(a.id, c.id) << "ids should differ for " << alias;
        EXPECT_EQ(a.offset_y, c.offset_y) << "offset_y for " << alias;
        EXPECT_EQ(a.offset_x, c.offset_x) << "offset_x for " << alias;
        ASSERT_EQ(a.parts.size(), c.parts.size()) << "part count for " << alias;
        for (std::size_t k = 0; k < a.parts.size(); ++k) {
            EXPECT_EQ(a.parts[k], c.parts[k]) << "part " << k << " for " << alias;
        }
    }
}

// 5. The 28 piece-rotation sprites occupy the first SpriteId indices in Piece encoding order:
//    Piece::of(kind, rotation).raw is exactly the SpriteId value, and the byte decodes to the same
//    kind through PieceKind.
TEST(Sprites, PieceRegionMatchesPieceEncoding) {
    for (int kind = 0; kind <= 6; ++kind) {
        for (int rot = 0; rot <= 3; ++rot) {
            const std::uint8_t raw = Piece::of(static_cast<std::uint8_t>(kind),
                                               static_cast<std::uint8_t>(rot)).raw;
            EXPECT_EQ(raw, kind * 4 + rot) << "raw for kind " << kind << " rot " << rot;
            EXPECT_EQ(idValue(kSprites[raw].id), raw) << "sprite index for raw " << int{raw};
            EXPECT_EQ(Piece{raw}.kind(), static_cast<PieceKind>(kind)) << "kind for raw " << int{raw};
        }
    }
}

// 6. Hand-pinned escape resolutions: a mid-row $FD flip, an alternating $FD stream, an all-flipped
//    row, and a leading $FE run that pushes the first part onto grid cell 8.
TEST(Sprites, XflipResolutionPins) {
    // CRYING_LARGE_MARIO_1 (SpriteTiles_2F8F): 2F, 05, 05-flip, 2F, ...
    const Sprite& mario = getSprite(SpriteId::CRYING_LARGE_MARIO_1);
    EXPECT_EQ(mario.parts[0], (SpritePart{0, 0, false, 0x2F}));
    EXPECT_EQ(mario.parts[1], (SpritePart{0, 8, false, 0x05}));
    EXPECT_EQ(mario.parts[2], (SpritePart{0, 16, true, 0x05}));
    EXPECT_EQ(mario.parts[3], (SpritePart{0, 24, false, 0x2F}));

    // ROCKET_S (SpriteTiles_318E): each tile appears once normal then once flipped.
    const Sprite& rocket = getSprite(SpriteId::ROCKET_S);
    EXPECT_EQ(rocket.parts[0], (SpritePart{0, 0, false, 0xA8}));
    EXPECT_EQ(rocket.parts[1], (SpritePart{0, 8, true, 0xA8}));
    EXPECT_EQ(rocket.parts[2], (SpritePart{8, 0, false, 0xA9}));
    EXPECT_EQ(rocket.parts[3], (SpritePart{8, 8, true, 0xA9}));

    // BIG_DRUM_2 (SpriteTiles_305C): every part is flipped.
    const Sprite& drum = getSprite(SpriteId::BIG_DRUM_2);
    ASSERT_EQ(drum.parts.size(), 4u);
    for (const SpritePart& part : drum.parts) {
        EXPECT_TRUE(part.xflip);
    }
    EXPECT_EQ(drum.parts[0].tile, 0xE6);
    EXPECT_EQ(drum.parts[3].tile, 0xE7);

    // L_0 (SpriteTiles_2D58): eight leading $FE skips, so the first part lands on grid cell 8.
    const Sprite& l0 = getSprite(SpriteId::L_0);
    ASSERT_EQ(l0.parts.size(), 4u);
    EXPECT_EQ(l0.parts[0], (SpritePart{16, 0, false, 0x84}));  // grid pair 8 = (16, 0)
}

// 7. The five grids follow their closed-form geometry (re-derived from the fixture pairs) - the
//    coverage the retired grid unit carried, folded in here.
TEST(Sprites, GridGeometryClosedForm) {
    ASSERT_EQ(kExpectedGridRows.size(), 5u);

    auto check = [](const SpriteGridRow& grid, std::size_t i,
                    std::uint8_t y, std::uint8_t x, const char* name) {
        const std::size_t base = 2 * (grid.pair_offset + i);
        EXPECT_EQ(kExpectedGridPairBytes[base], y) << name << " y at " << i;
        EXPECT_EQ(kExpectedGridPairBytes[base + 1], x) << name << " x at " << i;
    };

    const SpriteGridRow& g4x4 = kExpectedGridRows[0];   // 16 pairs, 4 wide
    ASSERT_EQ(g4x4.pair_count, 16u);
    for (std::size_t i = 0; i < 16; ++i) {
        check(g4x4, i, static_cast<std::uint8_t>(8 * (i / 4)), static_cast<std::uint8_t>(8 * (i % 4)), "4x4");
    }
    const SpriteGridRow& g1x8 = kExpectedGridRows[1];   // 8 pairs, one row
    ASSERT_EQ(g1x8.pair_count, 8u);
    for (std::size_t i = 0; i < 8; ++i) {
        check(g1x8, i, 0, static_cast<std::uint8_t>(8 * i), "1x8");
    }
    const SpriteGridRow& g7x2 = kExpectedGridRows[2];   // 14 pairs, 2 wide
    ASSERT_EQ(g7x2.pair_count, 14u);
    for (std::size_t i = 0; i < 14; ++i) {
        check(g7x2, i, static_cast<std::uint8_t>(8 * (i / 2)), static_cast<std::uint8_t>(8 * (i % 2)), "7x2");
    }
    const SpriteGridRow& gNotch = kExpectedGridRows[3];  // 28 pairs; rows 0-1 hold 2 at x=$08, rest 4
    ASSERT_EQ(gNotch.pair_count, 28u);
    for (std::size_t r = 0; r < 2; ++r) {
        for (std::size_t c = 0; c < 2; ++c) {
            check(gNotch, 2 * r + c, static_cast<std::uint8_t>(8 * r), static_cast<std::uint8_t>(8 * (c + 1)), "notch head");
        }
    }
    for (std::size_t r = 2; r < 8; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            check(gNotch, 4 + 4 * (r - 2) + c, static_cast<std::uint8_t>(8 * r), static_cast<std::uint8_t>(8 * c), "notch body");
        }
    }
    const SpriteGridRow& g3x3 = kExpectedGridRows[4];   // 9 pairs, 3 wide
    ASSERT_EQ(g3x3.pair_count, 9u);
    for (std::size_t i = 0; i < 9; ++i) {
        check(g3x3, i, static_cast<std::uint8_t>(8 * (i / 3)), static_cast<std::uint8_t>(8 * (i % 3)), "3x3");
    }
}

// 8. The bonus-ending tiers point at the three ranked rockets by SpriteId (the scoring-to-sprite
//    link, from the sprite side). The score-to-tier edges live in test_scoring.cpp.
TEST(Sprites, RocketSpriteTiersTyped) {
    ASSERT_EQ(kBonusEndings.size(), 3u);
    EXPECT_EQ(kBonusEndings[0].rocket_sprite, SpriteId::ROCKET_L);
    EXPECT_EQ(kBonusEndings[1].rocket_sprite, SpriteId::ROCKET_M);
    EXPECT_EQ(kBonusEndings[2].rocket_sprite, SpriteId::ROCKET_S);
}

}  // namespace
