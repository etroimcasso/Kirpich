#pragma once

// Where a screen came from, for the screens that are more than one deep.
//
// The screens the settings screen opens each have exactly one parent, so each of them hard-codes it
// and returns there by name (returnToSettings, systems/settings_screen.h). The statistics are four
// deep - the title screen, the chooser it opens, a game type and a combination - and every level is
// an instance of one list screen, so no handler can know by name where it was entered from. It has
// to be recorded on the way in.
//
// The stack lives on ScreenUiState with the rest of the port's own screen state: an array of states
// and a count of how many are live. A push records where the player is and enters the next screen; a
// pop returns to the top of the stack. Popping an empty stack reaches the title screen rather than
// nowhere, so a stack that has somehow been emptied strands nobody.
//
// A push at the ceiling is refused and changes nothing. Dropping the bottom of the stack to make room
// would lose the way home, which is worse than not going deeper.
//
// It serves the new screens. The screens that already hard-code their parent are left as they are -
// converting them is a sweep across every screen in the game, and that they COULD use this is worth
// recording rather than acting on here.

#include <kirpich/game_state.h>

#include "systems/game_context.h"

namespace kirpich::systems {

// Record where the player is and enter `next`. Returns false and changes nothing when the stack is
// already at kScreenStackDepth.
inline bool pushScreen(GameContext& game, GameState next) {
    ScreenUiState& ui = game.screens;
    if (ui.screenStackDepth >= kScreenStackDepth) {
        return false;
    }
    ui.screenStack[ui.screenStackDepth++] = game.flow.gameState;
    game.flow.gameState                   = next;
    return true;
}

// Return to the screen on top of the stack, or to the title screen when there is none.
inline void popScreen(GameContext& game) {
    ScreenUiState& ui = game.screens;
    if (ui.screenStackDepth == 0) {
        game.flow.gameState = GameState::TITLE_SCREEN;
        return;
    }
    game.flow.gameState = ui.screenStack[--ui.screenStackDepth];
}

}  // namespace kirpich::systems
