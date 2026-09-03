// The statistics screens — behavioral tests over src/systems/stats_screens.h and the title screen's
// bottom row.
//
// Device-free: the install is pure wiring over the dispatcher, and every handler is pure logic over
// the game-state aggregate. The screens are the port's own, so every asserted value comes from the
// surface's stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/charmap.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/list_screen.h"
#include "systems/settings_screen.h"
#include "systems/stats_pages.h"
#include "systems/stats_screens.h"
#include "systems/title_screens.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::GameState;
using kirpich::OamEntry;
using kirpich::Settings;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::kListFirstRow;
using kirpich::systems::SettingsWiring;

constexpr std::size_t kTextCol = 3;

// The title screen's own cursor positions, and the row the port's bottom items sit on. Pinned here so
// a move shows up as a test change rather than silently.
constexpr std::uint8_t kCursorX1P     = 0x10;
constexpr std::uint8_t kCursorX2P     = 0x60;
constexpr std::uint8_t kSettingsAloneX = 0x30;
constexpr std::uint8_t kBottomRowY    = 123 + 16;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held    = actionSet(as);
}

// Open a screen at its init slot and press one action on it, through the dispatcher the way a frame
// does. Three ticks: the init runs and writes the loop state, an empty tick puts the loop handler
// live with nothing held, then the action arrives as a fresh press against that empty previous set.
void openAndPress(GameStateDispatcher& dispatcher, GameContext& game, GameState init,
                  Action action) {
    game.flow.gameState = init;
    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, actionSet({action}));
}

// The text a map row holds from `col`, decoded back through the charmap, trailing spaces dropped.
std::string_view rowText(const BackgroundMap& map, std::size_t row, std::size_t col,
                         std::size_t width, std::string_view candidate) {
    const auto glyphs = kirpich::encodeCharmapText(candidate);
    if (!glyphs || glyphs->size() > width) return {};
    for (std::size_t i = 0; i < glyphs->size(); ++i) {
        if (map[row][col + i] != static_cast<std::uint8_t>((*glyphs)[i])) return {};
    }
    return candidate;
}

// One frame of the title screen with the given press and the given setting.
void titleFrame(GameContext& game, bool showStats, std::initializer_list<Action> as) {
    press(game, as);
    kirpich::systems::titleScreen(game, {}, [showStats] { return showStats; });
}

// A title screen sitting at rest: the attract countdown is skipped so the input is what is under test.
GameContext titleContext(bool showStats) {
    GameContext game;
    kirpich::systems::initTitleScreen(game, [showStats] { return showStats; });
    game.flow.gameState = GameState::TITLE_SCREEN;
    game.flow.timer1    = 5;
    return game;
}

// Which object entries carry a word, as the glyphs of that word starting at a column.
bool wordDrawnAt(const GameContext& game, std::size_t col, std::string_view word) {
    const auto glyphs = kirpich::encodeCharmapText(word);
    if (!glyphs) return false;
    for (std::size_t i = 0; i < glyphs->size(); ++i) {
        const auto wantX = static_cast<std::uint8_t>((col + i) * 8 + 8);
        bool       found = false;
        for (const OamEntry& object : game.engine.oam) {
            if (object.y == kBottomRowY && object.x == wantX &&
                object.tile == static_cast<std::uint8_t>((*glyphs)[i])) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

// (1) The statistics toggle's screen moves Settings::showStats and no other flag, driven through the
// dispatcher the way a frame drives it. The install runs in an inner scope that then ends, so the
// option table's lifetime is exercised rather than assumed: the carousel borrows its table as a span
// and the handlers that read it outlive everything the install call had on its stack.
TEST(StatsScreens, TheToggleMovesTheShowStatsFlagAndNoOther) {
    Settings            settings;
    GameStateDispatcher dispatcher;
    int                 changed = 0;

    {
        const SettingsWiring settingsWiring{.settings = &settings};
        kirpich::systems::installStatsScreens(dispatcher, settings, [&changed] { ++changed; },
                                              settingsWiring);
    }

    GameContext game;
    openAndPress(dispatcher, game, GameState::INIT_STATS_SCREEN, Action::MenuRight);

    EXPECT_TRUE(settings.showStats);
    EXPECT_EQ(changed, 1) << "the host's seam did not fire";

    Settings onlyThisOne;
    onlyThisOne.showStats = true;
    EXPECT_EQ(settings, onlyThisOne) << "the toggle reached a flag that is not its own";

    // And off again, which is the other end of the same row.
    openAndPress(dispatcher, game, GameState::INIT_STATS_SCREEN, Action::MenuLeft);
    EXPECT_FALSE(settings.showStats);
    EXPECT_EQ(changed, 2);
}

// (2) The statistics screen IS the statistics rather than a menu on the way to them: its five rows
// are the whole game's totals, each game type's, and the achievements. Every one of them leads
// somewhere - the branch behind them says it is not built yet, which is honest, where a row that
// simply did nothing would be a dead end.
TEST(StatsScreens, EveryRowOfTheStatsScreenLeadsSomewhere) {
    Settings            settings;
    GameStateDispatcher dispatcher;
    kirpich::systems::installStatsScreens(dispatcher, settings, {}, SettingsWiring{});

    constexpr std::string_view kRows[] = {"all time", "mode a", "mode b", "mode c", "achievements"};

    for (std::size_t row = 0; row < std::size(kRows); ++row) {
        GameContext game;
        game.flow.gameState = GameState::INIT_STATS_MENU;
        dispatcher.tick(game, retropp::ActionSet{});
        ASSERT_EQ(game.flow.gameState, GameState::STATS_MENU);

        // Every row is on the screen, one to a line, in the order they are named.
        for (std::size_t i = 0; i < std::size(kRows); ++i) {
            EXPECT_EQ(rowText(game.display.map, kListFirstRow + i, kTextCol, 16, kRows[i]),
                      kRows[i])
                << "row " << i;
        }

        // Walk to this row and act on it.
        for (std::size_t i = 0; i < row; ++i) {
            dispatcher.tick(game, retropp::ActionSet{});
            dispatcher.tick(game, actionSet({Action::MenuDown}));
        }
        ASSERT_EQ(game.screens.listRow, row);

        dispatcher.tick(game, retropp::ActionSet{});
        dispatcher.tick(game, actionSet({Action::Confirm}));
        EXPECT_EQ(game.flow.gameState, GameState::INIT_STATS_PAGE)
            << kRows[row] << " did not open its branch";
        EXPECT_EQ(game.screens.statsBranch, row) << "the branch that opened is not the row taken";

        // The branch opens on its own first page, headed by what that page holds, and on its own
        // aggregate rather than on whatever the last branch was left showing.
        dispatcher.tick(game, retropp::ActionSet{});
        ASSERT_EQ(game.flow.gameState, GameState::STATS_PAGE);
        EXPECT_EQ(game.screens.statsLevel, kirpich::kStatAxisAll);
        EXPECT_EQ(game.screens.statsVariant, kirpich::kStatAxisAll);

        const auto branch = kirpich::systems::statsBranchOf(static_cast<std::uint8_t>(row));
        const std::string_view heading = kirpich::systems::statsPageTitle(branch, 0);
        const std::size_t      titleCol = (20 - heading.size()) / 2;
        EXPECT_EQ(rowText(game.display.map, kirpich::systems::kScreenTitleRow, titleCol, 20, heading),
                  heading)
            << "the branch's first page is not headed by what it holds";

        // And B comes back to the screen that opened it rather than leaving the tree.
        dispatcher.tick(game, retropp::ActionSet{});
        dispatcher.tick(game, actionSet({Action::Back}));
        EXPECT_EQ(game.flow.gameState, GameState::STATS_MENU);
    }
}

// (3) The statistics screen is still on the row the player left it on when they come back from a
// branch. The branch sits on top of it and is returned to without being re-initialised, so the two
// screens cannot share the fields that say where each of them is - a page turn inside a branch would
// otherwise walk the screen underneath it.
TEST(StatsScreens, TheScreenKeepsItsRowWhileABranchTurnsItsPages) {
    Settings            settings;
    GameStateDispatcher dispatcher;
    kirpich::systems::installStatsScreens(dispatcher, settings, {}, SettingsWiring{});

    GameContext game;
    game.flow.gameState = GameState::INIT_STATS_MENU;
    dispatcher.tick(game, retropp::ActionSet{});

    // Down to "mode c", which is the fourth row.
    for (std::size_t i = 0; i < 3; ++i) {
        dispatcher.tick(game, retropp::ActionSet{});
        dispatcher.tick(game, actionSet({Action::MenuDown}));
    }
    ASSERT_EQ(game.screens.listRow, 3u);

    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, actionSet({Action::Confirm}));
    dispatcher.tick(game, retropp::ActionSet{});
    ASSERT_EQ(game.flow.gameState, GameState::STATS_PAGE);

    // Walk the branch: down through its picker rows and on to its second page.
    for (std::size_t i = 0; i < 3; ++i) {
        dispatcher.tick(game, retropp::ActionSet{});
        dispatcher.tick(game, actionSet({Action::MenuDown}));
    }
    ASSERT_EQ(game.screens.statsPage, 1u);

    dispatcher.tick(game, retropp::ActionSet{});
    dispatcher.tick(game, actionSet({Action::Back}));
    ASSERT_EQ(game.flow.gameState, GameState::STATS_MENU);
    EXPECT_EQ(game.screens.listRow, 3u) << "the branch walked the screen underneath it";

    // And the screen is drawn with its cursor still on that row.
    dispatcher.tick(game, retropp::ActionSet{});
    EXPECT_EQ(game.display.map[kListFirstRow + 3][1],
              static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
}

// (4) With the statistics off, the title screen's bottom row is the settings item alone, in the place
// it has always occupied - centred, with its cursor one cell to its left. A player who never asks for
// the statistics gets the screen they have always had.
TEST(StatsScreens, TheBottomRowIsTheSettingsItemAloneWhileTheStatisticsAreOff) {
    GameContext game = titleContext(/*showStats=*/false);

    EXPECT_TRUE(wordDrawnAt(game, 6, "settings")) << "the settings item is not where it has been";
    EXPECT_FALSE(wordDrawnAt(game, 12, "stats")) << "there is no second item to draw";

    // Down reaches it, and its cursor is the single centred one.
    titleFrame(game, /*showStats=*/false, {Action::MenuDown});
    EXPECT_TRUE(game.screens.titleSettingsSelected);
    EXPECT_EQ(game.engine.oam[0].x, kSettingsAloneX);

    // Left and right do nothing on a row that holds one item, and Start opens the settings screen.
    titleFrame(game, /*showStats=*/false, {Action::MenuRight});
    EXPECT_EQ(game.engine.oam[0].x, kSettingsAloneX);
    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);

    titleFrame(game, /*showStats=*/false, {Action::Start});
    EXPECT_EQ(game.flow.gameState, GameState::INIT_SETTINGS);
}

// (4) With them on, the row holds both items under the two player-count columns, the cursor moves
// between them, and each one opens its own screen. The columns are the player options' own, so the
// items sit under what they belong to.
TEST(StatsScreens, TheBottomRowHoldsBothItemsUnderThePlayerColumns) {
    GameContext game = titleContext(/*showStats=*/true);

    EXPECT_TRUE(wordDrawnAt(game, 2, "settings")) << "under the one-player column";
    EXPECT_TRUE(wordDrawnAt(game, 12, "stats")) << "under the two-player column";

    titleFrame(game, /*showStats=*/true, {Action::MenuDown});
    ASSERT_TRUE(game.screens.titleSettingsSelected);
    EXPECT_FALSE(game.screens.titleStatsColumn) << "it opens on the left item";
    EXPECT_EQ(game.engine.oam[0].x, kCursorX1P);

    titleFrame(game, /*showStats=*/true, {Action::MenuRight});
    EXPECT_TRUE(game.screens.titleStatsColumn);
    EXPECT_EQ(game.engine.oam[0].x, kCursorX2P);

    // Right again is an end stop, as it is on the row above.
    titleFrame(game, /*showStats=*/true, {Action::MenuRight});
    EXPECT_TRUE(game.screens.titleStatsColumn);
    EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);

    // Start on the stats item opens the statistics, not the settings.
    titleFrame(game, /*showStats=*/true, {Action::Start});
    EXPECT_EQ(game.flow.gameState, GameState::INIT_STATS_MENU);
    EXPECT_EQ(game.screens.screenStackDepth, 1u) << "and records where to come back to";
    EXPECT_EQ(game.screens.screenStack[0], GameState::TITLE_SCREEN);

    // Back on the left item, Start opens the settings screen instead.
    GameContext left = titleContext(/*showStats=*/true);
    titleFrame(left, /*showStats=*/true, {Action::MenuDown});
    titleFrame(left, /*showStats=*/true, {Action::Start});
    EXPECT_EQ(left.flow.gameState, GameState::INIT_SETTINGS);
}

// (5) Moving up from the bottom row and back down leaves the player count where the player left it,
// as it does today - and leaves the bottom row's own column where they left that too. Neither row
// resets the other, which is what makes the two rows a grid rather than a single walk.
TEST(StatsScreens, LeavingARowAndComingBackLeavesBothChoicesAlone) {
    GameContext game = titleContext(/*showStats=*/true);

    // Two players on the top row, the stats item on the bottom one.
    titleFrame(game, /*showStats=*/true, {Action::MenuRight});
    ASSERT_TRUE(game.multiplayer.isMultiplayer);
    titleFrame(game, /*showStats=*/true, {Action::MenuDown});
    titleFrame(game, /*showStats=*/true, {Action::MenuRight});
    ASSERT_TRUE(game.screens.titleStatsColumn);

    // Up: the player count is untouched and the cursor goes back to where it was on that row.
    titleFrame(game, /*showStats=*/true, {Action::MenuUp});
    EXPECT_FALSE(game.screens.titleSettingsSelected);
    EXPECT_TRUE(game.multiplayer.isMultiplayer) << "moving rows changed the player count";
    EXPECT_EQ(game.engine.oam[0].x, kCursorX2P);

    // And down again: the bottom row is still on the item the player left it on.
    titleFrame(game, /*showStats=*/true, {Action::MenuDown});
    EXPECT_TRUE(game.screens.titleSettingsSelected);
    EXPECT_TRUE(game.screens.titleStatsColumn) << "moving rows changed the bottom row's column";
    EXPECT_EQ(game.engine.oam[0].x, kCursorX2P);

    // The two are independent: one player on the top row still leaves the stats item chosen below.
    titleFrame(game, /*showStats=*/true, {Action::MenuUp});
    titleFrame(game, /*showStats=*/true, {Action::MenuLeft});
    ASSERT_FALSE(game.multiplayer.isMultiplayer);
    titleFrame(game, /*showStats=*/true, {Action::MenuDown});
    EXPECT_TRUE(game.screens.titleStatsColumn);
}

}  // namespace
