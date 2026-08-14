// Demo state: the attract-mode demo struct (src/state/demo_state.h) checked against the existing high-RAM
// layout+census fixture (tests/fixtures/hram_expected.h). Like the sprite-renderer and serial/multiplayer
// units, this ships no fixture of its own - the HRAM layout+census already carries every row it consumes:
// the seven labelled demo rows and the census entries for the two raw-accessed bytes ($FFE4, $FFED).
//
// The struct does not mirror HRAM's byte offsets (two pointer halves collapse into one uint16_t cursor),
// so its fidelity is held by the fixture: every byte the demo machinery owns resolves to exactly one
// field, the labelled rows keep their width, and no byte outside the seven is claimed. All sweeps are
// full-corpus over the fixture - never a subset. Ownership expectations come from
// docs/contracts/demo-state.md.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "state/demo_state.h"
#include "data/demo.h"
#include "data/misc.h"
#include "fixtures/hram_expected.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::DemoInputRecord;
using kirpich::DemoState;
using kirpich::fixtures::HramKind;
using kirpich::fixtures::HramLabel;
using kirpich::fixtures::kHramCensus;
using kirpich::fixtures::kHramLabels;

// Fixture lookups; a miss fails the test rather than reading past the array.
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

const HramLabel* hramRowAt(std::uint16_t addr) {
    for (const auto& r : kHramLabels)
        if (r.address == addr && r.kind != HramKind::Alias) return &r;
    return nullptr;
}

int hramCensusRefOf(std::uint16_t addr) {
    for (const auto& c : kHramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

// Every byte the demo machinery owns, mapped to its port field. All seven carry an hram.asm label; the
// two pointer halves ($FFEB, $FFEC) both resolve to the single uint16_t cursor `nextRecord` (the
// pointer-pair collapse). Seven bytes, six fields.
struct Owned {
    std::uint16_t addr;
    const char*   field;
    const char*   hramLabel;
};
constexpr Owned kOwned[] = {
    {0xFFE4, "activeDemo",      "hDemoNumber"},
    {0xFFE9, "recording",       "hDemoRecording"},
    {0xFFEA, "framesRemaining", "hDemoJoypadTimer"},
    {0xFFEB, "nextRecord",      "hDemoJoypadDataHi"},
    {0xFFEC, "nextRecord",      "hDemoJoypadDataLo"},
    {0xFFED, "demoHeld",        "hDemoJoypadHeld"},
    {0xFFEE, "savedHeld",       "hSavedJoyHeld"},
};

bool isOwned(std::uint16_t a) {
    for (const auto& o : kOwned) if (o.addr == a) return true;
    return false;
}

// (1) HRAM window pins - the seven labelled rows the unit consumes are present at their addresses, the two
// raw-accessed bytes are censused, and no other byte in the demo held/pointer range is. An upstream repin
// that moves any of them fails here.
TEST(DemoState, HramWindowIsPresent) {
    // The seven labelled demo rows, each a single byte at its address.
    struct LRow { const char* name; std::uint16_t addr; };
    constexpr LRow kLabelled[] = {
        {"hDemoNumber", 0xFFE4}, {"hDemoRecording", 0xFFE9}, {"hDemoJoypadTimer", 0xFFEA},
        {"hDemoJoypadDataHi", 0xFFEB}, {"hDemoJoypadDataLo", 0xFFEC}, {"hDemoJoypadHeld", 0xFFED},
        {"hSavedJoyHeld", 0xFFEE},
    };
    for (const auto& r : kLabelled) {
        EXPECT_EQ(hramSizeOf(r.name), 1) << r.name;
        EXPECT_EQ(hramAddrOf(r.name), r.addr) << r.name;
    }

    // The two raw-operand census rows this unit owns: the title-screen countdown select reads $FFE4, and
    // StartDemo clears $FFED by raw operand. Each is reached from exactly one site.
    EXPECT_EQ(hramCensusRefOf(0xFFE4), 1);
    EXPECT_EQ(hramCensusRefOf(0xFFED), 1);

    // Negative guard: no other byte in $FFE9-$FFEE is raw-accessed (every other demo byte is reached only
    // through its symbolic label). If a future repin adds a raw operand here, it must be re-adjudicated.
    for (std::uint16_t a : {0xFFE9, 0xFFEA, 0xFFEB, 0xFFEC, 0xFFEE})
        EXPECT_EQ(hramCensusRefOf(a), 0) << "unexpected raw-operand census at $" << std::hex << a;

    // Neighbour pins: the bytes bracketing this unit belong to other units. $FFE3 / $FFE5 are game-flow
    // labels, $FFE8 is a top-scores label, and the $FFEF gap (size 2) is the serial unit's - none is a
    // demo byte.
    const HramLabel* wipe = hramRowAt(0xFFE3);
    ASSERT_NE(wipe, nullptr);
    EXPECT_EQ(wipe->name, std::string_view("hWipeCounter"));
    const HramLabel* soft = hramRowAt(0xFFE5);
    ASSERT_NE(soft, nullptr);
    EXPECT_EQ(soft->name, std::string_view("hSoftDropCounter"));
    const HramLabel* redraw = hramRowAt(0xFFE8);
    ASSERT_NE(redraw, nullptr);
    EXPECT_EQ(redraw->name, std::string_view("hRedrawTopScoresDuringVBlank"));
    const HramLabel* gap = hramRowAt(0xFFEF);
    ASSERT_NE(gap, nullptr);
    EXPECT_EQ(gap->kind, HramKind::Gap);
    EXPECT_EQ(gap->size, 2);
}

// (2) Struct<->fixture width pins - the labelled rows are one byte each; the scalar members are one byte;
// the two pointer rows (one byte each) collapse into one uint16_t cursor; the two held sets are the same
// type a DemoInputRecord carries; the record has a defaulted ==.
TEST(DemoState, StructWidthsMatchFixture) {
    // Each labelled address is a single-byte fixture row.
    for (const auto& o : kOwned)
        EXPECT_EQ(hramSizeOf(o.hramLabel), 1) << o.hramLabel;

    const DemoState s{};
    EXPECT_EQ(sizeof(s.activeDemo), 1u);
    EXPECT_EQ(sizeof(s.recording), 1u);
    EXPECT_EQ(sizeof(s.framesRemaining), 1u);
    static_assert(sizeof(ActiveDemo) == 1);

    // The pointer-pair collapse: two one-byte fixture rows ($FFEB, $FFEC) map to one two-byte cursor.
    EXPECT_EQ(sizeof(s.nextRecord), 2u);
    EXPECT_EQ(hramSizeOf("hDemoJoypadDataHi"), 1);
    EXPECT_EQ(hramSizeOf("hDemoJoypadDataLo"), 1);

    // The held sets are exactly the type a DemoInputRecord carries - the demo-data action vocabulary, not
    // a raw joypad byte.
    static_assert(std::is_same_v<decltype(DemoState::demoHeld), decltype(DemoInputRecord::held)>);
    static_assert(std::is_same_v<decltype(DemoState::savedHeld), decltype(DemoInputRecord::held)>);

    DemoState other{};
    other.activeDemo = ActiveDemo::TYPE_A;
    EXPECT_FALSE(other == s);   // the defaulted == distinguishes them
}

// (3) Per-byte field resolution - every byte the unit owns resolves to exactly one field, both pointer
// halves resolve to nextRecord, and no byte outside the seven is claimed here.
TEST(DemoState, EveryOwnedByteResolvesToOneField) {
    // Seven owned bytes, no duplicate addresses.
    ASSERT_EQ(std::size(kOwned), 7u);
    for (std::size_t i = 0; i < std::size(kOwned); ++i)
        for (std::size_t j = i + 1; j < std::size(kOwned); ++j)
            EXPECT_NE(kOwned[i].addr, kOwned[j].addr)
                << "duplicate owned address $" << std::hex << kOwned[i].addr;

    // Each owned byte is a labelled Field carrying its upstream name.
    for (const auto& o : kOwned) {
        const HramLabel* row = hramRowAt(o.addr);
        ASSERT_NE(row, nullptr) << o.field;
        EXPECT_EQ(row->kind, HramKind::Field) << o.field;
        EXPECT_EQ(row->name, std::string_view(o.hramLabel)) << o.field;
    }

    // Both pointer halves resolve to the one cursor field.
    int cursorHalves = 0;
    for (const auto& o : kOwned)
        if (std::string_view(o.field) == "nextRecord") ++cursorHalves;
    EXPECT_EQ(cursorHalves, 2);

    // Negative guard: the bytes bracketing the unit are NOT owned here - the game-flow / top-scores labels
    // below $FFE9 and the serial gap above $FFEE. A future unit that widens this range without updating the
    // map trips this.
    for (std::uint16_t a : {0xFFE3, 0xFFE5, 0xFFE6, 0xFFE7, 0xFFE8, 0xFFEF, 0xFFF0})
        EXPECT_FALSE(isOwned(a)) << "byte $" << std::hex << a << " must not be owned by the demo unit";
}

// (4) Reset restores boot state - mutate every member, reset, compare to fresh; pin the boot values.
TEST(DemoState, ResetRestoresBootState) {
    DemoState s{};
    s.activeDemo = ActiveDemo::TYPE_A;
    s.recording = kirpich::kDemoRecordingEnabledMagic;
    s.framesRemaining = 0x2A;
    s.nextRecord = 29;
    s.demoHeld = kirpich::heldActions({kirpich::Action::MoveLeft, kirpich::Action::RotateClockwise});
    s.savedHeld = kirpich::heldActions({kirpich::Action::SoftDrop});

    EXPECT_FALSE(s == DemoState{});   // the mutations took
    s.reset();
    EXPECT_TRUE(s == DemoState{});    // back to boot state

    // Boot pins.
    const DemoState boot{};
    EXPECT_EQ(boot.activeDemo, ActiveDemo::NONE);
    EXPECT_EQ(boot.recording, 0);                 // != the magic - playback mode, not recording
    EXPECT_NE(boot.recording, kirpich::kDemoRecordingEnabledMagic);
    EXPECT_EQ(boot.framesRemaining, 0);
    EXPECT_EQ(boot.nextRecord, 0);
    EXPECT_TRUE(boot.demoHeld == retropp::ActionSet{});
    EXPECT_TRUE(boot.savedHeld == retropp::ActionSet{});
}

// (5) Wire-value pins - the ActiveDemo codes are the hand-typed demo numbers; the recording magic
// cross-pins the misc-data emit; the timeline counts fit the cursor.
TEST(DemoState, WireValuePins) {
    EXPECT_EQ(static_cast<std::uint8_t>(ActiveDemo::NONE), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(ActiveDemo::TYPE_B), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(ActiveDemo::TYPE_A), 2);

    // The recording enable value is $FF, tied to the parser-emitted misc constant (guards the link
    // against the misc-data emit drifting).
    EXPECT_EQ(kirpich::kDemoRecordingEnabledMagic, 0xFF);

    // Both timelines' record counts fit the uint16_t cursor (the sizing link to the demo-data counts).
    EXPECT_LE(kirpich::kTypeADemoInputCount, 0xFFFF);
    EXPECT_LE(kirpich::kTypeBDemoInputCount, 0xFFFF);
    EXPECT_EQ(kirpich::kTypeADemoInputCount, 128);
    EXPECT_EQ(kirpich::kTypeBDemoInputCount, 80);
}

}  // namespace
