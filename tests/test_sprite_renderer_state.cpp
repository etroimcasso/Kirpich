// Sprite-renderer state (the $C200 sprite-object array): the live-slot struct
// (src/state/sprite_renderer_state.h) checked against the two existing layout/census fixtures. This
// unit ships no fixture of its own - the work-RAM census (tests/fixtures/wram_expected.h) already
// carries the twelve $C2xx rows the game reaches, and the high-RAM layout+census
// (tests/fixtures/hram_expected.h) already carries the renderer's HRAM working window.
//
// The struct does not mirror the array's byte offsets, so its fidelity is held by the fixtures: every
// censused $C2xx byte must resolve to a modelled slot offset (with a guard against the seven dropped
// offsets), the renderer's HRAM bytes must still be present, and the slot count must match the array
// window. All sweeps are full-corpus over the fixtures - never a subset. Ownership expectations come
// from docs/contracts/sprite-renderer-state.md.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include <kirpich/sprite_id.h>

#include "state/sprite_renderer_state.h"
#include "fixtures/wram_expected.h"
#include "fixtures/hram_expected.h"

namespace {

using kirpich::SpriteId;
using kirpich::SpriteRendererState;
using kirpich::SpriteSlot;
using kirpich::fixtures::HramKind;
using kirpich::fixtures::HramLabel;
using kirpich::fixtures::kHramCensus;
using kirpich::fixtures::kHramLabels;
using kirpich::fixtures::kWramCensus;
using kirpich::fixtures::kWramLabels;
using kirpich::fixtures::WramKind;

// The sprite-object array: base is the renderer's $C200 literal, top is wPieceList in the layout
// fixture ($C300); the $10-byte stride gives the slot count.
constexpr std::uint16_t kArrayBase = 0xC200;

int wramCensusRefOf(std::uint16_t addr) {
    for (const auto& c : kWramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

int hramCensusRefOf(std::uint16_t addr) {
    for (const auto& c : kHramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

// Layout lookups; a miss fails the test rather than reading past the array.
std::uint16_t wramAddrOf(std::string_view name) {
    for (const auto& r : kWramLabels)
        if (r.name == name && r.kind == WramKind::Field) return r.address;
    ADD_FAILURE() << "wram field not found in fixture: " << name;
    return 0xFFFF;
}

const HramLabel* hramRowAt(std::uint16_t addr) {
    for (const auto& r : kHramLabels)
        if (r.address == addr && r.kind != HramKind::Alias) return &r;
    return nullptr;
}

std::uint16_t hramSizeOf(std::string_view name) {
    for (const auto& r : kHramLabels)
        if (r.name == name && r.kind == HramKind::Field) return r.size;
    ADD_FAILURE() << "hram field not found in fixture: " << name;
    return 0;
}

std::uint16_t hramAddrOf(std::string_view name) {
    for (const auto& r : kHramLabels)
        if (r.name == name && r.kind == HramKind::Field) return r.address;
    ADD_FAILURE() << "hram field not found in fixture: " << name;
    return 0xFFFF;
}

bool isModeledOffset(int off) {
    return off == 0x0 || off == 0x1 || off == 0x2 || off == 0x3 || off == 0x4 ||
           off == 0x5 || off == 0x6 || off == 0xE || off == 0xF;
}

// (1) Census offsets resolve to slot fields. Every censused byte of the array must land on one of the
// nine modelled offsets; a byte at a dropped offset (+7..+$D) fails here (the dropped-offset guard).
// Additionally pin the offset set observed today and the two corner rows.
TEST(SpriteRendererState, CensusOffsetsResolveToSlotFields) {
    const std::uint16_t arrayTop = wramAddrOf("wPieceList");   // $C300, from the fixture
    EXPECT_EQ(arrayTop, 0xC300);

    bool observed[0x10] = {};
    bool sawAny = false;
    for (const auto& c : kWramCensus) {
        if (c.address < kArrayBase || c.address >= arrayTop) continue;
        sawAny = true;
        const int off = (c.address - kArrayBase) % 0x10;
        EXPECT_TRUE(isModeledOffset(off))
            << "censused $" << std::hex << c.address << " at slot offset +" << off
            << " is not a modelled slot field (dropped offsets +7..+$D must never be reached)";
        observed[off] = true;
    }
    ASSERT_TRUE(sawAny) << "no $C2xx census rows found - the array window is empty";

    // The offset set the corpus reaches today is exactly {0,1,2,3,6,$E}.
    for (int off = 0; off < 0x10; ++off) {
        const bool want = (off == 0x0 || off == 0x1 || off == 0x2 || off == 0x3 ||
                           off == 0x6 || off == 0xE);
        EXPECT_EQ(observed[off], want) << "offset +" << std::hex << off;
    }

    // Corner pins: slot 0's status byte is the most-written byte of the array; $C266 is slot 6's
    // attribute byte (offset +6), the dancer OBP1 write.
    EXPECT_EQ(wramCensusRefOf(0xC200), 24);
    EXPECT_EQ((0xC266 - kArrayBase) / 0x10, 6);   // slot 6
    EXPECT_EQ((0xC266 - kArrayBase) % 0x10, 6);   // byte +6
    EXPECT_GT(wramCensusRefOf(0xC266), 0);
}

// (2) HRAM window pins. The renderer's working bytes must still be present in the layout+census
// fixture, or an upstream repin has moved the mechanism this unit adjudicates.
TEST(SpriteRendererState, RendererHramWindowIsPresent) {
    // The 7-byte slot-head working copy gap at $FF86, and the per-tile working-attr gap at $FF94.
    const HramLabel* gap86 = hramRowAt(0xFF86);
    ASSERT_NE(gap86, nullptr) << "no layout row at $FF86";
    EXPECT_EQ(gap86->kind, HramKind::Gap);
    EXPECT_EQ(gap86->size, 7);            // the ld b,7 copy-loop bound

    const HramLabel* gap94 = hramRowAt(0xFF94);
    ASSERT_NE(gap94, nullptr) << "no layout row at $FF94";
    EXPECT_EQ(gap94->kind, HramKind::Gap);
    EXPECT_EQ(gap94->size, 1);

    // The ten labelled hSpriteRenderer* registers, each a single byte, at $FF8D-$FF97 ($FF94 gaps).
    struct Row { const char* name; std::uint16_t addr; };
    constexpr Row kRegs[] = {
        {"hSpriteRendererOAMHi", 0xFF8D}, {"hSpriteRendererOAMLo", 0xFF8E},
        {"hSpriteRendererCount", 0xFF8F}, {"hSpriteRendererOffsetY", 0xFF90},
        {"hSpriteRendererOffsetX", 0xFF91}, {"hSpriteRendererObjX", 0xFF92},
        {"hSpriteRendererObjY", 0xFF93}, {"hSpriteRendererVisible", 0xFF95},
        {"hSpriteRendererSpriteHi", 0xFF96}, {"hSpriteRendererSpriteLo", 0xFF97},
    };
    for (const auto& r : kRegs) {
        EXPECT_EQ(hramSizeOf(r.name), 1) << r.name;
        EXPECT_EQ(hramAddrOf(r.name), r.addr) << r.name;
    }

    // The census reaches the 7-byte working copy ($FF86-$FF8C), the working-attr byte ($FF94), and
    // the _LookupTile interface ($FFB2-$FFB5) - the renderer's HRAM working bytes.
    for (std::uint16_t a : {0xFF86, 0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C})
        EXPECT_GT(hramCensusRefOf(a), 0) << "working-copy byte $" << std::hex << a << " not censused";
    EXPECT_GT(hramCensusRefOf(0xFF94), 0);
    for (std::uint16_t a : {0xFFB2, 0xFFB3, 0xFFB4, 0xFFB5})
        EXPECT_GT(hramCensusRefOf(a), 0) << "_LookupTile byte $" << std::hex << a << " not censused";
}

// (3) Slot-shape pins. The slot count matches the array window arithmetic read from the fixture, the
// record has a defaulted ==, and a default slot is the boot state.
TEST(SpriteRendererState, SlotShapeMatchesTheWindow) {
    const std::uint16_t arrayTop = wramAddrOf("wPieceList");     // $C300
    const SpriteRendererState s{};
    EXPECT_EQ(s.slots.size(), 16u);
    EXPECT_EQ((arrayTop - kArrayBase) / 0x10, s.slots.size());   // ($C300 - $C200) / $10 == 16

    // Defaulted operator== and boot slot.
    const SpriteSlot boot{};
    EXPECT_EQ(s.slots[0], boot);
    EXPECT_FALSE(boot.hidden);
    EXPECT_EQ(boot.y, 0);
    EXPECT_EQ(boot.x, 0);
    EXPECT_EQ(static_cast<std::uint8_t>(boot.spriteId), 0);
    EXPECT_FALSE(boot.behindBg);
    EXPECT_FALSE(boot.yflip);
    EXPECT_FALSE(boot.xflip);
    EXPECT_FALSE(boot.palette1);
    EXPECT_EQ(boot.animCounter, 0);
    EXPECT_EQ(boot.animReload, 0);

    SpriteSlot other{};
    other.hidden = true;
    EXPECT_NE(other, boot);          // the defaulted == distinguishes them
}

// (4) Reset restores boot state. Mutate every field across several slots, reset, compare to fresh.
TEST(SpriteRendererState, ResetRestoresBootState) {
    SpriteRendererState s{};

    s.slots[0] = SpriteSlot{.hidden = false, .y = 40, .x = 80, .spriteId = SpriteId::T_0,
                            .behindBg = false, .yflip = false, .xflip = true, .palette1 = false,
                            .animCounter = 0, .animReload = 0};
    s.slots[1] = SpriteSlot{.hidden = true, .y = 24, .x = 120, .spriteId = SpriteId::I_2,
                            .behindBg = true, .yflip = true, .xflip = false, .palette1 = false,
                            .animCounter = 0, .animReload = 0};
    s.slots[6] = SpriteSlot{.hidden = false, .y = 96, .x = 32, .spriteId = SpriteId::DANCER_1,
                            .behindBg = false, .yflip = false, .xflip = false, .palette1 = true,
                            .animCounter = 0x1C, .animReload = 0x1C};
    s.slots[15] = SpriteSlot{.hidden = true, .y = 8, .x = 8, .spriteId = SpriteId::ROCKET_S,
                             .behindBg = false, .yflip = false, .xflip = false, .palette1 = false,
                             .animCounter = 1, .animReload = 2};

    EXPECT_FALSE(s == SpriteRendererState{});   // the mutations took
    s.reset();
    EXPECT_TRUE(s == SpriteRendererState{});     // back to boot state
}

// (5) SpriteId link pins. Byte +3 is the typed SpriteId; it carries piece-rotation ids (slots 0/1) and
// the score-tier rocket ids.
TEST(SpriteRendererState, SpriteIdLink) {
    static_assert(std::is_same_v<decltype(SpriteSlot{}.spriteId), SpriteId>);

    SpriteSlot slot{};
    // A piece rotation: the four orientations are four consecutive ids.
    slot.spriteId = SpriteId::T_0;
    EXPECT_EQ(static_cast<std::uint8_t>(slot.spriteId), 0x18);
    slot.spriteId = SpriteId::T_3;
    EXPECT_EQ(static_cast<std::uint8_t>(slot.spriteId), 0x1B);
    // A rocket-tier id copied into a slot for the celebration scene.
    slot.spriteId = SpriteId::ROCKET_M;
    EXPECT_EQ(static_cast<std::uint8_t>(slot.spriteId), 0x59);
}

}  // namespace
