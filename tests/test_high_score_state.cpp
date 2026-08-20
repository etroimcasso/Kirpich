// High-score state: the top-score struct (src/state/high_score_state.h) and its persistence codec
// (src/state/high_score_persistence.{h,cpp}), checked against the existing WRAM and HRAM
// layout+census fixtures. Like the other state units this ships no fixture of its own - the
// WRAM map already carries the two table rows ($D000 wTypeBTopScores, $D654 wTypeATopScores) and the
// staging census rows ($C9A4/$C9AC), and the HRAM map carries the four owned bytes and their census.
//
// Ownership expectations come from docs/contracts/high-score-state.md. All sweeps are full-corpus
// over the fixtures - never a subset. The struct does not mirror the RAM byte offsets (scores are
// decimal, not BCD; the name-cursor pointer bytes $FFC9/$FFCA get no field), so the codec round-trips
// exercise the exact 1890-byte wire layout the contract pins.

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/save_store.h>

#include "state/high_score_state.h"
#include "state/high_score_persistence.h"
#include "fixtures/wram_expected.h"
#include "fixtures/hram_expected.h"

namespace {

using kirpich::CharTile;
using kirpich::HighScoreState;
using kirpich::TopScoreEntry;
using kirpich::fixtures::HramKind;
using kirpich::fixtures::HramLabel;
using kirpich::fixtures::kHramCensus;
using kirpich::fixtures::kHramLabels;
using kirpich::fixtures::WramKind;
using kirpich::fixtures::kWramCensus;
using kirpich::fixtures::kWramLabels;

// ---- Fixture lookups; a miss fails the test rather than reading past the array. -------------------

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

std::uint16_t wramSizeOf(std::string_view name) {
    for (const auto& r : kWramLabels)
        if (r.name == name && r.kind == WramKind::Field) return r.size;
    ADD_FAILURE() << "wram field not found in fixture: " << name;
    return 0;
}

std::uint16_t wramAddrOf(std::string_view name) {
    for (const auto& r : kWramLabels)
        if (r.name == name && r.kind == WramKind::Field) return r.address;
    ADD_FAILURE() << "wram field not found in fixture: " << name;
    return 0xFFFF;
}

bool wramCensusHas(std::uint16_t addr) {
    for (const auto& c : kWramCensus)
        if (c.address == addr) return true;
    return false;
}

// Every HRAM byte this unit owns, mapped to its port field. Four bytes, four fields; the two
// name-cursor pointer bytes ($FFC9/$FFCA) are deliberately NOT owned here (derived state - see D6).
struct Owned {
    std::uint16_t addr;
    const char*   field;
};
constexpr Owned kOwned[] = {
    {0xFFC6, "nameEntryColumn"},           // shared-byte overlay split with GameFlowState
    {0xFFC7, "newTopScore"},               // hNewTopScore (labelled)
    {0xFFC8, "newScoreRank"},              // unlabelled census byte
    {0xFFE8, "topScoresRedrawRequested"},  // hRedrawTopScoresDuringVBlank (labelled)
};

bool isOwned(std::uint16_t a) {
    for (const auto& o : kOwned) if (o.addr == a) return true;
    return false;
}

// (1) HRAM window pins - the two labelled rows are present at width 1, the $FFC6 and $FFC8 gap rows
// are genuine gaps of the expected size, the raw-operand census refCounts hold, and the bracketing
// neighbours belong to other units. An upstream repin that moves any of them fails here.
TEST(HighScoreState, HramWindowPins) {
    // Labelled rows, single byte each.
    EXPECT_EQ(hramSizeOf("hNewTopScore"), 1);
    EXPECT_EQ(hramAddrOf("hNewTopScore"), 0xFFC7);
    EXPECT_EQ(hramSizeOf("hRedrawTopScoresDuringVBlank"), 1);
    EXPECT_EQ(hramAddrOf("hRedrawTopScoresDuringVBlank"), 0xFFE8);

    // The two unlabelled owned bytes are genuine gap rows: $FFC6 alone (size 1), $FFC8 the head of the
    // three-byte name-entry scratch $FFC8-$FFCA (size 3).
    const HramLabel* col = hramRowAt(0xFFC6);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->kind, HramKind::Gap);
    EXPECT_EQ(col->size, 1);
    const HramLabel* scratch = hramRowAt(0xFFC8);
    ASSERT_NE(scratch, nullptr);
    EXPECT_EQ(scratch->kind, HramKind::Gap);
    EXPECT_EQ(scratch->size, 3);

    // Census refCounts: the blink byte $FF9C the insert zeroes (GameFlowState's blinkCounter, shared),
    // the shared column byte $FFC6, the rank byte $FFC8, and the two name-cursor pointer halves.
    EXPECT_EQ(hramCensusRefOf(0xFF9C), 12);
    EXPECT_EQ(hramCensusRefOf(0xFFC6), 17);
    EXPECT_EQ(hramCensusRefOf(0xFFC8), 2);
    EXPECT_EQ(hramCensusRefOf(0xFFC9), 6);
    EXPECT_EQ(hramCensusRefOf(0xFFCA), 7);

    // Bracketing neighbours belong to other units: hIsMultiplayer just below the column byte, and
    // hSerialRole just above the name-entry scratch.
    const HramLabel* mp = hramRowAt(0xFFC5);
    ASSERT_NE(mp, nullptr);
    EXPECT_EQ(mp->name, std::string_view("hIsMultiplayer"));
    const HramLabel* role = hramRowAt(0xFFCB);
    ASSERT_NE(role, nullptr);
    EXPECT_EQ(role->name, std::string_view("hSerialRole"));
}

// (2) WRAM table pins - the two table rows have the fixture's extents and sizes, they are adjacent,
// the sizes are the closed-form products, the struct dimensions multiply to the same entry counts,
// and the staging + table census rows are present.
TEST(HighScoreState, WramTablePins) {
    EXPECT_EQ(wramAddrOf("wTypeBTopScores"), 0xD000);
    EXPECT_EQ(wramSizeOf("wTypeBTopScores"), 1620);
    EXPECT_EQ(wramAddrOf("wTypeATopScores"), 0xD654);
    EXPECT_EQ(wramSizeOf("wTypeATopScores"), 270);

    // Type A begins exactly where Type B ends.
    EXPECT_EQ(wramAddrOf("wTypeATopScores"), wramAddrOf("wTypeBTopScores") + 1620);

    // Closed-form: 10 levels x 6 heights x 3 ranks x (6 name + 3 score) bytes; Type A drops the height.
    EXPECT_EQ(1620, 10 * 6 * 3 * (6 + 3));
    EXPECT_EQ(270, 10 * 3 * (6 + 3));

    // The struct dimensions multiply out to the same entry counts (180 Type B entries, 30 Type A).
    const HighScoreState s{};
    EXPECT_EQ(std::size(s.typeB), 10u);
    EXPECT_EQ(std::size(s.typeB[0]), 6u);
    EXPECT_EQ(std::size(s.typeB[0][0]), 3u);
    EXPECT_EQ(std::size(s.typeA), 10u);
    EXPECT_EQ(std::size(s.typeA[0]), 3u);
    EXPECT_EQ(std::size(s.typeB) * std::size(s.typeB[0]) * std::size(s.typeB[0][0]) * 9u, 1620u);
    EXPECT_EQ(std::size(s.typeA) * std::size(s.typeA[0]) * 9u, 270u);

    // Census rows: the two table heads and the two staging rows ($C9A4/$C9AC, the board shadow window).
    EXPECT_TRUE(wramCensusHas(0xD000));
    EXPECT_TRUE(wramCensusHas(0xD654));
    EXPECT_TRUE(wramCensusHas(0xC9A4));
    EXPECT_TRUE(wramCensusHas(0xC9AC));
}

// (3) Struct shape and field resolution - the glyph is one byte, the name array is six, the score
// holds the ceiling, both types have a defaulted ==; every owned byte resolves to exactly one field,
// with the two name-cursor pointer bytes explicitly NOT owned.
TEST(HighScoreState, StructShapeAndFieldResolution) {
    static_assert(sizeof(CharTile) == 1);
    static_assert(std::tuple_size_v<decltype(TopScoreEntry::name)> == 6);

    TopScoreEntry e{};
    e.score = 999999u;
    EXPECT_EQ(e.score, 999999u);
    const TopScoreEntry same{.score = 999999u, .name = {}};
    EXPECT_TRUE(e == same);

    HighScoreState a{};
    HighScoreState b{};
    EXPECT_TRUE(a == b);
    a.newScoreRank = 3;
    EXPECT_FALSE(a == b);   // defaulted == on the aggregate distinguishes them

    // Four owned bytes, no duplicate address, each resolving to one named field.
    ASSERT_EQ(std::size(kOwned), 4u);
    for (std::size_t i = 0; i < std::size(kOwned); ++i)
        for (std::size_t j = i + 1; j < std::size(kOwned); ++j)
            EXPECT_NE(kOwned[i].addr, kOwned[j].addr);

    // Negative guard: the name-cursor pointer halves are derived state and this unit owns neither.
    // $FFCA does have an owner elsewhere - the launch scenes' congratulations cursor carries it as
    // GameFlowState::congratulationsColumn, the same disjoint-in-time split as $FFC6 above - but the
    // top-score screen recomputes its own cursor each frame, so it claims no role in that byte.
    // $FFC9 is the pointer's constant high half and is carried by no surface at all.
    EXPECT_FALSE(isOwned(0xFFC9));
    EXPECT_FALSE(isOwned(0xFFCA));
}

// (4) Reset restores boot state - mutate every member including a deep Type B entry, reset, compare
// to a fresh instance; pin the boot values.
TEST(HighScoreState, ResetRestoresBootState) {
    HighScoreState s{};
    s.newTopScore = true;
    s.topScoresRedrawRequested = true;
    s.newScoreRank = 3;
    s.nameEntryColumn = 5;
    s.typeA[9][2].score = 999999u;
    s.typeA[9][2].name = {CharTile::LETTER_A, CharTile::SPACE, CharTile::HEART,
                          CharTile::ELLIPSIS, CharTile::ELLIPSIS, CharTile::ELLIPSIS};
    s.typeB[4][3][1].score = 12345u;
    s.typeB[4][3][1].name[0] = CharTile::LETTER_Z;

    EXPECT_FALSE(s == HighScoreState{});   // the mutations took
    s.reset();
    EXPECT_TRUE(s == HighScoreState{});    // back to boot state

    const HighScoreState boot{};
    EXPECT_FALSE(boot.newTopScore);
    EXPECT_FALSE(boot.topScoresRedrawRequested);
    EXPECT_EQ(boot.newScoreRank, 0);
    EXPECT_EQ(boot.nameEntryColumn, 0);
    EXPECT_EQ(boot.typeA[0][0].score, 0u);
    EXPECT_EQ(boot.typeB[0][0][0].score, 0u);
    EXPECT_EQ(boot.typeA[0][0].name[0], CharTile::DIGIT_0);   // $00 glyph, the name terminator
}

// (5) Wire-value pins - the name-entry wheel vocabulary maps to the fixed charmap byte values, the
// $00 name delimiter aliases the digit-0 glyph, and the rank domain is the inverted {1,2,3}.
TEST(HighScoreState, WireValuePins) {
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::LETTER_A), 0x0A);   // wheel seed "a"
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::ELLIPSIS), 0x60);   // "…" clear glyph
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::MULTIPLICATION_SIGN), 0x26);  // "×", wheel wrap
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::HEART), 0x27);      // "♥", heart-mode wrap partner
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::SPACE), 0x2F);      // " ", last selectable

    // The short-name delimiter is byte $00, which aliases the digit-0 glyph; unreachable from the
    // wheel, so no collision in practice.
    EXPECT_EQ(static_cast<std::uint8_t>(CharTile::DIGIT_0), 0x00);

    // Rank is the ROM's inverted counter: 3 = 1st place, 2 = 2nd, 1 = 3rd. All three fit the byte.
    for (std::uint8_t rank : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
        HighScoreState s{};
        s.newScoreRank = rank;
        EXPECT_EQ(s.newScoreRank, rank);
    }
}

// A populated state used by the codec round-trips: distinctive scores and names across both tables so
// a mis-ordered or mis-sized codec would corrupt a byte the round-trip catches.
HighScoreState populatedState() {
    HighScoreState s{};
    s.typeB[0][0][0] = {.score = 123456u, .name = {CharTile::LETTER_A, CharTile::LETTER_B,
                                                   CharTile::LETTER_C, CharTile::SPACE,
                                                   CharTile::HEART, CharTile::MULTIPLICATION_SIGN}};
    s.typeB[9][5][2] = {.score = 999999u, .name = {CharTile::LETTER_Z, CharTile::ELLIPSIS,
                                                   CharTile::ELLIPSIS, CharTile::ELLIPSIS,
                                                   CharTile::ELLIPSIS, CharTile::ELLIPSIS}};
    s.typeA[0][0] = {.score = 42u, .name = {CharTile::LETTER_A, CharTile::DIGIT_0,  // short name
                                            CharTile::DIGIT_0, CharTile::DIGIT_0,
                                            CharTile::DIGIT_0, CharTile::DIGIT_0}};
    s.typeA[9][2] = {.score = 1000u, .name = {CharTile::SPACE, CharTile::SPACE, CharTile::SPACE,
                                              CharTile::SPACE, CharTile::SPACE, CharTile::SPACE}};
    return s;
}

// (6) Persistence codec round-trip - encode then decode is identity on the tables; BCD edge scores
// survive; the image is exactly 1890 bytes with the Type B block first; decode refuses a wrong length.
TEST(HighScoreState, PersistenceCodecRoundTrip) {
    const HighScoreState original = populatedState();
    const auto image = kirpich::encodeTopScores(original);

    // The image is the fixed wire size.
    static_assert(std::tuple_size_v<decltype(image)> == kirpich::kTopScoresImageBytes);
    EXPECT_EQ(kirpich::kTopScoresImageBytes, 1890u);

    // Type B block first: image[0..2] is the BCD of typeB[0][0][0] (123456 -> 0x56 0x34 0x12), and the
    // Type A block starts at 1620 with typeA[0][0] (42 -> 0x42 0x00 0x00).
    EXPECT_EQ(image[0], 0x56);
    EXPECT_EQ(image[1], 0x34);
    EXPECT_EQ(image[2], 0x12);
    EXPECT_EQ(image[1620], 0x42);
    EXPECT_EQ(image[1621], 0x00);
    EXPECT_EQ(image[1622], 0x00);

    // Round-trip identity on the two tables (HRAM session fields do not persist and are untouched).
    HighScoreState restored{};
    ASSERT_TRUE(kirpich::decodeTopScores(std::span<const std::uint8_t>(image), restored));
    EXPECT_TRUE(restored.typeB == original.typeB);
    EXPECT_TRUE(restored.typeA == original.typeA);

    // The short-name $00 delimiter survives the round-trip verbatim.
    EXPECT_EQ(restored.typeA[0][0].name[1], CharTile::DIGIT_0);

    // BCD edges: 0, the ceiling, and a leading-zero score each survive on their own.
    for (std::uint32_t v : {std::uint32_t{0}, std::uint32_t{999999}, std::uint32_t{42},
                            std::uint32_t{1000}, std::uint32_t{600060}}) {
        HighScoreState in{};
        in.typeA[3][1].score = v;
        HighScoreState out{};
        ASSERT_TRUE(kirpich::decodeTopScores(
            std::span<const std::uint8_t>(kirpich::encodeTopScores(in)), out));
        EXPECT_EQ(out.typeA[3][1].score, v);
    }

    // Decode refuses a payload that is not exactly 1890 bytes, leaving the target untouched.
    HighScoreState untouched{};
    const std::vector<std::uint8_t> tooShort(1889, 0);
    EXPECT_FALSE(kirpich::decodeTopScores(std::span<const std::uint8_t>(tooShort), untouched));
    const std::vector<std::uint8_t> tooLong(1891, 0);
    EXPECT_FALSE(kirpich::decodeTopScores(std::span<const std::uint8_t>(tooLong), untouched));
    EXPECT_TRUE(untouched == HighScoreState{});
}

// (7) Persistence store round-trip - through a hermetic SaveStore::atPath: save then load returns the
// tables; an absent document leaves boot state; a corrupt document surfaces as SaveStoreError and the
// policy leaves boot state with the damaged file in place.
TEST(HighScoreState, PersistenceStoreRoundTrip) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_high_score_store_roundtrip";
    std::filesystem::remove_all(root);

    // Absent document: load leaves the state at boot and reports "no save".
    {
        auto store = retropp::SaveStore::atPath(root);
        HighScoreState s{};
        EXPECT_FALSE(kirpich::loadTopScores(store, s));
        EXPECT_TRUE(s == HighScoreState{});
    }

    // Save then load: the two tables come back equal.
    {
        auto store = retropp::SaveStore::atPath(root);
        const HighScoreState saved = populatedState();
        ASSERT_TRUE(kirpich::saveTopScores(saved, store));

        HighScoreState loaded{};
        ASSERT_TRUE(kirpich::loadTopScores(store, loaded));
        EXPECT_TRUE(loaded.typeB == saved.typeB);
        EXPECT_TRUE(loaded.typeA == saved.typeA);
    }

    // Corrupt document: truncate the on-disk file to a stub. loadTopScores catches SaveStoreError,
    // returns false, leaves the boot zeros, and does not remove the damaged file.
    {
        auto store = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01').put('\x02');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no save document on disk to corrupt";

        HighScoreState s{};
        EXPECT_FALSE(kirpich::loadTopScores(store, s));
        EXPECT_TRUE(s == HighScoreState{});

        // The damaged file survives - policy never proactively overwrites or removes it.
        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath()))
            if (entry.is_regular_file()) stillPresent = true;
        EXPECT_TRUE(stillPresent);
    }

    std::filesystem::remove_all(root);
}

}  // namespace
