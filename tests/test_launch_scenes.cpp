// Launch scenes — behavioral tests against docs/contracts/launch-scenes.md.
//
// Device-free: all fifteen handlers are pure logic over the game-state aggregate. Neither chain reads
// input, so nothing here needs a joypad beyond the empty set the dispatcher tick takes. Every asserted
// value is traced to the tetris.asm lines named in the contract.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/sprite_id.h>

#include "data/music.h"
#include "data/scene_sprites.h"
#include "data/sfx.h"
#include "data/tilemaps.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/launch_scenes.h"

namespace {

using kirpich::BackgroundMap;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::MusicId;
using kirpich::NoiseSfxId;
using kirpich::SpriteId;
using kirpich::SquareSfxId;
using kirpich::TileSheet;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(kirpich::CharTile::SPACE);

// The pad geometry, re-derived from the ROM destination addresses rather than from the implementation:
// off = addr - $9C00, row = off / 32, col = off % 32. Contract §2.2.
constexpr std::size_t kBackdropRow = 14;        // $9DC0 -> 448 = 14*32 + 0
constexpr std::size_t kTowerTopRow = 7;         // $9CEC -> 236 = 7*32 + 12
constexpr std::size_t kRightTowerLeftCol = 12;
constexpr std::size_t kRightTowerRightCol = 13;
constexpr std::size_t kLeftTowerLeftCol = 6;    // $9CE6 -> 230 = 7*32 + 6
constexpr std::size_t kLeftTowerRightCol = 7;   // $9CE7 -> 231 = 7*32 + 7
constexpr std::size_t kTowerRows = 7;

struct Cell {
    std::size_t  row;
    std::size_t  col;
    std::uint8_t tile;
};
// $9D08 / $9D09 / $9D28 / $9D29 (:2704-2711).
constexpr Cell kFittings[4] = {
    { .row = 8, .col = 8, .tile = 0x72 },
    { .row = 8, .col = 9, .tile = 0xC4 },
    { .row = 9, .col = 8, .tile = 0xB7 },
    { .row = 9, .col = 9, .tile = 0xB8 },
};

constexpr std::uint8_t kPadHold = 187;
constexpr std::uint8_t kMaxHold = 255;
constexpr std::uint8_t kRocketReveal = 160;
constexpr std::uint8_t kRocketIgnitionHold = 128;
constexpr std::uint8_t kClimbStep = 10;
constexpr std::uint8_t kLetterStep = 6;

constexpr std::size_t kVehicle = 0;
constexpr std::size_t kSmokeL = 1;
constexpr std::size_t kSmokeR = 2;

// Run one more step of a chain: the handlers gate on the frame timer, so clear it first.
template <typename Handler>
void step(GameContext& game, Handler handler) {
    game.flow.timer1 = 0;
    handler(game);
}

// Every cell of the second map is a space except those the caller names.
void expectSecondMapClearExcept(const GameContext& game, auto&& painted) {
    for (std::size_t row = 0; row < kirpich::kBackgroundMapRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kBackgroundMapCols; ++col) {
            if (painted(row, col)) continue;
            EXPECT_EQ(game.display.secondMap[row][col], kSpace)
                << "second map (" << row << "," << col << ") should still be a space";
        }
    }
}

// The first map is never written by either chain (contract §3).
void expectFirstMapUntouched(const GameContext& game) {
    const BackgroundMap blank{};
    EXPECT_EQ(game.display.map, blank);
}

// A game whose Type A round earned a rocket, sitting on the bonus state.
GameContext earnedRocket(SpriteId tier = SpriteId::ROCKET_L) {
    GameContext game;
    game.flow.rocketSpriteIndex = tier;
    return game;
}

}  // namespace

// ── Test 1: LaunchPadGeometry ───────────────────────────────────────────────────────────────────────
// The pad both scenes share (InitRocketLaunchGraphics, :2729-2748), exercised through the rocket init
// because that state adds nothing to it. The clear is to spaces and not to zero (:2734-2735 through
// ClearTilemap :6345-6354), the backdrop is a 4x20 block and the towers are 7-cell columns — the two
// loop shapes the contract §2.1 separates.
TEST(LaunchScenes, LaunchPadGeometry) {
    GameContext game = earnedRocket();
    kirpich::systems::initRocketLaunch(game);

    EXPECT_EQ(game.display.sheet, TileSheet::MULTIPLAYER_BURAN);

    for (std::size_t r = 0; r < kirpich::kBuranBackdropTilemap.size(); ++r) {
        for (std::size_t c = 0; c < kirpich::kBuranBackdropTilemap[r].size(); ++c) {
            EXPECT_EQ(game.display.secondMap[kBackdropRow + r][c], kirpich::kBuranBackdropTilemap[r][c])
                << "backdrop cell (" << r << "," << c << ")";
        }
    }

    for (std::size_t r = 0; r < kTowerRows; ++r) {
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kRightTowerLeftCol],
                  kirpich::kRightTowerLeftSideTilemap[r]);
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kRightTowerRightCol],
                  kirpich::kRightTowerRightSideTilemap[r]);
    }

    expectSecondMapClearExcept(game, [](std::size_t row, std::size_t col) {
        const bool backdrop = row >= kBackdropRow && row < kBackdropRow + 4 && col < 20;
        const bool tower = row >= kTowerTopRow && row < kTowerTopRow + kTowerRows &&
                           (col == kRightTowerLeftCol || col == kRightTowerRightCol);
        return backdrop || tower;
    });

    expectFirstMapUntouched(game);
}

// ── Test 2: BuranPadVectors ─────────────────────────────────────────────────────────────────────────
// $26 (:2694-2726): the shared pad plus a left tower and four cells of launch hardware, the three
// launch objects, the switch to the second map, the 187-frame hold, and the launch music.
TEST(LaunchScenes, BuranPadVectors) {
    GameContext game;
    kirpich::systems::initBuran(game);

    for (std::size_t r = 0; r < kTowerRows; ++r) {
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kLeftTowerLeftCol],
                  kirpich::kLeftTowerLeftSideTilemap[r]);
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kLeftTowerRightCol],
                  kirpich::kLeftTowerRightSideTilemap[r]);
    }
    for (const Cell& f : kFittings) {
        EXPECT_EQ(game.display.secondMap[f.row][f.col], f.tile);
    }

    expectSecondMapClearExcept(game, [](std::size_t row, std::size_t col) {
        const bool backdrop = row >= kBackdropRow && row < kBackdropRow + 4 && col < 20;
        const bool tower = row >= kTowerTopRow && row < kTowerTopRow + kTowerRows &&
                           (col == kRightTowerLeftCol || col == kRightTowerRightCol ||
                            col == kLeftTowerLeftCol || col == kLeftTowerRightCol);
        const bool fitting = (row == 8 || row == 9) && (col == 8 || col == 9);
        return backdrop || tower || fitting;
    });
    expectFirstMapUntouched(game);

    // The shuttle visible, both plumes hidden (kBuranLaunchSprites).
    const auto sprites = kirpich::buranLaunchSprites();
    ASSERT_EQ(sprites.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(game.spriteRenderer.slots[i].hidden, sprites[i].hidden);
        EXPECT_EQ(game.spriteRenderer.slots[i].y, sprites[i].y);
        EXPECT_EQ(game.spriteRenderer.slots[i].x, sprites[i].x);
        EXPECT_EQ(game.spriteRenderer.slots[i].spriteId, sprites[i].sprite);
    }
    EXPECT_FALSE(game.spriteRenderer.slots[kVehicle].hidden);
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeL].hidden);
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeR].hidden);

    EXPECT_EQ(game.display.sheet, TileSheet::MULTIPLAYER_BURAN);
    EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);
    EXPECT_EQ(game.flow.timer1, kPadHold);
    EXPECT_EQ(game.audioCues.music, MusicId::ROCKET_LAUNCH);
    EXPECT_EQ(game.flow.gameState, GameState::PREPARE_BURAN_LAUNCH);
}

// ── Test 3: BuranCountdownChain ─────────────────────────────────────────────────────────────────────
// $27 / $28 / $29 (:2750-2803): each gates on the frame timer; the smoke is revealed, then swapped to
// its second frame with the field cleared, then the umbilicals swing away and nothing else moves.
TEST(LaunchScenes, BuranCountdownChain) {
    GameContext game;
    kirpich::systems::initBuran(game);

    // $27 holds while the timer runs.
    game.flow.timer1 = 1;
    kirpich::systems::prepareBuranLaunch(game);
    EXPECT_EQ(game.flow.gameState, GameState::PREPARE_BURAN_LAUNCH);
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeL].hidden);

    step(game, kirpich::systems::prepareBuranLaunch);
    EXPECT_FALSE(game.spriteRenderer.slots[kSmokeL].hidden);
    EXPECT_FALSE(game.spriteRenderer.slots[kSmokeR].hidden);
    EXPECT_EQ(game.flow.timer1, kMaxHold);
    EXPECT_EQ(game.flow.gameState, GameState::BURAN_IGNITION);

    // $28 holds and flickers; the flicker is on its own timer and toggles both plumes.
    game.flow.timer1 = 1;
    game.flow.timer2 = 0;
    kirpich::systems::buranIgnition(game);
    EXPECT_EQ(game.flow.gameState, GameState::BURAN_IGNITION);
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeL].hidden);   // toggled off
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeR].hidden);
    EXPECT_EQ(game.audioCues.noise, NoiseSfxId::IGNITION);
    EXPECT_EQ(game.flow.timer2, 10);

    step(game, kirpich::systems::buranIgnition);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].spriteId, SpriteId::ROCKET_SMOKE_2);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeR].spriteId, SpriteId::ROCKET_SMOKE_2);
    EXPECT_EQ(game.flow.timer1, kMaxHold);
    EXPECT_EQ(game.flow.gameState, GameState::BURAN_IGNITION_2);
    EXPECT_EQ(game.flow.wipeCounter, 2);  // the field fill armed the wipe (:2780-2781)

    // $29 removes exactly the four fitting cells and leaves the rest of the map alone.
    const BackgroundMap before = game.display.secondMap;
    step(game, kirpich::systems::buranIgnition2);
    EXPECT_EQ(game.flow.gameState, GameState::BURAN_LIFTOFF);
    for (std::size_t row = 0; row < kirpich::kBackgroundMapRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kBackgroundMapCols; ++col) {
            const bool fitting = (row == 8 || row == 9) && (col == 8 || col == 9);
            const std::uint8_t expected = fitting ? kSpace : before[row][col];
            EXPECT_EQ(game.display.secondMap[row][col], expected)
                << "cell (" << row << "," << col << ") after the umbilicals swing away";
        }
    }
}

// ── Test 4: BuranClimbAndExhaust ────────────────────────────────────────────────────────────────────
// $02 / $03 (:2805-2872). The liftoff sentinel is tested for equality, and the flight runs the
// coordinate *past zero* to reach a terminal above where it started — 136 steps (contract §8).
TEST(LaunchScenes, BuranClimbAndExhaust) {
    GameContext game;
    kirpich::systems::initBuran(game);
    game.flow.gameState = GameState::BURAN_LIFTOFF;

    const std::uint8_t start = game.spriteRenderer.slots[kVehicle].y;  // $5F from the stored objects
    ASSERT_EQ(start, 0x5F);

    // Seven steps to the sentinel; none of the first six lights the exhaust.
    for (int i = 0; i < 6; ++i) {
        step(game, kirpich::systems::buranLiftoff);
        EXPECT_EQ(game.flow.gameState, GameState::BURAN_LIFTOFF) << "step " << i;
        EXPECT_EQ(game.flow.timer1, kClimbStep);
    }
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].y, 0x59);

    step(game, kirpich::systems::buranLiftoff);
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].y, 0x58);
    EXPECT_FALSE(game.spriteRenderer.slots[kSmokeL].hidden);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].y, 0x78);  // $58 + $20
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].x, 0x4C);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].spriteId, SpriteId::BURAN_EXHAUST_1);
    EXPECT_TRUE(game.spriteRenderer.slots[kSmokeR].hidden);
    EXPECT_EQ(game.audioCues.noise, NoiseSfxId::LIFTOFF);
    EXPECT_EQ(game.flow.gameState, GameState::BURAN_RISING);

    // The flight. Count the steps to the terminal and prove the coordinate wrapped on the way.
    bool sawZero = false;
    int steps = 0;
    while (game.flow.gameState == GameState::BURAN_RISING && steps < 400) {
        game.flow.timer2 = 1;  // hold the exhaust-frame animation off so the count is the climb's
        step(game, kirpich::systems::buranRising);
        ++steps;
        if (game.spriteRenderer.slots[kVehicle].y == 0x00) sawZero = true;
    }
    EXPECT_EQ(steps, 136);
    EXPECT_TRUE(sawZero) << "the climb must pass through zero, not saturate at it";
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].y, 0xD0);
    EXPECT_EQ(game.flow.gameState, GameState::PRINT_CONGRATULATIONS);
    EXPECT_EQ(game.flow.congratulationsColumn, 2);  // seeded from $9C82 (:2851-2854)

    // The exhaust-frame tail alternates the pair on its own timer (:2859-2868).
    GameContext rising;
    kirpich::systems::initBuran(rising);
    rising.spriteRenderer.slots[kSmokeL].spriteId = SpriteId::BURAN_EXHAUST_1;
    rising.flow.timer1 = 5;
    rising.flow.timer2 = 0;
    kirpich::systems::buranRising(rising);
    EXPECT_EQ(rising.spriteRenderer.slots[kSmokeL].spriteId, SpriteId::BURAN_EXHAUST_2);
    EXPECT_EQ(rising.flow.timer2, kLetterStep);
    rising.flow.timer2 = 0;
    kirpich::systems::buranRising(rising);
    EXPECT_EQ(rising.spriteRenderer.slots[kSmokeL].spriteId, SpriteId::BURAN_EXHAUST_1);
}

// ── Test 5: CongratulationsPrinting ─────────────────────────────────────────────────────────────────
// $2C / $2D (:2874-2929): sixteen letters along row 4 at columns 2-17, each with $B6 beneath it and a
// sound, one every six frames; then the hand-off to the scoreboard *without* an audio reset.
TEST(LaunchScenes, CongratulationsPrinting) {
    GameContext game;
    kirpich::systems::initBuran(game);
    game.flow.congratulationsColumn = 2;
    game.flow.gameState = GameState::PRINT_CONGRATULATIONS;

    const BackgroundMap before = game.display.secondMap;

    // Gated on the frame timer.
    game.flow.timer1 = 1;
    kirpich::systems::printCongratulations(game);
    EXPECT_EQ(game.flow.congratulationsColumn, 2);

    for (std::size_t i = 0; i < kirpich::kCongratulationsTilemap.size(); ++i) {
        game.audioCues.square = SquareSfxId::NONE;
        step(game, kirpich::systems::printCongratulations);

        const std::size_t col = 2 + i;
        EXPECT_EQ(game.display.secondMap[4][col], kirpich::kCongratulationsTilemap[i])
            << "letter " << i;
        EXPECT_EQ(game.display.secondMap[5][col], 0xB6) << "tile beneath letter " << i;
        EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
        if (i + 1 < kirpich::kCongratulationsTilemap.size()) {
            EXPECT_EQ(game.flow.timer1, kLetterStep);
            EXPECT_EQ(game.flow.gameState, GameState::PRINT_CONGRATULATIONS);
        }
    }

    EXPECT_EQ(game.flow.congratulationsColumn, 18);
    EXPECT_EQ(game.flow.timer1, kMaxHold);
    EXPECT_EQ(game.flow.gameState, GameState::CONGRATULATIONS);

    // Only rows 4 and 5, columns 2-17, changed.
    for (std::size_t row = 0; row < kirpich::kBackgroundMapRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kBackgroundMapCols; ++col) {
            if ((row == 4 || row == 5) && col >= 2 && col < 18) continue;
            EXPECT_EQ(game.display.secondMap[row][col], before[row][col])
                << "cell (" << row << "," << col << ") outside the message";
        }
    }

    // An unseeded cursor prints nothing rather than running off the message. The original has no such
    // check — it would read a neighbouring ROM byte — but the state is entered only from the rising
    // state, which always seeds it, so the two agree everywhere the game can actually go.
    {
        GameContext cold;
        kirpich::systems::initBuran(cold);
        const BackgroundMap padded = cold.display.secondMap;
        cold.flow.congratulationsColumn = 0;
        step(cold, kirpich::systems::printCongratulations);
        EXPECT_EQ(cold.display.secondMap, padded);
        EXPECT_EQ(cold.flow.congratulationsColumn, 0);
    }

    // $2D gates, then restores the gameplay regime and hands to the scoreboard — with no audio reset.
    game.flow.timer1 = 1;
    kirpich::systems::congratulations(game);
    EXPECT_EQ(game.flow.gameState, GameState::CONGRATULATIONS);

    step(game, kirpich::systems::congratulations);
    EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
    EXPECT_EQ(game.display.displayed, DisplayedMap::FIRST);
    EXPECT_EQ(game.flow.gameState, GameState::TYPE_B_VICTORY_JINGLE);
    EXPECT_FALSE(game.audioCues.resetRequested) << "the Buran exit does not re-init the driver";
}

// ── Test 6: RocketChainVectors ──────────────────────────────────────────────────────────────────────
// $34 / $2E / $2F (:2931-2973): the hold, the earned tier consumed into slot 0, the sparser pad
// (asymmetry 1), and the reveal.
TEST(LaunchScenes, RocketChainVectors) {
    GameContext game = earnedRocket(SpriteId::ROCKET_M);
    game.flow.gameState = GameState::GAME_OVER_TO_BONUS;

    game.flow.timer1 = 1;
    kirpich::systems::gameOverToBonusEnding(game);
    EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_TO_BONUS);

    step(game, kirpich::systems::gameOverToBonusEnding);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_ROCKET_LAUNCH);

    kirpich::systems::initRocketLaunch(game);

    // The tier the score earned goes into the vehicle slot, and the record is consumed.
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].spriteId, SpriteId::ROCKET_M);
    EXPECT_EQ(game.flow.rocketSpriteIndex, SpriteId{});

    // Asymmetry 1: no left tower, no umbilicals — those cells are still the cleared space.
    for (std::size_t r = 0; r < kTowerRows; ++r) {
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kLeftTowerLeftCol], kSpace);
        EXPECT_EQ(game.display.secondMap[kTowerTopRow + r][kLeftTowerRightCol], kSpace);
    }
    for (const Cell& f : kFittings) {
        EXPECT_EQ(game.display.secondMap[f.row][f.col], kSpace);
    }

    EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);
    EXPECT_EQ(game.flow.timer1, kPadHold);
    EXPECT_EQ(game.audioCues.music, MusicId::ROCKET_LAUNCH);
    EXPECT_EQ(game.flow.gameState, GameState::ROCKET);

    step(game, kirpich::systems::rocket);
    EXPECT_FALSE(game.spriteRenderer.slots[kSmokeL].hidden);
    EXPECT_FALSE(game.spriteRenderer.slots[kSmokeR].hidden);
    EXPECT_EQ(game.flow.timer1, kRocketReveal);
    EXPECT_EQ(game.flow.gameState, GameState::ROCKET_IGNITION);
}

// ── Test 7: RocketClimbAndExit ──────────────────────────────────────────────────────────────────────
// $30 / $31 / $32 / $33 (:2975-3065): no smoke art on ignition (asymmetry 2), a different sentinel and
// terminal from the Buran's, no cursor seeded (asymmetry 3), and an exit with no timer gate that does
// ask for an audio reset.
TEST(LaunchScenes, RocketClimbAndExit) {
    GameContext game = earnedRocket();
    kirpich::systems::initRocketLaunch(game);
    step(game, kirpich::systems::rocket);

    const SpriteId smokeBefore = game.spriteRenderer.slots[kSmokeL].spriteId;
    step(game, kirpich::systems::rocketIgnition);
    // Asymmetry 2: the Buran's ignition swaps both plumes; this one leaves the art alone.
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].spriteId, smokeBefore);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeR].spriteId, smokeBefore);
    EXPECT_EQ(game.flow.timer1, kRocketIgnitionHold);
    EXPECT_EQ(game.flow.wipeCounter, 2);
    EXPECT_EQ(game.flow.gameState, GameState::ROCKET_LIFTOFF);

    ASSERT_EQ(game.spriteRenderer.slots[kVehicle].y, 0x6F);
    for (int i = 0; i < 4; ++i) {
        step(game, kirpich::systems::rocketLiftoff);
        EXPECT_EQ(game.flow.gameState, GameState::ROCKET_LIFTOFF) << "step " << i;
    }
    step(game, kirpich::systems::rocketLiftoff);
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].y, 0x6A);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].y, 0x7A);  // $6A + $10, a different drop from the Buran's
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].x, 0x54);
    EXPECT_EQ(game.spriteRenderer.slots[kSmokeL].spriteId, SpriteId::ROCKET_EXHAUST_1);
    EXPECT_EQ(game.flow.gameState, GameState::ROCKET_MAIN_ENGINE_FIRE);

    int steps = 0;
    while (game.flow.gameState == GameState::ROCKET_MAIN_ENGINE_FIRE && steps < 400) {
        game.flow.timer2 = 1;
        step(game, kirpich::systems::rocketMainEngineFire);
        ++steps;
    }
    EXPECT_EQ(steps, 138) << "a different climb length from the Buran's 136";
    EXPECT_EQ(game.spriteRenderer.slots[kVehicle].y, 0xE0) << "a different terminal from the Buran's $D0";
    EXPECT_EQ(game.flow.gameState, GameState::END_OF_BONUS_SCENE);
    // Asymmetry 3: no congratulations screen, so no cursor was seeded.
    EXPECT_EQ(game.flow.congratulationsColumn, 0);

    // $33 has no timer gate: it runs on a frame where every sibling would have returned.
    game.flow.timer1 = 200;
    kirpich::systems::endOfBonusScene(game);
    EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
    EXPECT_EQ(game.display.displayed, DisplayedMap::FIRST);
    EXPECT_TRUE(game.audioCues.resetRequested) << "the rocket exit re-inits the driver";
    EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_A_DIFFICULTY);
}

// ── Test 8: LaunchChainsHaveNoDeadEnd ───────────────────────────────────────────────────────────────
// The holes this unit fills. Both entry states — the height-5 fork out of the ending dance
// (type_b_ending.cpp:208) and the bonus state the game-over chain writes — reach real handlers once
// these are installed; and every state either chain can write resolves to some installed handler, so a
// renumbering cannot silently reopen a hole.
TEST(LaunchScenes, LaunchChainsHaveNoDeadEnd) {
    const GameState entries[] = { GameState::INIT_BURAN, GameState::GAME_OVER_TO_BONUS };
    const GameState reached[] = { GameState::PREPARE_BURAN_LAUNCH, GameState::INIT_ROCKET_LAUNCH };

    for (std::size_t i = 0; i < 2; ++i) {
        // Without the install the slot holds the dispatcher's not-ported stub and the state sticks.
        {
            GameStateDispatcher dispatcher;
            GameContext game = earnedRocket();
            game.flow.gameState = entries[i];
            dispatcher.tick(game, retropp::ActionSet{});
            EXPECT_EQ(game.flow.gameState, entries[i]);
        }

        GameStateDispatcher dispatcher;
        kirpich::systems::installLaunchSceneHandlers(dispatcher);
        GameContext game = earnedRocket();
        game.flow.gameState = entries[i];
        dispatcher.tick(game, retropp::ActionSet{});
        EXPECT_EQ(game.flow.gameState, reached[i]);
    }

    // Every state either chain writes is installed here, except the two rejoin points which belong to
    // screens that already shipped.
    const GameState owned[] = {
        GameState::INIT_BURAN,          GameState::PREPARE_BURAN_LAUNCH,
        GameState::BURAN_IGNITION,      GameState::BURAN_IGNITION_2,
        GameState::BURAN_LIFTOFF,       GameState::BURAN_RISING,
        GameState::PRINT_CONGRATULATIONS, GameState::CONGRATULATIONS,
        GameState::GAME_OVER_TO_BONUS,  GameState::INIT_ROCKET_LAUNCH,
        GameState::ROCKET,              GameState::ROCKET_IGNITION,
        GameState::ROCKET_LIFTOFF,      GameState::ROCKET_MAIN_ENGINE_FIRE,
        GameState::END_OF_BONUS_SCENE,
    };
    ASSERT_EQ(std::size(owned), 15u);

    GameStateDispatcher dispatcher;
    kirpich::systems::installLaunchSceneHandlers(dispatcher);
    for (const GameState state : owned) {
        GameContext game = earnedRocket();
        game.flow.gameState = state;
        game.flow.timer1 = 0;
        // The print state is reachable only from the rising state, which seeds this first; entering it
        // cold is not something the game does.
        game.flow.congratulationsColumn = 2;

        const auto mapBefore = game.display.secondMap;
        const auto slotsBefore = game.spriteRenderer.slots;
        const auto cuesBefore = game.audioCues;
        const auto columnBefore = game.flow.congratulationsColumn;

        dispatcher.tick(game, retropp::ActionSet{});

        // The dispatcher's not-ported stub writes nothing at all. Every handler here leaves at least
        // one mark: a new state, a redrawn map, a moved or re-arted sprite, a cue, or a stepped cursor.
        const bool worked = game.flow.gameState != state ||
                            game.display.secondMap != mapBefore ||
                            game.spriteRenderer.slots != slotsBefore ||
                            !(game.audioCues == cuesBefore) ||
                            game.flow.congratulationsColumn != columnBefore;
        EXPECT_TRUE(worked)
            << "state $" << std::hex << static_cast<int>(state) << " ran the stub";
    }
}
