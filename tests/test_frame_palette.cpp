// Which palette the frame is drawn through while a round's palette follows its level — behavioral
// tests over src/systems/frame_palette.h.
//
// Device-free. The rotation set's colors are handed to a writer the tests count, so the reconcile is
// asserted by what it would have written rather than through a renderer.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "render/palettes.h"
#include "render/rotation_presets.h"
#include "state/custom_rotations.h"
#include "state/settings.h"
#include "systems/frame_palette.h"
#include "systems/game_context.h"

namespace {

using kirpich::GameState;
using kirpich::PaletteRotation;
using kirpich::RotationProgression;
using kirpich::Settings;
using kirpich::render::ShadeRamp;
using kirpich::systems::GameContext;

// The round's own states, spelled here independently of the table in frame_palette.cpp: the round's
// play, its game-over curtain and screen, the Type B scoreboard and dance, and both launch scenes.
constexpr std::array kRoundStates{
    GameState::INIT_GAME,
    GameState::NORMAL_GAMEPLAY,
    GameState::INIT_GAME_OVER,
    GameState::GAME_OVER_CURTAIN,
    GameState::GAME_OVER_SCREEN,
    GameState::STATE_0C_UNKNOWN,
    GameState::TYPE_B_VICTORY_JINGLE,
    GameState::INIT_TYPE_B_SCOREBOARD,
    GameState::INIT_TYPE_B_BONUS,
    GameState::DANCERS,
    GameState::INIT_BURAN,
    GameState::PREPARE_BURAN_LAUNCH,
    GameState::BURAN_IGNITION,
    GameState::BURAN_IGNITION_2,
    GameState::BURAN_LIFTOFF,
    GameState::BURAN_RISING,
    GameState::PRINT_CONGRATULATIONS,
    GameState::CONGRATULATIONS,
    GameState::GAME_OVER_TO_BONUS,
    GameState::INIT_ROCKET_LAUNCH,
    GameState::ROCKET,
    GameState::ROCKET_IGNITION,
    GameState::ROCKET_LIFTOFF,
    GameState::ROCKET_MAIN_ENGINE_FIRE,
    GameState::END_OF_BONUS_SCENE,
};

GameContext contextAt(GameState state, std::uint8_t level) {
    GameContext game;
    game.flow.gameState = state;
    game.flow.level     = level;
    return game;
}

// Ten distinct palettes, so a level's entry can be told from every other level's.
RotationProgression distinctRotation(std::uint8_t base) {
    RotationProgression r;
    for (std::size_t j = 0; j < kirpich::kRotationLevelCount; ++j) {
        const auto v = static_cast<std::uint8_t>(base + 5 * j);
        r.levels[j]  = ShadeRamp{.darkest  = {.r = v, .g = 1, .b = 2},
                                 .dark     = {.r = v, .g = 80, .b = 81},
                                 .light    = {.r = v, .g = 160, .b = 161},
                                 .lightest = {.r = v, .g = 240, .b = 241}};
    }
    return r;
}

Settings withMode(PaletteRotation mode) { return Settings{.paletteRotation = mode}; }

}  // namespace

// (1) The round's own states are exactly the twenty-five, over the whole byte: every port screen, the
// top-score entry and every menu draws the player's palette.
TEST(FramePalette, RoundStatesAreExactlyTheRoundsOwn) {
    EXPECT_EQ(kRoundStates.size(), 25u);
    for (int byte = 0; byte <= 255; ++byte) {
        const auto state    = static_cast<GameState>(byte);
        const bool expected = std::find(kRoundStates.begin(), kRoundStates.end(), state) !=
                              kRoundStates.end();
        EXPECT_EQ(kirpich::systems::roundPaletteShown(state), expected) << "state byte " << byte;
    }
}

// (2) A level wears the palette of its last digit: the ten wrap rather than stopping at the last.
TEST(FramePalette, ALevelWearsItsLastDigitsPalette) {
    EXPECT_EQ(kirpich::systems::rotationLevelOf(0), 0u);
    EXPECT_EQ(kirpich::systems::rotationLevelOf(9), 9u);
    EXPECT_EQ(kirpich::systems::rotationLevelOf(10), 0u);
    EXPECT_EQ(kirpich::systems::rotationLevelOf(19), 9u);
    EXPECT_EQ(kirpich::systems::rotationLevelOf(20), 0u);
}

// (3) With the mode off, nothing is wanted in any state at any level - mid-round included - so every
// frame draws through the player's own palette.
TEST(FramePalette, OffWantsNothingAnywhere) {
    const RotationProgression working = distinctRotation(30);
    for (int byte = 0; byte <= 255; ++byte) {
        for (std::uint8_t level = 0; level < 25; ++level) {
            const GameContext game = contextAt(static_cast<GameState>(byte), level);
            EXPECT_FALSE(kirpich::systems::rotationPaletteFor(withMode(PaletteRotation::OFF),
                                                              working, game))
                << "state byte " << byte << " level " << +level;
            EXPECT_EQ(kirpich::systems::framePaletteIndex(withMode(PaletteRotation::OFF), working,
                                                          game),
                      kirpich::paletteIndexOf(Settings{}));
        }
    }
}

// (4) With a preset, the round's states draw the preset's entry for the level and every screen
// outside them draws nothing.
TEST(FramePalette, APresetIsDrawnInTheRoundsStatesAndNowhereElse) {
    const RotationProgression working;

    for (const PaletteRotation mode :
         {PaletteRotation::HOTTER, PaletteRotation::COLDER, PaletteRotation::RAINBOW}) {
        const kirpich::render::RotationPalettes* preset = kirpich::render::presetRotation(mode);
        ASSERT_NE(preset, nullptr);
        for (std::uint8_t level = 0; level < 25; ++level) {
            for (const GameState state : {GameState::NORMAL_GAMEPLAY, GameState::GAME_OVER_SCREEN,
                                          GameState::DANCERS}) {
                const auto wanted = kirpich::systems::rotationPaletteFor(
                    withMode(mode), working, contextAt(state, level));
                ASSERT_TRUE(wanted) << "mode " << +static_cast<int>(mode) << " level " << +level;
                EXPECT_EQ(*wanted, (*preset)[level % 10])
                    << "mode " << +static_cast<int>(mode) << " level " << +level;
            }
            for (const GameState state :
                 {GameState::TITLE_SCREEN, GameState::SETTINGS, GameState::PALETTE_SETTINGS,
                  GameState::ENTER_TOP_SCORE, GameState::ACHIEVEMENT_NOTICE}) {
                EXPECT_FALSE(kirpich::systems::rotationPaletteFor(withMode(mode), working,
                                                                  contextAt(state, level)))
                    << "mode " << +static_cast<int>(mode) << " state "
                    << +static_cast<int>(state);
            }
        }
    }
}

// (5) CUSTOM draws the working rotation the host passes, level for level; nothing else is consulted.
TEST(FramePalette, CustomDrawsTheWorkingRotation) {
    const RotationProgression working = distinctRotation(30);
    for (std::uint8_t level = 0; level < 20; ++level) {
        const auto wanted = kirpich::systems::rotationPaletteFor(
            withMode(PaletteRotation::CUSTOM), working,
            contextAt(GameState::NORMAL_GAMEPLAY, level));
        ASSERT_TRUE(wanted) << "level " << +level;
        EXPECT_EQ(*wanted, working.levels[level % 10]) << "level " << +level;
    }

    // A present slot holding a different rotation changes nothing: the slots are what the working
    // rotation is saved into, never what a round is drawn through.
    kirpich::CustomRotations rotations;
    rotations.working  = working;
    rotations.slots[0] = kirpich::CustomRotationSlot{.present = true,
                                                     .progression = distinctRotation(90)};
    const auto wanted = kirpich::systems::rotationPaletteFor(
        withMode(PaletteRotation::CUSTOM), rotations.working,
        contextAt(GameState::NORMAL_GAMEPLAY, 3));
    ASSERT_TRUE(wanted);
    EXPECT_EQ(*wanted, working.levels[3]);
    EXPECT_NE(*wanted, rotations.slots[0].progression.levels[3]);
}

// (6) The palette follows the level shown, whatever else the round is: the attract demo rotates, a
// Type B round wears its level's palette, and heart mode shifts the gravity, not the palette.
TEST(FramePalette, TheDemoTypeBAndHeartModeAllFollowTheLevel) {
    const RotationProgression                working;
    const kirpich::render::RotationPalettes& hotter = kirpich::render::kHotterRotation;
    const Settings                           settings = withMode(PaletteRotation::HOTTER);

    GameContext demo = contextAt(GameState::NORMAL_GAMEPLAY, 2);
    demo.demo.activeDemo = kirpich::ActiveDemo::TYPE_A;
    const auto demoWanted = kirpich::systems::rotationPaletteFor(settings, working, demo);
    ASSERT_TRUE(demoWanted) << "the demo is a round and rotates";
    EXPECT_EQ(*demoWanted, hotter[2]);

    GameContext typeB = contextAt(GameState::NORMAL_GAMEPLAY, 5);
    typeB.flow.gameType = kirpich::GameType::TYPE_B;
    const auto typeBWanted = kirpich::systems::rotationPaletteFor(settings, working, typeB);
    ASSERT_TRUE(typeBWanted);
    EXPECT_EQ(*typeBWanted, hotter[5]);

    GameContext heart = contextAt(GameState::NORMAL_GAMEPLAY, 3);
    heart.flow.heartMode = 1;
    const auto heartWanted = kirpich::systems::rotationPaletteFor(settings, working, heart);
    ASSERT_TRUE(heartWanted);
    EXPECT_EQ(*heartWanted, hotter[3]) << "the displayed level picks, not the shifted gravity";
}

// (7) The frame draws through the rotation set whenever a palette is wanted, and through the player's
// own palette otherwise - the custom set while it is in use, the chosen ramp when it is not.
TEST(FramePalette, TheFrameIndexIsTheRotationSetOrThePlayersPalette) {
    const RotationProgression working;
    for (const bool custom : {false, true}) {
        Settings settings{.shadeRamp = 17, .customPalette = custom};
        const std::uint8_t base = kirpich::paletteIndexOf(settings);
        EXPECT_EQ(base, custom ? kirpich::render::kCustomRamp : std::uint8_t{17});

        for (const PaletteRotation mode :
             {PaletteRotation::OFF, PaletteRotation::HOTTER, PaletteRotation::COLDER,
              PaletteRotation::RAINBOW, PaletteRotation::CUSTOM}) {
            settings.paletteRotation = mode;
            for (const GameState state :
                 {GameState::NORMAL_GAMEPLAY, GameState::TITLE_SCREEN, GameState::SETTINGS}) {
                const GameContext game     = contextAt(state, 4);
                const bool        wanted   = kirpich::systems::rotationPaletteFor(settings, working,
                                                                                  game)
                                                 .has_value();
                const std::uint8_t expected = wanted ? kirpich::render::kRotationRamp : base;
                EXPECT_EQ(kirpich::systems::framePaletteIndex(settings, working, game), expected)
                    << "custom " << custom << " mode " << +static_cast<int>(mode) << " state "
                    << +static_cast<int>(state);
                EXPECT_EQ(wanted, mode != PaletteRotation::OFF &&
                                      state == GameState::NORMAL_GAMEPLAY);
            }
        }
    }
}

// (8) The reconcile writes the rotation set only when the palette wanted differs from what the set
// holds, over a scripted run of ticks: CUSTOM over a fresh working rotation while the set holds the
// grayscale it was uploaded with, a menu, a round init, ticks at one level, a level-up, the game-over
// screen, the title, and the mode switched off mid-round. Every write is counted and checked.
TEST(FramePalette, TheReconcileWritesOnlyWhenTheWantedPaletteChanges) {
    ShadeRamp              held = kirpich::render::freshCustomPalette();
    std::vector<ShadeRamp> writes;
    const auto             write = [&held, &writes](const ShadeRamp& shades) {
        writes.push_back(shades);
        held = shades;
    };
    const RotationProgression                fresh;
    const kirpich::render::RotationPalettes& hotter = kirpich::render::kHotterRotation;

    const auto tick = [&](PaletteRotation mode, GameState state, std::uint8_t level) {
        kirpich::systems::reconcileRotationPalette(withMode(mode), fresh, contextAt(state, level),
                                                   held, write);
    };

    tick(PaletteRotation::CUSTOM, GameState::NORMAL_GAMEPLAY, 0);    // grayscale over grayscale
    EXPECT_EQ(writes.size(), 0u);

    tick(PaletteRotation::HOTTER, GameState::TITLE_SCREEN, 0);       // a menu: nothing wanted
    EXPECT_EQ(writes.size(), 0u);

    tick(PaletteRotation::HOTTER, GameState::INIT_GAME, 0);          // the round starts at 0
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes.back(), hotter[0]);

    for (int i = 0; i < 5; ++i) {
        tick(PaletteRotation::HOTTER, GameState::NORMAL_GAMEPLAY, 0);  // same level: no write
    }
    EXPECT_EQ(writes.size(), 1u);

    tick(PaletteRotation::HOTTER, GameState::NORMAL_GAMEPLAY, 1);    // the level-up
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes.back(), hotter[1]);

    tick(PaletteRotation::HOTTER, GameState::GAME_OVER_SCREEN, 1);   // same palette holds
    tick(PaletteRotation::HOTTER, GameState::TITLE_SCREEN, 1);       // a menu: nothing wanted
    tick(PaletteRotation::OFF, GameState::NORMAL_GAMEPLAY, 1);       // off: nothing wanted
    EXPECT_EQ(writes.size(), 2u);
    EXPECT_EQ(held, hotter[1]) << "the set keeps its colors while no palette is wanted";

    tick(PaletteRotation::HOTTER, GameState::NORMAL_GAMEPLAY, 1);    // back on, same level
    EXPECT_EQ(writes.size(), 2u) << "returning to the palette the set holds writes nothing";
}
