// The navigation stack — behavioral tests over src/systems/screen_stack.h.
//
// Device-free: two functions over the game-state aggregate. The stack is the port's own, so every
// asserted value comes from the surface's stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include <kirpich/game_state.h>

#include "state/screen_ui_state.h"
#include "systems/game_context.h"
#include "systems/screen_stack.h"

namespace {

using kirpich::GameState;
using kirpich::systems::GameContext;
using kirpich::systems::popScreen;
using kirpich::systems::pushScreen;

// (1) A push records where the player was and enters the next screen; a pop returns to it. Driven
// four deep, which is the deepest path the game has, and unwound one level at a time so that each
// return is checked rather than only the last.
TEST(ScreenStack, PushAndPopReturnToWhereTheyCameFrom) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;

    constexpr std::array<GameState, 4> descent{
        GameState::INIT_STATS_MENU,
        GameState::INIT_STATS_PAGE,
        GameState::INIT_TYPE_C_DIFFICULTY,
        GameState::INIT_GHOST_SCREEN,
    };
    // Where each push is made FROM, which is what that level's pop has to come back to.
    constexpr std::array<GameState, 4> from{
        GameState::TITLE_SCREEN,
        GameState::INIT_STATS_MENU,
        GameState::INIT_STATS_PAGE,
        GameState::INIT_TYPE_C_DIFFICULTY,
    };

    for (std::size_t level = 0; level < descent.size(); ++level) {
        EXPECT_TRUE(pushScreen(game, descent[level])) << "level " << level;
        EXPECT_EQ(game.flow.gameState, descent[level]) << "level " << level;
        EXPECT_EQ(game.screens.screenStackDepth, level + 1) << "level " << level;
    }

    for (std::size_t level = descent.size(); level-- > 0;) {
        popScreen(game);
        EXPECT_EQ(game.flow.gameState, from[level]) << "level " << level;
        EXPECT_EQ(game.screens.screenStackDepth, level) << "level " << level;
    }

    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
    EXPECT_EQ(game.screens.screenStackDepth, 0u);
}

// (2) The stack saturates at its depth rather than overrunning, and a push at the ceiling is REFUSED
// rather than quietly dropping the bottom entry. Dropping the bottom would lose the way home, which
// is worse than not going deeper - so the refusal is asserted by the whole stack being unchanged and
// the player still on the screen they were on.
TEST(ScreenStack, PushAtTheCeilingIsRefusedAndKeepsTheWayHome) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;

    for (std::size_t level = 0; level < kirpich::kScreenStackDepth; ++level) {
        ASSERT_TRUE(pushScreen(game, GameState::STATS_PAGE)) << "level " << level;
    }
    ASSERT_EQ(game.screens.screenStackDepth, kirpich::kScreenStackDepth);

    const auto      fullStack = game.screens.screenStack;
    const GameState standing  = game.flow.gameState;

    EXPECT_FALSE(pushScreen(game, GameState::INIT_SETTINGS));
    EXPECT_EQ(game.screens.screenStackDepth, kirpich::kScreenStackDepth);
    EXPECT_EQ(game.screens.screenStack, fullStack) << "a refused push writes nothing";
    EXPECT_EQ(game.flow.gameState, standing) << "and leaves the player where they were";

    // The bottom is still the title screen, so unwinding the whole stack still gets home.
    for (std::size_t level = 0; level < kirpich::kScreenStackDepth; ++level) {
        popScreen(game);
    }
    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
}

// (3) Popping an empty stack reaches the title screen rather than nowhere. A screen that has somehow
// lost its stack strands nobody: the pop is safe to call unconditionally, which is what lets every
// list instance answer B the same way.
TEST(ScreenStack, PoppingAnEmptyStackReachesTheTitleScreen) {
    GameContext game;
    game.flow.gameState = GameState::STATS_MENU;
    ASSERT_EQ(game.screens.screenStackDepth, 0u);

    popScreen(game);
    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
    EXPECT_EQ(game.screens.screenStackDepth, 0u) << "and the depth does not go below zero";

    // Again, from the state it just reached: still safe, still the title screen.
    popScreen(game);
    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
    EXPECT_EQ(game.screens.screenStackDepth, 0u);
}

}  // namespace
