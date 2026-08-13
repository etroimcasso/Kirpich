// Engine state (WRAM globals): the mutable game-state struct (src/state/engine_state.h) and its
// layout contract (tests/fixtures/wram_expected.h).
//
// The fixture holds the whole wram.asm layout as {name, address, size} rows plus a section table.
// The integrity sweep walks every row - never a subset - and proves each section tiles exactly with
// no overlap or hole; the width pins tie the $C000 rows to the struct's shapes. The remaining tests
// exercise the struct's behaviour: reset-to-boot, the OamEntry value type, and the line-clears
// bounded vector. Expectations come from docs/contracts/engine-state.md.

#include <cstdint>
#include <limits>
#include <string_view>

#include <gtest/gtest.h>

#include <kirpich/piece.h>

#include "state/engine_state.h"
#include "fixtures/wram_expected.h"

namespace {

using kirpich::EngineState;
using kirpich::OamEntry;
using kirpich::Piece;
using kirpich::fixtures::kWramLabels;
using kirpich::fixtures::kWramSections;
using kirpich::fixtures::WramKind;

// Fixture lookups by label name. A miss fails the test rather than reading past the array.
std::uint16_t sizeOf(std::string_view name) {
    for (const auto& row : kWramLabels) {
        if (row.name == name) return row.size;
    }
    ADD_FAILURE() << "label not found in fixture: " << name;
    return 0;
}

std::uint16_t addrOf(std::string_view name) {
    for (const auto& row : kWramLabels) {
        if (row.name == name) return row.address;
    }
    ADD_FAILURE() << "label not found in fixture: " << name;
    return 0xFFFF;
}

// (1) Fixture-integrity sweep - every row, every section, no subset.
TEST(EngineState, WramFixtureTilesEverySection) {
    // Sections partition kWramLabels contiguously.
    std::size_t idx = 0;
    for (const auto& sec : kWramSections) {
        EXPECT_EQ(sec.first, idx) << sec.name;
        idx += sec.count;
    }
    EXPECT_EQ(idx, kWramLabels.size());

    // Every region: positive size, inside WRAM0, and the section tiles with no overlap or hole.
    for (const auto& sec : kWramSections) {
        std::uint32_t cursor = sec.origin;
        std::uint16_t lastFieldAddr = 0;
        bool haveField = false;
        for (std::size_t i = sec.first; i < sec.first + sec.count; ++i) {
            const auto& r = kWramLabels[i];
            EXPECT_GT(r.size, 0) << r.name;
            EXPECT_GE(r.address, 0xC000) << r.name;
            EXPECT_LT(r.address, 0xE000) << r.name;
            if (r.kind == WramKind::Alias) {
                ASSERT_TRUE(haveField) << "alias with no preceding field: " << r.name;
                EXPECT_EQ(r.address, lastFieldAddr) << r.name;  // shares the field's address
                continue;                                       // does not advance the cursor
            }
            EXPECT_EQ(r.address, cursor) << sec.name << " @ " << r.name;
            if (r.kind == WramKind::Field) {
                lastFieldAddr = r.address;
                haveField = true;
            }
            cursor += r.size;
        }
        EXPECT_EQ(cursor, sec.end) << sec.name;
    }
}

// (2) Width/count pins - the $C000 rows against the struct shapes.
TEST(EngineState, StructWidthsMatchFixture) {
    const EngineState s{};

    // OAM staging: 160 bytes = 40 objects x 4 bytes.
    EXPECT_EQ(sizeOf("wOAMBuffer"), 160);
    EXPECT_EQ(sizeOf("wOAMBuffer"), 40 * 4);
    EXPECT_EQ(s.oam.size(), 40u);

    // Score: 3 packed-decimal bytes (6 digits); the decimal port field must hold the 999,999 ceiling.
    EXPECT_EQ(sizeOf("wScore"), 3);
    static_assert(999999u <= std::numeric_limits<std::uint32_t>::max());

    // Piece ring: 256 entries.
    EXPECT_EQ(sizeOf("wPieceList"), 256);
    EXPECT_EQ(s.pieceList.size(), 256u);

    // Line-clears list: 4 row addresses + a zero-word terminator = 9 bytes; port caps at 4.
    EXPECT_EQ(sizeOf("wLineClearsList"), 9);
    EXPECT_EQ(s.lineClears.capacity(), 4u);

    // Soft-drop points: a 16-bit binary count.
    EXPECT_EQ(sizeOf("wSoftDropPoints"), 2);
    EXPECT_EQ(sizeof(s.softDropPoints), 2u);

    // The four stat counts are single bytes on a stride of five ($C0AC, $C0B1, $C0B6, $C0BB).
    for (const char* name : {"wSinglesCount", "wDoublesCount", "wTriplesCount", "wTetrisCount"}) {
        EXPECT_EQ(sizeOf(name), 1) << name;
    }
    EXPECT_EQ(addrOf("wSinglesCount"), 0xC0AC);
    EXPECT_EQ(addrOf("wDoublesCount"), 0xC0B1);
    EXPECT_EQ(addrOf("wTriplesCount"), 0xC0B6);
    EXPECT_EQ(addrOf("wTetrisCount"), 0xC0BB);
    EXPECT_EQ(addrOf("wLineClearStats"), addrOf("wSinglesCount"));  // aliased head of the block
}

// (3) Reset-to-zero behaviour - mutate every field, reset, compare to a fresh instance.
TEST(EngineState, ResetRestoresBootState) {
    EngineState s{};
    s.oam[0] = OamEntry{.y = 1, .x = 2, .tile = 3,
                        .behindBg = true, .yflip = true, .xflip = true, .palette1 = true};
    s.oam[39].tile = 0x9E;
    s.score = 123456;
    s.lineClears = {0, 5, 17};
    s.stats = {.singles = 1, .doubles = 2, .triples = 3, .tetrises = 4};
    s.softDropPoints = 4321;
    s.scoreboardState = 7;
    s.scoreboardTallyPhase = 2;
    s.blockSoftDropAfterLock = true;
    s.scoreRedrawRequested = true;
    s.hidePreviewPiece = true;
    s.pieceList[0] = Piece{0x1C};
    s.pieceList[255] = Piece{0x04};

    EXPECT_FALSE(s == EngineState{});  // the mutations actually took
    s.reset();
    EXPECT_TRUE(s == EngineState{});   // back to the boot state
}

// (4) OamEntry value type - default is all-zero/false; operator== compares every field.
TEST(EngineState, OamEntryValueSemantics) {
    OamEntry def{};
    EXPECT_EQ(def.y, 0);
    EXPECT_EQ(def.x, 0);
    EXPECT_EQ(def.tile, 0);
    EXPECT_FALSE(def.behindBg);
    EXPECT_FALSE(def.yflip);
    EXPECT_FALSE(def.xflip);
    EXPECT_FALSE(def.palette1);

    OamEntry a{.y = 8, .x = 16, .tile = 0xAE, .xflip = true};
    OamEntry b = a;
    EXPECT_TRUE(a == b);
    b.palette1 = true;         // flip one attribute bit
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(a == def);
}

// (5) Line-clears bounded domain - up to 4 field-row indices in 0..17.
TEST(EngineState, LineClearsBoundedDomain) {
    EngineState s{};
    EXPECT_TRUE(s.lineClears.empty());
    EXPECT_EQ(s.lineClears.capacity(), 4u);

    s.lineClears = {17, 0, 9, 3};  // four rows, filling the cap
    EXPECT_EQ(s.lineClears.size(), 4u);
    EXPECT_EQ(s.lineClears[0], 17);
    EXPECT_EQ(s.lineClears[3], 3);
    for (std::uint8_t row : s.lineClears) {
        EXPECT_LT(row, 18);  // valid playing-field row index
    }
}

}  // namespace
