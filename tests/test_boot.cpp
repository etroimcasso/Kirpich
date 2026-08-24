// The boot path — behavioral tests against docs/contracts/boot.md.
//
// Device-free except case 5, which needs a save store and gets a hermetic temp directory. Every
// asserted value is traced to the tetris.asm lines named in the contract.
//
// Two of these cases are about composition rather than about any single value: case 4 pins the cold
// boot and the soft reset as the same sequence differing only in the two tables, and case 7 pins that
// a matched chord ends the gameplay frame. Both are the kind of property that would otherwise drift
// silently, because nothing about them shows up in a single field.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/serial_role.h>
#include <kirpich/sprite_id.h>

#include <retropp/save_store.h>

#include "fixtures/hram_expected.h"
#include "retropp/input.h"
#include "state/demo_state.h"
#include "state/display_state.h"
#include "state/high_score_persistence.h"
#include "state/high_score_state.h"
#include "systems/boot.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::Action;
using kirpich::CharTile;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::HighScoreState;
using kirpich::MusicType;
using kirpich::SerialRole;
using kirpich::SpriteId;
using kirpich::TileSheet;
using kirpich::TopScoreEntry;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// The four buttons the reset chord is (tetris.asm:392, and again at :4442). A and B reach the port's
// action vocabulary as the two rotations.
retropp::ActionSet chord() {
    return actionSet({Action::Start, Action::Select, Action::RotateClockwise,
                      Action::RotateCounterClockwise});
}

// Distinguishable content for all three top-score tables, so "the tables came back" cannot pass by
// accident on a table of zeros.
void fillTopScoreTables(HighScoreState& scores) {
    std::uint32_t n = 1;
    for (auto& level : scores.typeB) {
        for (auto& height : level) {
            for (auto& entry : height) {
                entry = TopScoreEntry{.score = n * 7,
                                      .name  = {CharTile::LETTER_A, CharTile::LETTER_A,
                                                CharTile::LETTER_A, CharTile::LETTER_A,
                                                CharTile::LETTER_A, CharTile::LETTER_A}};
                ++n;
            }
        }
    }
    for (auto& level : scores.typeA) {
        for (auto& entry : level) {
            entry = TopScoreEntry{.score = n * 13,
                                  .name  = {CharTile::LETTER_B, CharTile::LETTER_B,
                                            CharTile::LETTER_B, CharTile::LETTER_B,
                                            CharTile::LETTER_B, CharTile::LETTER_B}};
            ++n;
        }
    }
    for (auto& level : scores.typeC) {
        for (auto& entry : level) {
            entry = TopScoreEntry{.score = n * 17,
                                  .name  = {CharTile::LETTER_C, CharTile::LETTER_C,
                                            CharTile::LETTER_C, CharTile::LETTER_C,
                                            CharTile::LETTER_C, CharTile::LETTER_C}};
            ++n;
        }
    }
}

// A context with something non-boot in every member, so a clear the boot fails to perform shows up.
GameContext dirtied() {
    GameContext game;

    game.engine.score            = 123456;
    game.engine.hidePreviewPiece = true;
    game.engine.oam[3].tile      = 0x5A;

    game.flow.gameState  = GameState::NORMAL_GAMEPLAY;
    game.flow.timer1     = 40;
    game.flow.timer2     = 9;
    game.flow.lines      = 4321;
    game.flow.level      = 7;
    game.flow.gameType   = GameType::TYPE_B;
    game.flow.musicType  = MusicType::OFF;
    game.flow.paused     = true;
    game.flow.wipeCounter = 5;

    game.field.fieldCell(4, 4)      = 0x81;
    game.field.attackRow[2]         = 0x28;
    game.spriteRenderer.slots[1].hidden   = true;
    game.spriteRenderer.slots[1].spriteId = SpriteId::J_0;

    game.multiplayer.isMultiplayer = true;
    game.multiplayer.role          = SerialRole::MASTER;
    game.multiplayer.rx            = 0x33;

    game.demo.activeDemo     = ActiveDemo::TYPE_A;
    game.demo.framesRemaining = 12;

    fillTopScoreTables(game.highScores);
    game.highScores.newTopScore              = true;
    game.highScores.topScoresRedrawRequested = true;
    game.highScores.newScoreRank             = 2;
    game.highScores.nameEntryColumn          = 4;

    game.display.map[0][0]     = 0x99;
    game.display.secondMap[0][0] = 0x99;
    game.display.displayed     = DisplayedMap::SECOND;
    game.display.sheet         = TileSheet::MULTIPLAYER_BURAN;

    game.joypad.held    = chord();
    game.joypad.pressed = chord();

    game.audioCues.resetRequested         = false;
    game.oamSources.entries[0].drawn      = true;
    game.oamSources.entries[0].slot       = 6;

    return game;
}

// Every cell of the first map, and every cell of the second.
void expectFirstMapFilledSecondMapZero(const GameContext& game) {
    for (std::size_t r = 0; r < kirpich::kBackgroundMapRows; ++r) {
        for (std::size_t c = 0; c < kirpich::kBackgroundMapCols; ++c) {
            ASSERT_EQ(game.display.map[r][c], kSpace) << "first map " << r << "," << c;
            ASSERT_EQ(game.display.secondMap[r][c], 0) << "second map " << r << "," << c;
        }
    }
}

const kirpich::fixtures::HramLabel& hramLabel(std::string_view name) {
    for (const auto& region : kirpich::fixtures::kHramLabels) {
        if (region.name == name) return region;
    }
    ADD_FAILURE() << "no HRAM label named " << name;
    return kirpich::fixtures::kHramLabels[0];
}

}  // namespace

// ── Test 1: ColdBootComposition ─────────────────────────────────────────────────────────────────────
// Init entered at its top (tetris.asm:264): the six clear loops, the tile-map fill (:366), the sound
// driver's startup (:301-306/:311-317/:367), and the three values the following screens read
// (:371-376).
TEST(Boot, ColdBootComposition) {
    GameContext game = dirtied();
    kirpich::systems::coldBoot(game);

    // Everything the clears reach is back at boot. Compared member by member rather than against a
    // whole default context, because three members are deliberately NOT at their default afterwards.
    const GameContext boot;
    EXPECT_TRUE(game.engine == boot.engine);
    EXPECT_TRUE(game.field == boot.field);
    EXPECT_TRUE(game.spriteRenderer == boot.spriteRenderer);
    EXPECT_TRUE(game.multiplayer == boot.multiplayer);
    EXPECT_TRUE(game.demo == boot.demo);
    EXPECT_TRUE(game.highScores == boot.highScores);
    EXPECT_TRUE(game.joypad == boot.joypad);
    EXPECT_TRUE(game.oamSources == boot.oamSources);

    // The tile-map fill covers the first map only, with the character map's space glyph rather than
    // zero; the second map is left as the video-memory clear left it (contract section 5).
    expectFirstMapFilledSecondMapZero(game);
    EXPECT_EQ(game.display.displayed, DisplayedMap::FIRST);
    EXPECT_EQ(game.display.sheet, TileSheet::COPYRIGHT_TITLE);

    // The sound driver's whole startup is asked for, not performed here (contract section 6). Not the
    // plain initialisation the game asks for elsewhere: that one leaves the driver's pause state
    // latched and silences it for the rest of the session.
    EXPECT_TRUE(game.audioCues.driverRestartRequested);
    EXPECT_FALSE(game.audioCues.resetRequested);

    // What the boot leaves behind (:371-376).
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
    EXPECT_EQ(game.flow.musicType, MusicType::MUSIC_A);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_COPYRIGHT);
    // And nothing else in the game-flow block survived.
    GameContext expectedFlow;
    expectedFlow.flow.gameType  = GameType::TYPE_A;
    expectedFlow.flow.musicType = MusicType::MUSIC_A;
    expectedFlow.flow.gameState = GameState::INIT_COPYRIGHT;
    EXPECT_TRUE(game.flow == expectedFlow.flow);
}

// ── Test 2: ColdBootClearsTopScores ─────────────────────────────────────────────────────────────────
// The clear at :265-274 covers $D000-$DFFF, which is where both top-score tables live. This is the one
// thing the two entry points disagree about, so it is asserted on its own.
TEST(Boot, ColdBootClearsTopScores) {
    GameContext game;
    fillTopScoreTables(game.highScores);
    ASSERT_FALSE(game.highScores.typeA == HighScoreState{}.typeA);

    kirpich::systems::coldBoot(game);

    EXPECT_TRUE(game.highScores.typeA == HighScoreState{}.typeA);
    EXPECT_TRUE(game.highScores.typeB == HighScoreState{}.typeB);
    EXPECT_TRUE(game.highScores.typeC == HighScoreState{}.typeC);
}

// ── Test 3: SoftResetPreservesTablesAndResetsItsHramBytes ───────────────────────────────────────────
// HighScoreState sits on both sides of the line .softReset draws (contract section 3). Its two tables
// are in the clear a soft reset skips (:265-274); its four high-memory bytes are in the clear a soft
// reset runs (:347-352). Both halves are asserted here, in one case, so that neither a
// preserve-everything nor a reset-everything implementation can pass.
TEST(Boot, SoftResetPreservesTablesAndResetsItsHramBytes) {
    GameContext game = dirtied();
    const HighScoreState before = game.highScores;

    kirpich::systems::softReset(game);

    // The tables survive, byte for byte.
    EXPECT_TRUE(game.highScores.typeA == before.typeA);
    EXPECT_TRUE(game.highScores.typeB == before.typeB);
    EXPECT_TRUE(game.highScores.typeC == before.typeC)
        << "Type C's table keeps the same company as the other two";

    // The four bytes do not.
    EXPECT_FALSE(game.highScores.newTopScore);
    EXPECT_FALSE(game.highScores.topScoresRedrawRequested);
    EXPECT_EQ(game.highScores.newScoreRank, 0);
    EXPECT_EQ(game.highScores.nameEntryColumn, 0);
}

// ── Test 4: SoftResetMatchesColdBootElsewhere ───────────────────────────────────────────────────────
// The two entry points are one routine (tetris.asm:264 falls into :276), so everything below the first
// clear has to be identical. Run both from the same starting state and the results may differ in the
// two tables and in nothing else.
TEST(Boot, SoftResetMatchesColdBootElsewhere) {
    GameContext viaCold = dirtied();
    GameContext viaSoft = dirtied();
    ASSERT_TRUE(viaCold == viaSoft);

    kirpich::systems::coldBoot(viaCold);
    kirpich::systems::softReset(viaSoft);

    // The one licensed difference.
    EXPECT_FALSE(viaSoft.highScores.typeA == viaCold.highScores.typeA);

    // Substitute it out and the two are the same machine.
    viaSoft.highScores.typeA = viaCold.highScores.typeA;
    viaSoft.highScores.typeB = viaCold.highScores.typeB;
    viaSoft.highScores.typeC = viaCold.highScores.typeC;
    EXPECT_TRUE(viaSoft == viaCold);
}

// ── Test 5: BootOrderKeepsSavedScores ──────────────────────────────────────────────────────────────
// bootGame clears and then loads, in that order. Reversed, a launch would wipe the tables it had just
// read — every launch, and nothing else in the suite would see it. Through a hermetic store.
TEST(Boot, BootOrderKeepsSavedScores) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_boot_order";
    std::filesystem::remove_all(root);

    HighScoreState saved{};
    fillTopScoreTables(saved);
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveTopScores(saved, store));
    }

    // A launch: dirty machine, saved document on disk.
    GameContext game = dirtied();
    auto        store = retropp::SaveStore::atPath(root);
    kirpich::systems::bootGame(game, store);

    // The saved tables are what the machine ends up holding...
    EXPECT_TRUE(game.highScores.typeA == saved.typeA);
    EXPECT_TRUE(game.highScores.typeB == saved.typeB);
    EXPECT_TRUE(game.highScores.typeC == saved.typeC);

    // ...and the boot still happened around them.
    EXPECT_EQ(game.flow.gameState, GameState::INIT_COPYRIGHT);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
    EXPECT_TRUE(game.audioCues.driverRestartRequested);
    expectFirstMapFilledSecondMapZero(game);
    EXPECT_TRUE(game.field == GameContext{}.field);

    // A first run with no document keeps the zeros the clear produced, which is what the original's
    // cold boot leaves.
    const std::filesystem::path empty =
        std::filesystem::temp_directory_path() / "kirpich_boot_order_empty";
    std::filesystem::remove_all(empty);
    GameContext fresh = dirtied();
    auto        emptyStore = retropp::SaveStore::atPath(empty);
    kirpich::systems::bootGame(fresh, emptyStore);
    EXPECT_TRUE(fresh.highScores == HighScoreState{});

    std::filesystem::remove_all(root);
    std::filesystem::remove_all(empty);
}

// ── Test 6: ChordFiresResetThroughDispatcher ───────────────────────────────────────────────────────
// End to end through the frame dispatcher with the real reset installed (tetris.asm:391-394). The
// timer-skip that goes with the chord is the dispatcher's own law and is pinned there with a probe
// seam; it cannot be re-asserted here, because a real reset zeroes both timers whether they were
// decremented or not.
TEST(Boot, ChordFiresResetThroughDispatcher) {
    GameContext          game = dirtied();
    GameStateDispatcher  dispatcher;
    const HighScoreState before = game.highScores;

    int resets = 0;
    dispatcher.softReset = [&] {
        ++resets;
        kirpich::systems::softReset(game);
        dispatcher.reset();
    };

    dispatcher.tick(game, chord());

    EXPECT_EQ(resets, 1);
    EXPECT_TRUE(game.highScores.typeA == before.typeA);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_COPYRIGHT);
    EXPECT_TRUE(game.field == GameContext{}.field);

    // Held down, it fires again on the next frame — the original re-runs the jump every frame the
    // chord matches, and clearing the held byte is what keeps it matching.
    dispatcher.tick(game, chord());
    EXPECT_EQ(resets, 2);

    // Released, it stops.
    dispatcher.tick(game, retropp::ActionSet{});
    EXPECT_EQ(resets, 2);
}

// ── Test 7: ChordEndsTheGameplayFrame ──────────────────────────────────────────────────────────────
// The duplicate check inside HandleStartSelect reaches the reset with a jump (tetris.asm:4444), so the
// rest of the gameplay frame does not run. Asserted as a whole-machine comparison rather than as a
// return value: if any of the ten beats below the chord had run, the two machines would differ.
TEST(Boot, ChordEndsTheGameplayFrame) {
    GameContext playing = dirtied();
    playing.flow.gameState = GameState::NORMAL_GAMEPLAY;
    playing.flow.paused    = false;
    playing.demo.activeDemo = ActiveDemo::NONE;
    playing.joypad.held    = chord();
    playing.joypad.pressed = retropp::ActionSet{};

    // The same machine, reset and nothing more.
    GameContext resetOnly = playing;
    kirpich::systems::softReset(resetOnly);

    kirpich::systems::normalGameplay(playing, {},
                                     [&playing] { kirpich::systems::softReset(playing); });

    EXPECT_TRUE(playing == resetOnly);

    // And the routine itself reports the frame as ended.
    GameContext again = playing;
    again.joypad.held = chord();
    EXPECT_FALSE(kirpich::systems::handleStartSelect(again, [] {}));
}

// ── Test 8: OverCopyLandsOnValuesTheBootOverwrites ─────────────────────────────────────────────────
// The routine copy at :356-364 writes two bytes more than the routine is long, and they land on the
// two selection bytes the boot writes a few instructions later (contract section 8). The arithmetic is
// re-derived from the shipped layout so a change to it breaks this test rather than the proof, and the
// outcome is asserted as an equivalence: those two bytes end at their boot values whatever preceded
// them.
TEST(Boot, OverCopyLandsOnValuesTheBootOverwrites) {
    const auto& dma       = hramLabel("hDMARoutine");
    const auto& gameType  = hramLabel("hGameType");
    const auto& musicType = hramLabel("hMusicType");

    // The routine is 10 bytes and the copy loop writes `.end - DMARoutine + 2` of them.
    constexpr std::uint16_t kRoutineBytes = 10;
    constexpr std::uint16_t kCopiedBytes  = kRoutineBytes + 2;
    EXPECT_EQ(dma.size, kRoutineBytes);

    // So the two extra bytes are exactly these two addresses.
    EXPECT_EQ(dma.address + kRoutineBytes, gameType.address);
    EXPECT_EQ(dma.address + kRoutineBytes + 1, musicType.address);
    EXPECT_EQ(dma.address + kCopiedBytes - 1, musicType.address);
    EXPECT_EQ(gameType.size, 1);
    EXPECT_EQ(musicType.size, 1);

    // And whatever they held, the boot's own writes are what remains (:371-374).
    for (const GameType before : {GameType::TYPE_A, GameType::TYPE_B}) {
        for (const MusicType music :
             {MusicType::MUSIC_A, MusicType::MUSIC_B, MusicType::MUSIC_C, MusicType::OFF}) {
            GameContext game;
            game.flow.gameType  = before;
            game.flow.musicType = music;
            kirpich::systems::coldBoot(game);
            EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
            EXPECT_EQ(game.flow.musicType, MusicType::MUSIC_A);
        }
    }
}
