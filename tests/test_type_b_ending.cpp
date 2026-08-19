// Type B ending — behavioral tests against docs/contracts/type-b-ending.md.
//
// Device-free: the three state handlers are pure logic over the game-state aggregate, and the one thing
// they need from outside — whether a song is still playing — is a supplied query a test can answer
// directly. Every asserted value is traced to the tetris.asm lines named in the contract.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <kirpich/game_state.h>
#include <kirpich/line_clear_kind.h>
#include <kirpich/sprite_id.h>

#include "data/music.h"
#include "data/playing_field.h"
#include "data/scene_sprites.h"
#include "data/tilemaps.h"
#include "retropp/input.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/type_b_ending.h"

namespace {

using kirpich::GameState;
using kirpich::MusicId;
using kirpich::SpriteId;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;

constexpr std::uint8_t kWipeStartStep = 2;
constexpr std::uint8_t kScoreboardHoldFrames = 128;
constexpr std::uint8_t kDanceStartFrames = 27;
constexpr std::uint8_t kDanceRedrawFrame = 20;
constexpr std::uint16_t kEndingLines = 25;
constexpr std::size_t kDancerSlots = 10;
constexpr std::size_t kActive = kirpich::kActivePieceSlot;
constexpr std::size_t kPreview = kirpich::kPreviewPieceSlot;

// The ten animation periods (tetris.asm:4776-4777), pinned entry by entry.
constexpr std::uint8_t kPeriods[kDancerSlots] = {
    0x1C, 0x0F, 0x1E, 0x32, 0x20, 0x18, 0x26, 0x1D, 0x28, 0x2B,
};

// A game sitting on a won Type B round, ready for the ending to run.
GameContext wonRound(std::uint8_t level, std::uint8_t startHeight = 0) {
    GameContext game;
    game.flow.typeBLevel = level;
    game.flow.typeBStartHeight = startHeight;
    game.flow.timer1 = 0;
    return game;
}

// The dance laid out and its timer cleared, so the next call animates.
GameContext dancing(std::uint8_t startHeight = 0) {
    GameContext game = wonRound(9, startHeight);
    kirpich::systems::initBonusEnding(game);
    game.flow.timer1 = 0;
    return game;
}

// Every field cell of the board matches `screen`.
template <typename Screen>
void expectScreenDrawn(const GameContext& game, const Screen& screen) {
    for (std::size_t row = 0; row < kirpich::kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kPlayingFieldCols; ++col) {
            EXPECT_EQ(game.field.fieldCell(row, col), screen[row][col])
                << "field cell (" << row << ", " << col << ")";
        }
    }
}

}  // namespace

// ── Test 1: VictoryJingleVectors ────────────────────────────────────────────────────────────────────
// GameState_05 (tetris.asm:4617-4660): the timer gate, the screen load, the level fork, and the
// terminal block that runs on both arms.
TEST(TypeBEnding, VictoryJingleVectors) {
    // The timer gate: a live timer leaves the whole game untouched (:4618-4620).
    {
        GameContext game = wonRound(5);
        game.flow.timer1 = 1;
        const GameContext before = game;
        kirpich::systems::typeBVictoryJingle(game);
        EXPECT_EQ(game, before);
    }

    // Level 0: the drawn screen already carries the level-0 values, so the print block and the score
    // clear are both skipped (:4624-4626). The score survives; every field cell is the stored screen's.
    {
        GameContext game = wonRound(0);
        game.engine.score = 1234;
        kirpich::systems::typeBVictoryJingle(game);

        expectScreenDrawn(game, kirpich::kScoreboardTilemap);
        EXPECT_EQ(game.engine.score, 1234u);
    }

    // Level 1: each row is overprinted with base x (level + 1), left-aligned from column 5
    // (:4627-4638), and the score is zeroed afterwards (:4639-4645).
    {
        GameContext game = wonRound(1);
        game.engine.score = 1234;
        kirpich::systems::typeBVictoryJingle(game);

        // single 40x2 = 80, double 100x2 = 200, triple 300x2 = 600, tetris 1200x2 = 2400.
        const std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> rows{
            { 1,  { 8, 0 } },
            { 4,  { 2, 0, 0 } },
            { 7,  { 6, 0, 0 } },
            { 10, { 2, 4, 0, 0 } },
        };
        for (const auto& [fieldRow, digits] : rows) {
            for (std::size_t i = 0; i < digits.size(); ++i) {
                EXPECT_EQ(game.field.fieldCell(fieldRow, 5 + i), digits[i])
                    << "row " << fieldRow << " digit " << i;
            }
        }
        EXPECT_EQ(game.engine.score, 0u);
    }

    // The terminal block, on both arms of the fork (:4647-4659). The wipe is armed by the screen load
    // itself (:6453-6456) — its position relative to the copy is not observable from the finished
    // state, so what is asserted is that both the whole screen and the armed step are there.
    for (const std::uint8_t level : { std::uint8_t{0}, std::uint8_t{9} }) {
        GameContext game = wonRound(level);
        game.spriteRenderer.slots[kActive].hidden = false;
        game.spriteRenderer.slots[kPreview].hidden = false;

        kirpich::systems::typeBVictoryJingle(game);

        EXPECT_EQ(game.flow.wipeCounter, kWipeStartStep);
        EXPECT_EQ(game.flow.timer1, kScoreboardHoldFrames);
        EXPECT_TRUE(game.spriteRenderer.slots[kActive].hidden);
        EXPECT_TRUE(game.spriteRenderer.slots[kPreview].hidden);
        EXPECT_TRUE(game.audioCues.resetRequested);
        // Twenty-five, decimal — not the byte $25 read as hexadecimal (:4656).
        EXPECT_EQ(game.flow.lines, kEndingLines);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_B_SCOREBOARD);
    }
}

// ── Test 2: InitBonusEndingVectors ──────────────────────────────────────────────────────────────────
// GameState_22 (tetris.asm:4718-4773): the dance layout — backdrop, performers, palettes, animation
// seeds, visibility, and the jingle.
TEST(TypeBEnding, InitBonusEndingVectors) {
    // The timer gate (:4719-4721).
    {
        GameContext game = wonRound(9);
        game.flow.timer1 = 1;
        const GameContext before = game;
        kirpich::systems::initBonusEnding(game);
        EXPECT_EQ(game, before);
    }

    {
        GameContext game = wonRound(9, 2);
        // Dirty the object buffer so the clear at :4725 is observable.
        game.engine.oam[7].tile = 0x99;

        kirpich::systems::initBonusEnding(game);

        expectScreenDrawn(game, kirpich::kDancersTilemap);
        EXPECT_EQ(game.flow.wipeCounter, kWipeStartStep);
        EXPECT_EQ(game.engine.oam[7].tile, 0u);

        // Ten performers in the first ten slots, from the stored scene list (:4726-4729).
        const auto scene = kirpich::dancerSprites();
        ASSERT_EQ(scene.size(), kDancerSlots);
        for (std::size_t i = 0; i < kDancerSlots; ++i) {
            const kirpich::SpriteSlot& slot = game.spriteRenderer.slots[i];
            EXPECT_EQ(slot.y, scene[i].y) << "slot " << i;
            EXPECT_EQ(slot.x, scene[i].x) << "slot " << i;
            EXPECT_EQ(slot.spriteId, scene[i].sprite) << "slot " << i;
            EXPECT_EQ(slot.behindBg, scene[i].behindBg) << "slot " << i;
            EXPECT_EQ(slot.xflip, scene[i].xflip) << "slot " << i;
        }

        // The second palette on slots 6 and 7, and on no other slot (:4730-4734).
        for (std::size_t i = 0; i < game.spriteRenderer.slots.size(); ++i) {
            const bool expected = (i == 6 || i == 7);
            EXPECT_EQ(game.spriteRenderer.slots[i].palette1, expected) << "slot " << i;
        }

        // Both halves of each animation pair take the same period (:4735-4748, table :4776-4777).
        for (std::size_t i = 0; i < kDancerSlots; ++i) {
            EXPECT_EQ(game.spriteRenderer.slots[i].animCounter, kPeriods[i]) << "slot " << i;
            EXPECT_EQ(game.spriteRenderer.slots[i].animReload, kPeriods[i]) << "slot " << i;
        }

        EXPECT_EQ(game.flow.lines, kEndingLines);
        EXPECT_EQ(game.flow.timer1, kDanceStartFrames);
        EXPECT_EQ(game.flow.gameState, GameState::DANCERS);
    }

    // One more dancer than the starting height — except height 5, which reveals all ten (:4749-4763).
    // The jingle rises with the height across the same sweep (:4764-4766).
    const std::uint8_t visibleFor[6] = { 1, 2, 3, 4, 5, 10 };
    const MusicId jingleFor[6] = {
        MusicId::TYPE_B_JINGLE_1, MusicId::TYPE_B_JINGLE_2, MusicId::TYPE_B_JINGLE_3,
        MusicId::TYPE_B_JINGLE_4, MusicId::TYPE_B_JINGLE_5, MusicId::TYPE_B_JINGLE_6,
    };
    for (std::uint8_t height = 0; height <= 5; ++height) {
        GameContext game = wonRound(9, height);
        kirpich::systems::initBonusEnding(game);

        for (std::size_t i = 0; i < kDancerSlots; ++i) {
            const bool visible = i < visibleFor[height];
            EXPECT_EQ(game.spriteRenderer.slots[i].hidden, !visible)
                << "height " << int{height} << " slot " << i;
        }
        EXPECT_EQ(game.audioCues.music, jingleFor[height]) << "height " << int{height};
    }
}

// ── Test 3: DancerAnimationVectors ──────────────────────────────────────────────────────────────────
// GameState_23's per-slot step (tetris.asm:4784-4815, :4831-4841): the two gates, the countdown, the
// frame toggle, and the cossack's jump.
TEST(TypeBEnding, DancerAnimationVectors) {
    // At exactly 20 the original redraws the performers and does nothing else (:4785-4787): the
    // object buffer fills, and no performer's animation advances.
    {
        GameContext game = dancing();
        game.flow.timer1 = kDanceRedrawFrame;
        const GameContext before = game;
        kirpich::systems::dancers(game);

        EXPECT_NE(game.engine.oam, before.engine.oam) << "the redraw frame should draw";

        GameContext expected = before;
        expected.engine.oam = game.engine.oam;
        expected.oamSources = game.oamSources;
        EXPECT_EQ(game, expected) << "the redraw frame moves nothing else";
    }

    // Any other live timer returns too (:4788-4789).
    {
        GameContext game = dancing();
        game.flow.timer1 = 3;
        const GameContext before = game;
        kirpich::systems::dancers(game);
        EXPECT_EQ(game, before);
    }

    // Slots step independently within one tick: slot 0 fires, slot 1 only counts down.
    {
        GameContext game = dancing();
        game.spriteRenderer.slots[0].animCounter = 1;
        game.spriteRenderer.slots[1].animCounter = 5;
        const SpriteId slot1Sprite = game.spriteRenderer.slots[1].spriteId;

        // A playing song keeps the dance alive, so these blocks observe the animation alone.
        kirpich::systems::dancers(game, [] { return true; });

        EXPECT_EQ(game.spriteRenderer.slots[0].animCounter, kPeriods[0]);
        EXPECT_EQ(game.spriteRenderer.slots[1].animCounter, 4);
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId, slot1Sprite);
    }

    // The cossack (slot 6) flips frames and jumps with them; his neighbour (slot 7) flips in place.
    {
        GameContext game = dancing();
        const std::uint8_t restingY = game.spriteRenderer.slots[6].y;
        ASSERT_EQ(game.spriteRenderer.slots[6].spriteId, SpriteId::JUMPING_COSSACK_1);
        ASSERT_EQ(game.spriteRenderer.slots[7].spriteId, SpriteId::DANCER_1);
        const std::uint8_t neighbourY = game.spriteRenderer.slots[7].y;

        game.spriteRenderer.slots[6].animCounter = 1;
        game.spriteRenderer.slots[7].animCounter = 1;
        kirpich::systems::dancers(game, [] { return true; });

        EXPECT_EQ(game.spriteRenderer.slots[6].spriteId, SpriteId::JUMPING_COSSACK_2);
        EXPECT_EQ(game.spriteRenderer.slots[6].y, 0x5D);  // up (:4840)
        EXPECT_EQ(game.spriteRenderer.slots[6].animCounter, kPeriods[6]);
        EXPECT_EQ(game.spriteRenderer.slots[7].spriteId, SpriteId::DANCER_2);
        EXPECT_EQ(game.spriteRenderer.slots[7].y, neighbourY);

        // And back down on the next firing (:4834).
        game.flow.timer1 = 0;
        game.spriteRenderer.slots[6].animCounter = 1;
        kirpich::systems::dancers(game, [] { return true; });
        EXPECT_EQ(game.spriteRenderer.slots[6].spriteId, SpriteId::JUMPING_COSSACK_1);
        EXPECT_EQ(game.spriteRenderer.slots[6].y, 0x67);
        EXPECT_EQ(game.spriteRenderer.slots[6].y, restingY);
    }

    // A counter already at zero wraps rather than firing, as the original's decrement does (:4795).
    {
        GameContext game = dancing();
        game.spriteRenderer.slots[3].animCounter = 0;
        const SpriteId sprite = game.spriteRenderer.slots[3].spriteId;

        kirpich::systems::dancers(game, [] { return true; });

        EXPECT_EQ(game.spriteRenderer.slots[3].animCounter, 255);
        EXPECT_EQ(game.spriteRenderer.slots[3].spriteId, sprite);
    }
}

// ── Test 4: DancersExitFork ─────────────────────────────────────────────────────────────────────────
// GameState_23's exit (tetris.asm:4818-4828): the dance holds while the jingle plays, and forks on the
// starting height when it stops.
TEST(TypeBEnding, DancersExitFork) {
    // A song still playing holds the state — the performers animate, nothing else moves (:4818-4820).
    {
        GameContext game = dancing();
        kirpich::systems::dancers(game, [] { return true; });

        // The layout left the state on the dance; a playing song leaves it there. The performers are
        // redrawn every animating frame (:4816-4817) — their four parts each fill all forty entries
        // — so a populated buffer is the witness that the clear which ends the dance did not run.
        EXPECT_EQ(game.flow.gameState, GameState::DANCERS);
        const bool anyDrawn =
            std::any_of(game.engine.oam.begin(), game.engine.oam.end(),
                        [](const kirpich::OamEntry& e) { return !(e == kirpich::OamEntry{}); });
        EXPECT_TRUE(anyDrawn) << "the performers should have been drawn, not cleared";
    }

    // Silence ends it: the object buffer is cleared and the round leaves (:4821-4828).
    for (std::uint8_t height = 0; height <= 5; ++height) {
        GameContext game = dancing(height);
        game.engine.oam[39].tile = 0x55;

        kirpich::systems::dancers(game, [] { return false; });

        EXPECT_EQ(game.engine.oam[39].tile, 0u) << "height " << int{height};
        const GameState expected =
            height == 5 ? GameState::INIT_BURAN : GameState::TYPE_B_VICTORY_JINGLE;
        EXPECT_EQ(game.flow.gameState, expected) << "height " << int{height};
    }

    // With no query supplied the dance ends rather than holding. This is the safe default and is
    // asserted directly so it is not "fixed" into an infinite dance (contract §5.1).
    {
        GameContext game = dancing();
        kirpich::systems::dancers(game);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_B_VICTORY_JINGLE);
    }
}

// ── Test 5: WinChainHasNoDeadEnd ────────────────────────────────────────────────────────────────────
// Both states a won Type B round can be sent to (line_clear.cpp:91-92) reach a real handler once these
// are installed — the hole this unit fills.
TEST(TypeBEnding, WinChainHasNoDeadEnd) {
    const GameState targets[] = { GameState::TYPE_B_VICTORY_JINGLE, GameState::INIT_TYPE_B_BONUS };
    const GameState reached[] = { GameState::INIT_TYPE_B_SCOREBOARD, GameState::DANCERS };

    for (std::size_t i = 0; i < 2; ++i) {
        // Without the install, the slot holds the dispatcher's not-ported stub and the state sticks.
        {
            GameStateDispatcher dispatcher;
            GameContext game = wonRound(9);
            game.flow.gameState = targets[i];
            dispatcher.tick(game, retropp::ActionSet{});
            EXPECT_EQ(game.flow.gameState, targets[i]);
        }

        GameStateDispatcher dispatcher;
        kirpich::systems::installTypeBEndingHandlers(dispatcher);
        GameContext game = wonRound(9);
        game.flow.gameState = targets[i];

        dispatcher.tick(game, retropp::ActionSet{});

        EXPECT_EQ(game.flow.gameState, reached[i]);
    }
}
