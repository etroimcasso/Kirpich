// Serial / multiplayer state: the link-mode struct (src/state/multiplayer_state.h) checked against the
// existing high-RAM layout+census fixture (tests/fixtures/hram_expected.h). Like the sprite-renderer
// unit, this ships no fixture of its own - the HRAM layout+census already carries every row it consumes:
// the twelve labelled serial/MP rows, the five gap rows the unlabelled bytes fall in, and the census
// entry for every raw-accessed byte.
//
// The struct does not mirror HRAM's byte offsets, so its fidelity is held by the fixture: every byte the
// link mode owns must resolve to exactly one field, the labelled rows must keep their width, and the
// bytes just past this unit's range must NOT be claimed here. All sweeps are full-corpus over the
// fixture - never a subset. Ownership expectations come from docs/contracts/serial-multiplayer-state.md.

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include <kirpich/serial_role.h>
#include <kirpich/serial_state.h>

#include "state/multiplayer_state.h"
#include "fixtures/hram_expected.h"

namespace {

using kirpich::MultiplayerState;
using kirpich::RoundOutcome;
using kirpich::SerialRole;
using kirpich::SerialState;
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

// The layout region (Field or Gap) whose [address, address+size) span contains `a`.
const HramLabel* hramRowContaining(std::uint16_t a) {
    for (const auto& r : kHramLabels) {
        if (r.kind == HramKind::Alias) continue;
        if (r.address <= a && a < r.address + r.size) return &r;
    }
    return nullptr;
}

int hramCensusRefOf(std::uint16_t addr) {
    for (const auto& c : kHramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

// Every byte the link mode owns, mapped to its port field. `hramLabel` is the upstream label at that
// address, or "" for the unlabelled bytes the game reaches by a raw numeric operand (which fall inside a
// gap row). Twelve labelled + fourteen unlabelled = the unit's twenty-six bytes.
struct Owned {
    std::uint16_t addr;
    const char*   field;
    const char*   hramLabel;   // "" if the byte is an unlabelled gap byte
};
constexpr Owned kOwned[] = {
    {0xFFAC, "marioStartHeight",      "hMarioStartHeight"},
    {0xFFAD, "luigiStartHeight",      "hLuigiStartHeight"},
    {0xFFB1, "outgoingStatus",        ""},
    {0xFFC5, "isMultiplayer",         "hIsMultiplayer"},
    {0xFFCB, "role",                  "hSerialRole"},
    {0xFFCC, "transferCompleted",     "hSerialInterruptTriggered"},
    {0xFFCD, "protocolState",         "hSerialState"},
    {0xFFCE, "sendPending",           ""},
    {0xFFCF, "tx",                    "hSerialTx"},
    {0xFFD0, "rx",                    "hSerialRx"},
    {0xFFD1, "roundOutcome",          ""},
    {0xFFD2, "garbageRowsReceived",   ""},
    {0xFFD3, "garbageRowsPending",    ""},
    {0xFFD4, "garbageWipeActive",     ""},
    {0xFFD5, "linesGoalReached",      ""},
    {0xFFD6, "subsequentRound",       ""},
    {0xFFD7, "ourWins",               "hOurWins"},
    {0xFFD8, "theirWins",             "hTheirWins"},
    {0xFFD9, "advantageOurs",         ""},
    {0xFFDA, "advantageTheirs",       ""},
    {0xFFDB, "deuce",                 ""},
    {0xFFDC, "garbageRowsToSend",     ""},
    {0xFFEF, "winDoesNotCount",       ""},
    {0xFFF0, "musicSelectionChanged", ""},
    {0xFFF1, "savedTx",               "hSavedSerialTx"},
    {0xFFF2, "savedRx",               "hSavedSerialRx"},
};

bool isOwned(std::uint16_t a) {
    for (const auto& o : kOwned) if (o.addr == a) return true;
    return false;
}

// (1) HRAM window pins - the layout rows the unit consumes are present, and the census reaches every
// raw-accessed byte it owns. An upstream repin that moves any of them fails here.
TEST(MultiplayerState, HramWindowIsPresent) {
    // The twelve labelled serial/MP rows, each a single byte at its address.
    struct LRow { const char* name; std::uint16_t addr; };
    constexpr LRow kLabelled[] = {
        {"hMarioStartHeight", 0xFFAC}, {"hLuigiStartHeight", 0xFFAD}, {"hIsMultiplayer", 0xFFC5},
        {"hSerialRole", 0xFFCB}, {"hSerialInterruptTriggered", 0xFFCC}, {"hSerialState", 0xFFCD},
        {"hSerialTx", 0xFFCF}, {"hSerialRx", 0xFFD0}, {"hOurWins", 0xFFD7}, {"hTheirWins", 0xFFD8},
        {"hSavedSerialTx", 0xFFF1}, {"hSavedSerialRx", 0xFFF2},
    };
    for (const auto& r : kLabelled) {
        EXPECT_EQ(hramSizeOf(r.name), 1) << r.name;
        EXPECT_EQ(hramAddrOf(r.name), r.addr) << r.name;
    }

    // The five gap rows the fourteen unlabelled bytes fall inside.
    struct GRow { std::uint16_t addr; std::uint16_t size; };
    constexpr GRow kGaps[] = {
        {0xFFB1, 5}, {0xFFCE, 1}, {0xFFD1, 6}, {0xFFD9, 8}, {0xFFEF, 2},
    };
    for (const auto& g : kGaps) {
        const HramLabel* row = hramRowAt(g.addr);
        ASSERT_NE(row, nullptr) << "no layout row at $" << std::hex << g.addr;
        EXPECT_EQ(row->kind, HramKind::Gap) << std::hex << g.addr;
        EXPECT_EQ(row->size, g.size) << std::hex << g.addr;
    }

    // Every one of the fourteen unlabelled bytes is genuinely raw-accessed (refCount > 0), or the fold-in
    // no longer exists after a repin.
    constexpr std::uint16_t kUnlabelled[] = {
        0xFFB1, 0xFFCE, 0xFFD1, 0xFFD2, 0xFFD3, 0xFFD4, 0xFFD5, 0xFFD6,
        0xFFD9, 0xFFDA, 0xFFDB, 0xFFDC, 0xFFEF, 0xFFF0,
    };
    for (std::uint16_t a : kUnlabelled)
        EXPECT_GT(hramCensusRefOf(a), 0) << "unlabelled byte $" << std::hex << a << " not censused";

    // Corner refCount pins: the master send-request flag, the round-outcome byte (whose count folds the
    // one long-form ldh [$FFD1] access), the in-round status byte, and the music-select redraw flag.
    EXPECT_EQ(hramCensusRefOf(0xFFCE), 15);
    EXPECT_EQ(hramCensusRefOf(0xFFD1), 9);
    EXPECT_EQ(hramCensusRefOf(0xFFB1), 5);
    EXPECT_EQ(hramCensusRefOf(0xFFF0), 3);
}

// (2) Width pins - every labelled field's fixture row is one byte and the port member is one byte wide;
// the enum-typed members are single ROM-equivalent bytes; the record has a defaulted ==.
TEST(MultiplayerState, LabelledFieldWidthsMatchFixture) {
    // Each labelled address is a single-byte fixture row (the unlabelled bytes are single bytes of their
    // gap rows, checked in test 1).
    for (const auto& o : kOwned)
        if (o.hramLabel[0] != '\0')
            EXPECT_EQ(hramSizeOf(o.hramLabel), 1) << o.hramLabel;

    const MultiplayerState s{};
    EXPECT_EQ(sizeof(s.role), 1u);
    EXPECT_EQ(sizeof(s.protocolState), 1u);
    EXPECT_EQ(sizeof(s.roundOutcome), 1u);
    static_assert(sizeof(SerialRole) == 1);
    static_assert(sizeof(SerialState) == 1);
    static_assert(sizeof(RoundOutcome) == 1);

    // The single-byte scalar members map to single ROM bytes.
    EXPECT_EQ(sizeof(s.marioStartHeight), 1u);
    EXPECT_EQ(sizeof(s.outgoingStatus), 1u);
    EXPECT_EQ(sizeof(s.tx), 1u);
    EXPECT_EQ(sizeof(s.rx), 1u);
    EXPECT_EQ(sizeof(s.savedTx), 1u);

    MultiplayerState other{};
    other.isMultiplayer = true;
    EXPECT_FALSE(other == s);            // the defaulted == distinguishes them
}

// (3) Per-byte field resolution - every byte the unit owns resolves to exactly one field, and the bytes
// just past its range are NOT claimed here. This is the ownership boundary made testable.
TEST(MultiplayerState, EveryOwnedByteResolvesToOneField) {
    // Twelve labelled + fourteen unlabelled = twenty-six owned bytes, no duplicates.
    ASSERT_EQ(std::size(kOwned), 26u);
    for (std::size_t i = 0; i < std::size(kOwned); ++i)
        for (std::size_t j = i + 1; j < std::size(kOwned); ++j)
            EXPECT_NE(kOwned[i].addr, kOwned[j].addr)
                << "duplicate owned address $" << std::hex << kOwned[i].addr;

    // Each labelled byte carries the expected upstream label; each unlabelled byte falls inside a gap
    // row (no label of its own).
    for (const auto& o : kOwned) {
        if (o.hramLabel[0] != '\0') {
            const HramLabel* row = hramRowAt(o.addr);
            ASSERT_NE(row, nullptr) << o.field;
            EXPECT_EQ(row->kind, HramKind::Field) << o.field;
            EXPECT_EQ(row->name, std::string_view(o.hramLabel)) << o.field;
        } else {
            const HramLabel* row = hramRowContaining(o.addr);
            ASSERT_NE(row, nullptr) << o.field;
            EXPECT_EQ(row->kind, HramKind::Gap) << o.field;
            EXPECT_TRUE(row->name.empty()) << o.field;
        }
    }

    // Negative guard: the $FFD9 gap runs to $FFE0, but only $FFD9-$FFDC belong to this unit. $FFDD-$FFDF
    // are dead un-censused bytes and $FFE0 is the game-flow score-print flag (GameFlowState::
    // scorePrintFlag) - none is owned here. A future unit that widens this range without updating the
    // map trips this.
    for (std::uint16_t a : {0xFFDD, 0xFFDE, 0xFFDF, 0xFFE0})
        EXPECT_FALSE(isOwned(a)) << "byte $" << std::hex << a << " must not be owned by the link mode";
    EXPECT_EQ(hramCensusRefOf(0xFFDD), 0);   // truly dead, not merely unclaimed
    EXPECT_EQ(hramCensusRefOf(0xFFDE), 0);
    EXPECT_EQ(hramCensusRefOf(0xFFDF), 0);
    EXPECT_GT(hramCensusRefOf(0xFFE0), 0);   // censused, but owned by the game-flow state, not here
}

// (4) Reset restores boot state - mutate every member, reset, compare to fresh; pin the boot values that
// are not a plain zero scalar.
TEST(MultiplayerState, ResetRestoresBootState) {
    MultiplayerState s{};
    s.marioStartHeight = 5;
    s.luigiStartHeight = 3;
    s.outgoingStatus = 0x80 | 4;
    s.isMultiplayer = true;
    s.role = SerialRole::MASTER;
    s.transferCompleted = 0x1B;
    s.protocolState = SerialState::EXCHANGE;
    s.sendPending = 1;
    s.tx = 0x60;
    s.rx = 0xFF;
    s.roundOutcome = RoundOutcome::WE_WON;
    s.garbageRowsReceived = 4;
    s.garbageRowsPending = 0x80 | 2;
    s.garbageWipeActive = true;
    s.linesGoalReached = true;
    s.subsequentRound = true;
    s.ourWins = 5;
    s.theirWins = 2;
    s.advantageOurs = 1;
    s.advantageTheirs = 1;
    s.deuce = 1;
    s.garbageRowsToSend = 4;
    s.winDoesNotCount = true;
    s.musicSelectionChanged = true;
    s.savedTx = 0x94;
    s.savedRx = 0x39;

    EXPECT_FALSE(s == MultiplayerState{});   // the mutations took
    s.reset();
    EXPECT_TRUE(s == MultiplayerState{});     // back to boot state

    // Boot pins on the non-scalar members.
    const MultiplayerState boot{};
    EXPECT_EQ(boot.protocolState, SerialState::HANDSHAKE);           // 0 is a valid enumerator
    EXPECT_EQ(static_cast<std::uint8_t>(boot.role), 0);             // 0 is "unset", not a valid role
    EXPECT_NE(static_cast<std::uint8_t>(SerialRole::MASTER), 0);    // 0 really is outside the role values
    EXPECT_NE(static_cast<std::uint8_t>(SerialRole::SLAVE), 0);
    EXPECT_EQ(boot.roundOutcome, RoundOutcome::NONE);
    EXPECT_FALSE(boot.isMultiplayer);
}

// (5) Wire-value pins - the RoundOutcome codes are the two hand-typed round-end bytes, and the SerialRole
// codes cross-pin the contract's wire-vocabulary table against the generated enum header.
TEST(MultiplayerState, WireValuePins) {
    EXPECT_EQ(static_cast<std::uint8_t>(RoundOutcome::NONE), 0x00);
    EXPECT_EQ(static_cast<std::uint8_t>(RoundOutcome::WE_LOST), 0x77);
    EXPECT_EQ(static_cast<std::uint8_t>(RoundOutcome::WE_WON), 0xAA);

    EXPECT_EQ(static_cast<std::uint8_t>(SerialRole::MASTER), 0x29);
    EXPECT_EQ(static_cast<std::uint8_t>(SerialRole::SLAVE), 0x55);
}

}  // namespace
