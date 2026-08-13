// Audio state (the Audio-RAM boundary): this unit ships no struct. The port plays the original music
// and SFX by running the ROM's own sound driver as embedded code on the engine's virtual sound CPU,
// so the audio bytes at $DF70-$DFFF are the port's state inside that machine's RAM, never mirrored
// into C++. What is testable here is the BOUNDARY between game state and driver state: which work-RAM
// bytes the game itself reaches (proven by the census in wram_expected.h) and the fact that every one
// of them belongs to exactly one state surface.
//
// Ownership expectations come from docs/contracts/audio-state.md (the audio window) and the sibling
// state contracts (the rest of work RAM). All sweeps are full-corpus over the fixture - never a subset.

#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "data/music.h"
#include "data/sfx.h"
#include "fixtures/wram_expected.h"

namespace {

using kirpich::MusicId;
using kirpich::NoiseSfxId;
using kirpich::SquareSfxId;
using kirpich::WaveSfxId;
using kirpich::fixtures::kWramCensus;
using kirpich::fixtures::kWramLabels;
using kirpich::fixtures::WramKind;

// [address, address + size) for a labelled layout field, looked up in the fixture. A miss fails the
// test rather than silently owning nothing.
struct Window {
    std::uint16_t lo;
    std::uint16_t hi;          // one past the last owned byte
    std::string_view role;
};

Window fieldWindow(std::string_view name, std::string_view role) {
    for (const auto& r : kWramLabels)
        if (r.name == name && r.kind == WramKind::Field)
            return {r.address, static_cast<std::uint16_t>(r.address + r.size), role};
    ADD_FAILURE() << "field not found in fixture: " << name;
    return {0, 0, role};
}

Window byteWindow(std::uint16_t addr, std::string_view role) {
    return {addr, static_cast<std::uint16_t>(addr + 1), role};
}

int censusRefOf(std::uint16_t addr) {
    for (const auto& c : kWramCensus)
        if (c.address == addr) return c.refCount;
    return 0;
}

bool inList(std::uint16_t a, std::initializer_list<std::uint16_t> xs) {
    for (std::uint16_t x : xs)
        if (a == x) return true;
    return false;
}

// The whole-work-RAM ownership map. Labelled fields take their extents from the layout fixture so
// they cannot drift from wram.asm; the unlabelled windows (sprite / board / staging), the folded
// engine-state flags, and the boot/stack mechanism bytes are the hand-authored boundary this test
// asserts. The windows are disjoint by construction; the test proves every census byte lands in
// exactly one.
std::vector<Window> ownershipMap() {
    std::vector<Window> w;

    // Engine-state fields (score, line-clear pipeline, piece bag, ...): extents from the fixture.
    for (std::string_view name : {"wOAMBuffer", "wScore", "wLineClearsList", "wLineClearStats",
                                  "wDoublesCount", "wTriplesCount", "wTetrisCount", "wSoftDropPoints",
                                  "wSoftDropPointsBCD", "wScoreboardState", "wHidePreviewPiece",
                                  "wPieceList"})
        w.push_back(fieldWindow(name, "engine-state"));

    // Engine-state flags that live in unlabelled gaps of the layout (raw-addressed by the game).
    for (std::uint16_t a : {0xC0C6, 0xC0C7, 0xC0CE})
        w.push_back(byteWindow(a, "engine-state flag"));

    // The audio interface bytes (this unit): four cue mailboxes + the pause command + the current-
    // music read-back. Extents from the fixture; the rest of $DF70-$DFFF is driver-private and never
    // appears in a game-side census.
    for (std::string_view name : {"wPauseUnpauseSound", "wNewSquareSFXID", "wNewMusicID",
                                  "wCurrentMusicID", "wNewWaveSFXID", "wNewNoiseSFXID"})
        w.push_back(fieldWindow(name, "audio cue/slot"));

    // Unlabelled game windows the layout leaves as anonymous gaps.
    w.push_back({0xC200, 0xC300, "sprite objects"});
    w.push_back({0xC400, 0xC40A, "playfield staging"});
    w.push_back({0xC800, 0xCC00, "playfield board"});

    // Top-score tables: extents from the fixture.
    w.push_back(fieldWindow("wTypeBTopScores", "top scores"));
    w.push_back(fieldWindow("wTypeATopScores", "top scores"));

    // Boot/stack mechanism (not state): the stack top / bank-0 clear, and the upper-page clear.
    w.push_back(byteWindow(0xCFFF, "mechanism"));
    w.push_back(byteWindow(0xDFFF, "mechanism"));

    return w;
}

// (1) Census integrity - the whole table, no subset.
TEST(AudioState, CensusIsWellFormed) {
    ASSERT_FALSE(kWramCensus.empty());
    std::uint32_t prev = 0;
    bool first = true;
    for (const auto& c : kWramCensus) {
        EXPECT_GT(c.refCount, 0) << "census $" << std::hex << c.address << " has no accesses";
        EXPECT_GE(c.address, 0xC000) << std::hex << c.address;   // work RAM starts at $C000
        EXPECT_LT(c.address, 0xE000) << std::hex << c.address;   // ... and ends at $DFFF
        if (!first) EXPECT_LT(prev, c.address) << "census not strictly ascending near $"
                                               << std::hex << c.address;
        prev = c.address;
        first = false;
    }
}

// (2) Whole-map boundary guard - every census byte resolves to exactly one owner. An unowned gap
// byte (nobody's state) or a doubly-owned byte (an overlap in the map) fails here. This is the
// per-byte boundary between this unit's audio window and every other state surface, made testable.
TEST(AudioState, EveryCensusByteHasExactlyOneOwner) {
    const std::vector<Window> owners = ownershipMap();
    for (const auto& c : kWramCensus) {
        int hits = 0;
        std::string_view role;
        for (const auto& o : owners)
            if (o.lo <= c.address && c.address < o.hi) {
                ++hits;
                role = o.role;
            }
        EXPECT_EQ(hits, 1) << "census $" << std::hex << c.address << " has " << std::dec << hits
                           << " owners" << (hits == 1 ? "" : " (want exactly 1)");
    }
}

// (3) Audio-window pins - the six interface bytes are present, and NOTHING else in $DF70-$DFFF is
// game-reachable. The negative half is the machine-guard on the driver-private set: if a seventh
// audio byte ever shows up in a game operand, this fails.
TEST(AudioState, AudioWindowIsExactlyTheSixInterfaceBytes) {
    // The six interface bytes, each reached by game code.
    for (std::uint16_t a : {0xDF7F, 0xDFE0, 0xDFE8, 0xDFE9, 0xDFF0, 0xDFF8})
        EXPECT_GT(censusRefOf(a), 0) << "interface byte $" << std::hex << a << " is not censused";

    // The pause command is written five times (1 pause + 1 = the contract's five sites).
    EXPECT_EQ(censusRefOf(0xDF7F), 5);

    // No other $DF70-$DFFF byte is game-reachable. $DFFF is the boot RAM-clear top (a mechanism
    // address that happens to sit in the driver's private tail), not audio state.
    for (const auto& c : kWramCensus)
        if (0xDF70 <= c.address && c.address <= 0xDFFF)
            EXPECT_TRUE(inList(c.address, {0xDF7F, 0xDFE0, 0xDFE8, 0xDFE9, 0xDFF0, 0xDFF8, 0xDFFF}))
                << "unexpected audio-window census byte $" << std::hex << c.address
                << " (driver-private bytes must not appear in a game-side census)";
}

// (4) Cue-vocabulary pins - the enums the cue lanes carry, linking the audio window to the existing
// id sets. A cue mailbox holds one of these; 0 is the no-op in every lane.
TEST(AudioState, CueVocabularyWireValues) {
    static_assert(static_cast<std::uint8_t>(MusicId::NONE) == 0x00);
    static_assert(static_cast<std::uint8_t>(MusicId::STOP) == 0xFF);
    static_assert(static_cast<std::uint8_t>(SquareSfxId::NONE) == 0x00);
    static_assert(static_cast<std::uint8_t>(NoiseSfxId::NONE) == 0x00);
    static_assert(static_cast<std::uint8_t>(WaveSfxId::NONE) == 0x00);

    EXPECT_EQ(static_cast<std::uint8_t>(MusicId::STOP), 0xFF);
    EXPECT_EQ(static_cast<std::uint8_t>(SquareSfxId::NONE), 0x00);
    EXPECT_EQ(static_cast<std::uint8_t>(NoiseSfxId::NONE), 0x00);
    EXPECT_EQ(static_cast<std::uint8_t>(WaveSfxId::NONE), 0x00);
}

}  // namespace
