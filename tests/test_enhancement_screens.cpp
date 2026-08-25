// The enhancement screens — a behavioral test over src/systems/enhancement_screens.h.
//
// Device-free: the install is pure wiring over the dispatcher, and each screen's handlers are pure
// logic over the game-state aggregate.
//
// What this holds is the flag-to-screen pairing. One call installs all three screens and the pairing
// lives inside the unit, where a reader of the host cannot see it — so a crossed wiring is invisible
// at the call site, and this is the guard against one.

#include <gtest/gtest.h>

#include <initializer_list>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "retropp/input.h"
#include "state/settings.h"
#include "systems/enhancement_screens.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::GameState;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::SettingsWiring;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// Open a screen at its init slot and press one action on it, through the dispatcher the way a frame
// does. Three ticks: the init runs and writes the loop state, an empty tick puts the loop handler
// live with nothing held, then the action arrives as a fresh press against that empty previous set.
void openAndPress(GameStateDispatcher& dispatcher, GameContext& game, GameState init, Action action) {
    game.flow.gameState = init;
    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, actionSet({action}));
}

// (1) Each screen's enable row moves its OWN flag and no other — the fixes screen the audio fix, the
// ghost screen the ghost piece, the new-modes screen the extra game types — and the change reaches
// the host's single seam from all three.
//
// The install runs in an inner scope that then ends, so the option tables' lifetime is exercised
// rather than assumed: a carousel's wiring borrows its table as a span, and the handlers that read
// it outlive everything the install call had on its stack.
TEST(EnhancementScreens, EachScreenMovesItsOwnFlag) {
    kirpich::Settings   settings;
    GameStateDispatcher dispatcher;
    int                 changed = 0;

    {
        const SettingsWiring settingsWiring{.settings = &settings};
        kirpich::systems::installEnhancementScreens(dispatcher, settings, [&changed] { ++changed; },
                                                    settingsWiring);
    }

    struct Screen {
        GameState   init;
        const char* name;
        bool kirpich::Settings::*flag;
    };
    constexpr Screen kScreens[] = {
        {GameState::INIT_FIXES_SCREEN, "fixes", &kirpich::Settings::fixAudio},
        {GameState::INIT_GHOST_SCREEN, "ghost", &kirpich::Settings::ghostPiece},
        {GameState::INIT_MODE_SCREEN, "new modes", &kirpich::Settings::newModes},
    };

    int expectedChanges = 0;
    for (const Screen& screen : kScreens) {
        settings = kirpich::Settings{};
        GameContext game;

        openAndPress(dispatcher, game, screen.init, Action::MenuRight);
        ++expectedChanges;

        EXPECT_TRUE(settings.*(screen.flag)) << screen.name << " did not move its own flag";
        EXPECT_EQ(changed, expectedChanges) << screen.name << " did not fire the host's seam";

        // And nothing else moved: every other flag is still at the value a fresh Settings has.
        kirpich::Settings onlyThisOne;
        onlyThisOne.*(screen.flag) = true;
        EXPECT_EQ(settings, onlyThisOne) << screen.name << " reached a flag that is not its own";
    }
}

}  // namespace
