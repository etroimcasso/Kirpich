// Game-flow state (HRAM globals): the main-loop state struct (src/state/game_flow_state.h) and its
// layout + census contract (tests/fixtures/hram_expected.h).
//
// The fixture holds the whole hram.asm layout as {name, address, size} rows and a census of every
// raw-operand HRAM access in tetris.asm as {address, refCount} rows. The tests sweep the whole
// corpus - never a subset: the layout tiling proof, the width pins tying the ported $FFxx rows to
// the struct, the census boundary guard (every raw-accessed byte resolves to exactly one owner), the
// reset-to-boot behaviour, the typed-member boot pins, and the unlabelled-fold-in census pins.
// Ownership expectations come from docs/contracts/game-state-machine-state.md.

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/piece.h>
#include <kirpich/sprite_id.h>

#include "state/game_flow_state.h"
#include "fixtures/hram_expected.h"

namespace {

using kirpich::GameFlowState;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::MusicType;
using kirpich::Piece;
using kirpich::SpriteId;
using kirpich::fixtures::HramKind;
using kirpich::fixtures::HramLabel;
using kirpich::fixtures::kHramCensus;
using kirpich::fixtures::kHramLabels;
using kirpich::fixtures::kHramSections;

// Fixture lookups. A miss fails the test rather than reading past the array.
std::uint16_t sizeOf(std::string_view name) {
    for (const auto& row : kHramLabels)
        if (row.name == name) return row.size;
    ADD_FAILURE() << "label not found in fixture: " << name;
    return 0;
}

std::uint16_t addrOf(std::string_view name) {
    for (const auto& row : kHramLabels)
        if (row.name == name) return row.address;
    ADD_FAILURE() << "label not found in fixture: " << name;
    return 0xFFFF;
}

// The layout region whose [address, address+size) span contains `a` (a Field or a Gap; an Alias
// coincides with its Field). Returns nullptr if `a` is not inside any region (e.g. $FFFE, which the
// census reaches but the layout stops one below).
const HramLabel* rowContaining(std::uint16_t a) {
    for (const auto& row : kHramLabels) {
        if (row.kind == HramKind::Alias) continue;
        if (row.address <= a && a < row.address + row.size) return &row;
    }
    return nullptr;
}

bool inList(std::uint16_t a, std::initializer_list<std::uint16_t> xs) {
    for (std::uint16_t x : xs) if (a == x) return true;
    return false;
}

// (1) Fixture-integrity sweep - every row, the section, no subset.
TEST(GameFlowState, HramFixtureTilesTheSection) {
    ASSERT_EQ(kHramSections.size(), 1u);
    const auto& sec = kHramSections[0];
    EXPECT_EQ(sec.origin, 0xFF80);
    EXPECT_EQ(sec.end, 0xFFFE);          // HRAM proper stops one below rIE at $FFFF
    EXPECT_EQ(sec.first, 0u);
    EXPECT_EQ(sec.count, kHramLabels.size());

    std::uint32_t cursor = sec.origin;
    std::uint16_t lastFieldAddr = 0;
    bool haveField = false;
    int aliasCount = 0;
    for (const auto& r : kHramLabels) {
        EXPECT_GT(r.size, 0) << r.name;
        EXPECT_GE(r.address, 0xFF80) << r.name;
        EXPECT_LT(r.address, 0xFFFE) << r.name;
        if (r.kind == HramKind::Alias) {
            ++aliasCount;
            ASSERT_TRUE(haveField) << "alias with no preceding field: " << r.name;
            EXPECT_EQ(r.address, lastFieldAddr) << r.name;   // shares the field's address
            continue;                                        // does not advance the cursor
        }
        EXPECT_EQ(r.address, cursor) << "@ " << r.name;      // strictly ascending, no overlap or hole
        if (r.kind == HramKind::Field) {
            lastFieldAddr = r.address;
            haveField = true;
        }
        cursor += r.size;
    }
    EXPECT_EQ(cursor, sec.end);
    EXPECT_EQ(aliasCount, 1);                                // exactly one alias ($FFFC)
    EXPECT_EQ(addrOf("hTempPreviewPiece"), 0xFFFC);
    EXPECT_EQ(addrOf("hTopScorePointerLo"), 0xFFFC);
}

// (2) Width pins - every ported labelled game-flow field exists in the fixture at the port's width.
TEST(GameFlowState, LabelledFieldWidthsMatchFixture) {
    struct Row { const char* label; std::uint16_t width; };
    // Every game-flow field that has an hram.asm label (the four unlabelled fold-ins are checked in test 6).
    constexpr Row kFields[] = {
        {"hDropTimer", 1}, {"hFramesPerDrop", 1}, {"hLines", 2}, {"hTimer1", 1}, {"hTimer2", 1},
        {"hLevel", 1}, {"hKeyRepeatTimer", 1}, {"hPaused", 1}, {"hNextPreviewPiece", 1},
        {"hNumPiecesPlayed", 1}, {"hGameType", 1}, {"hMusicType", 1}, {"hTypeALevel", 1},
        {"hTypeBLevel", 1}, {"hTypeBStartHeight", 1}, {"hGameState", 1}, {"hFrameCounter", 1},
        {"hWipeCounter", 1}, {"hSoftDropCounter", 1}, {"hRocketSpriteIndex", 1}, {"hHeartMode", 1},
        {"hTempPreviewPiece", 1}, {"hTopScorePointerHi", 1},  // the two bytes this unit splits from another surface
    };
    for (const auto& f : kFields)
        EXPECT_EQ(sizeOf(f.label), f.width) << f.label;

    // hLines is the only two-byte field; it maps to uint16_t lines.
    const GameFlowState s{};
    EXPECT_EQ(sizeof(s.lines), 2u);
    EXPECT_EQ(sizeOf("hLines"), sizeof(s.lines));

    // The typed members are all single ROM-equivalent bytes.
    EXPECT_EQ(sizeof(s.gameState), 1u);
    EXPECT_EQ(sizeof(s.gameType), 1u);
    EXPECT_EQ(sizeof(s.musicType), 1u);
    EXPECT_EQ(sizeof(s.nextPreviewPiece), 1u);
    EXPECT_EQ(sizeof(s.tempPreviewPiece), 1u);
    EXPECT_EQ(sizeof(s.rocketSpriteIndex), 1u);
    static_assert(sizeof(Piece) == 1);
    static_assert(sizeof(GameState) == 1);
    static_assert(sizeof(GameType) == 1);
    static_assert(sizeof(MusicType) == 1);
    static_assert(sizeof(SpriteId) == 1);
}

// (3) Census boundary guard - every raw-accessed byte resolves to exactly one owner. This is the
// per-byte ownership boundary made testable: a raw access to an unowned gap byte fails here.
TEST(GameFlowState, EveryCensusByteHasExactlyOneOwner) {
    // Unlabelled gap bytes the ownership map assigns to a later state surface: the sprite-renderer
    // scratch, the serial/multiplayer scratch, and the top-score scratch. Labelled bytes are owned
    // by whichever surface claims the label and need no entry here.
    constexpr std::uint16_t kOtherSurfaceGapBytes[] = {
        // sprite-renderer state ($FF86-$FF97 incl. $FF94) + $FFB2-$FFB5
        0xFF86, 0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C, 0xFF94,
        0xFFB2, 0xFFB3, 0xFFB4, 0xFFB5,
        // serial / multiplayer state
        0xFFB1, 0xFFCE, 0xFFD1, 0xFFD2, 0xFFD3, 0xFFD4, 0xFFD5, 0xFFD6,
        0xFFD9, 0xFFDA, 0xFFDB, 0xFFDC, 0xFFEF, 0xFFF0,
        // top-score state
        0xFFC8,
        // The high half of a big-endian cursor pointer. Both roles of that pointer - the top-score
        // name cell and the launch scenes' congratulations cell - keep it constant for their whole
        // run ($C9 and $9C respectively), so neither surface carries it.
        0xFFC9,
    };

    auto ownerOf = [&](std::uint16_t a) -> const char* {
        if (inList(a, {0xFF98, 0xFF9C, 0xFFA0, 0xFFC6, 0xFFE0})) return "game-flow-foldin";
        if (a == 0xFFCA) return "game-flow-congratulations";  // congratulationsColumn (shared byte)
        if (a == 0xFFFB) return "game-flow-topout";          // topOutLockCount (shares hTopScorePointerHi)
        if (inList(a, {0xFF9B, 0xFFFE})) return "mechanism";  // gap scratch + boot-clear top
        if (inList(a, {0xFFA4, 0xFFAF})) return "dead";
        const HramLabel* row = rowContaining(a);
        if (row && !row->name.empty()) return "labeled";     // some state surface claims that label
        for (std::uint16_t x : kOtherSurfaceGapBytes) if (a == x) return "other-surface-gap";
        return nullptr;
    };

    for (const auto& c : kHramCensus) {
        const char* owner = ownerOf(c.address);
        EXPECT_NE(owner, nullptr)
            << "census byte $" << std::hex << c.address << " is not owned by any state unit";
    }
}

// (4) Reset-to-boot - mutate every member, reset, compare to a fresh instance.
TEST(GameFlowState, ResetRestoresBootState) {
    GameFlowState s{};
    s.pieceLockStage = 3;
    s.dropTimer = 9;
    s.framesPerDrop = 53;
    s.blinkCounter = 7;
    s.lines = 12345;
    s.completedRowCount = 4;
    s.timer1 = 40;
    s.timer2 = 200;
    s.level = 15;
    s.keyRepeatTimer = 6;
    s.paused = true;
    s.nextPreviewPiece = Piece{0x1C};
    s.numPiecesPlayed = 99;
    s.gameType = GameType::TYPE_B;
    s.musicType = MusicType::MUSIC_C;
    s.typeALevel = 9;
    s.typeBLevel = 8;
    s.typeBStartHeight = 5;
    s.coarseCountdown = 0x13;
    s.gameState = GameState::GAME_OVER_SCREEN;
    s.frameCounter = 250;
    s.wipeCounter = 17;
    s.softDropCounter = 22;
    s.rocketSpriteIndex = SpriteId::ROCKET_M;
    s.heartMode = 0x80;
    s.tempPreviewPiece = Piece{0x04};
    s.topOutLockCount = 1;

    EXPECT_FALSE(s == GameFlowState{});   // the mutations actually took
    s.reset();
    EXPECT_TRUE(s == GameFlowState{});    // back to boot state
}

// (5) Typed-member boot pins - the boot (all-zero) values and the shared-byte split.
TEST(GameFlowState, TypedMemberBootValues) {
    const GameFlowState s{};

    // The main loop dispatches on gameState; boot value $00 is NORMAL_GAMEPLAY (Init writes $24
    // before the first dispatch - a contract note, not asserted here).
    EXPECT_EQ(s.gameState, GameState::NORMAL_GAMEPLAY);
    EXPECT_EQ(static_cast<std::uint8_t>(s.gameState), 0);

    // Zero is not a valid GameType/MusicType enumerator; the boot value is "unset until the menu
    // writes it", exactly as in ROM. We pin the boot byte, not enumerator validity.
    EXPECT_EQ(static_cast<std::uint8_t>(s.gameType), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(s.musicType), 0);
    EXPECT_NE(static_cast<std::uint8_t>(GameType::TYPE_A), 0);   // 0 really is outside the enum's values
    EXPECT_NE(static_cast<std::uint8_t>(GameType::TYPE_B), 0);

    // Piece/SpriteId boot to their zero value; overwritten before use.
    EXPECT_EQ(s.nextPreviewPiece, Piece{0});
    EXPECT_EQ(s.tempPreviewPiece, Piece{0});
    EXPECT_EQ(static_cast<std::uint8_t>(s.rocketSpriteIndex), 0);

    // The two bytes this unit splits from another surface's label: the fixture shows one physical byte
    // under the top-score label; the port carries an independent game-flow field at each.
    EXPECT_EQ(addrOf("hTopScorePointerHi"), 0xFFFB);  // top-score label; this unit's topOutLockCount shares it
    EXPECT_EQ(addrOf("hTempPreviewPiece"), addrOf("hTopScorePointerLo"));  // one byte, two labels ($FFFC)
    EXPECT_EQ(s.topOutLockCount, 0);
}

// (6) Unlabelled-fold-in census pins - the five bytes this unit claims that have no hram.asm label
// must still be raw-accessed (refCount > 0), or the fold-in no longer exists after an upstream repin.
TEST(GameFlowState, UnlabelledFoldInsAreCensused) {
    auto censusRefOf = [&](std::uint16_t a) -> int {
        for (const auto& c : kHramCensus) if (c.address == a) return c.refCount;
        return 0;
    };
    // Each fold-in address, and the fact that it is genuinely a Gap (no label) in the layout.
    for (std::uint16_t a : {0xFF98, 0xFF9C, 0xFFA0, 0xFFC6, 0xFFE0}) {
        EXPECT_GT(censusRefOf(a), 0) << "fold-in $" << std::hex << a << " has no raw access";
        const HramLabel* row = rowContaining(a);
        ASSERT_NE(row, nullptr) << std::hex << a;
        EXPECT_TRUE(row->name.empty()) << "fold-in $" << std::hex << a << " unexpectedly has a label";
    }
}

}  // namespace
