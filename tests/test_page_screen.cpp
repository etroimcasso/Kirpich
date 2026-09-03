// The paged screen — behavioral tests over src/systems/page_screen.h, with the statistics branch's
// dispatch slots standing in for any instance's.
//
// Device-free: the handlers are pure logic over the game-state aggregate and the wiring's seams. The
// screen is the port's own, so every asserted value comes from the surface's stated contract rather
// than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "retropp/input.h"
#include "state/display_state.h"
#include "systems/game_context.h"
#include "systems/page_screen.h"
#include "systems/screen_stack.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::systems::GameContext;
using kirpich::systems::PageWiring;

// The layout the screen draws, pinned here so a move shows up as a test change rather than silently.
constexpr std::size_t kTitleRow   = 2;
constexpr std::size_t kMarkerCol  = 3;
constexpr std::size_t kMarkerRow  = 5;
constexpr std::size_t kScreenRows = 18;
constexpr std::size_t kScreenCols = 20;

constexpr auto kSpace = static_cast<std::uint8_t>(CharTile::SPACE);

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

// An instance whose length the test chooses and whose every call is recorded.
//
// Each page paints one cell on a line of its own - page n at row kMarkerRow + n - so a repaint can be
// read out of the map: after a turn the page just left must have taken its own cell with it, which is
// what makes "a different page is a different screen" an assertion rather than a claim.
struct Probe {
    std::size_t              pages = 0;
    std::vector<std::size_t> painted;
    std::vector<std::string> titled;
    int                      backs = 0;

    // The vertical steps the page was offered, and whether it says it took them.
    std::vector<int> walked;
    bool             consumeWalk = false;

    // The horizontal steps, which the screen has no meaning of its own for.
    std::vector<int> adjusted;

    // Each page's own name, held so a string_view handed back stays valid.
    std::vector<std::string> names;

    explicit Probe(std::size_t count) : pages(count) {
        for (std::size_t page = 0; page < count; ++page) {
            names.push_back("p" + std::to_string(page));
        }
    }

    PageWiring wiring() {
        return PageWiring{
            .count = [this](const GameContext&) { return pages; },
            .title =
                [this](const GameContext&, std::size_t page) {
                    titled.push_back(names[page]);
                    return std::string_view{names[page]};
                },
            .paintPage =
                [this](GameContext& game, std::size_t page) {
                    painted.push_back(page);
                    game.display.displayedMap()[kMarkerRow + page][kMarkerCol] =
                        static_cast<std::uint8_t>(CharTile::LETTER_X);
                },
            .back = [this](GameContext&) { ++backs; },
            .walk =
                [this](GameContext&, std::size_t, int delta) {
                    walked.push_back(delta);
                    return consumeWalk;
                },
            .adjust = [this](GameContext&, std::size_t,
                             int delta) { adjusted.push_back(delta); },
        };
    }
};

void open(GameContext& game, const PageWiring& wiring) {
    kirpich::systems::initPageScreen(game, wiring, GameState::STATS_PAGE);
}

void step(GameContext& game, const PageWiring& wiring, std::initializer_list<Action> as) {
    game.audioCues = kirpich::systems::AudioCues{};
    press(game, as);
    kirpich::systems::pageScreen(game, wiring);
}

// (1) The instance opens on its first page whatever the last one was left showing, paints it, records
// how many pages there are for the render bridge, and hands control to its loop slot.
TEST(PageScreen, TheInitOpensOnTheFirstPageAndRecordsTheCount) {
    GameContext game;
    Probe       probe{4};
    const auto  wiring = probe.wiring();

    game.screens.statsPage      = 3;  // a stale page from an earlier visit
    game.screens.statsPageCount = 9;
    open(game, wiring);

    EXPECT_EQ(game.flow.gameState, GameState::STATS_PAGE);
    EXPECT_EQ(game.screens.statsPage, 0u) << "an instance opens on its first page";
    EXPECT_EQ(game.screens.statsPageCount, 4u) << "the screen records what it drew";
    EXPECT_EQ(probe.painted, std::vector<std::size_t>{0});
    EXPECT_EQ(game.display.map[kMarkerRow][kMarkerCol],
              static_cast<std::uint8_t>(CharTile::LETTER_X));
}

// (2) Up and down turn pages, with an end stop at each end that moves nothing and says nothing.
TEST(PageScreen, PagesTurnBothWaysAndStopAtBothEnds) {
    GameContext game;
    Probe       probe{3};
    const auto  wiring = probe.wiring();
    open(game, wiring);

    // Up on the first page is an end stop.
    step(game, wiring, {Action::MenuUp});
    EXPECT_EQ(game.screens.statsPage, 0u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    for (std::uint8_t expected = 1; expected < 3; ++expected) {
        step(game, wiring, {Action::MenuDown});
        EXPECT_EQ(game.screens.statsPage, expected);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    }

    // Down on the last page is the other end stop.
    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(game.screens.statsPage, 2u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    step(game, wiring, {Action::MenuUp});
    EXPECT_EQ(game.screens.statsPage, 1u);
}

// (3) A turn repaints the whole screen rather than adding to it: the cell the page just left painted
// is gone, because a different page is a different screen.
TEST(PageScreen, ATurnRepaintsTheWholeScreen) {
    GameContext game;
    Probe       probe{2};
    const auto  wiring = probe.wiring();
    open(game, wiring);

    const BackgroundMap& map = game.display.map;
    ASSERT_EQ(map[kMarkerRow][kMarkerCol], static_cast<std::uint8_t>(CharTile::LETTER_X));

    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(map[kMarkerRow][kMarkerCol], kSpace) << "the page just left is still on the screen";
    EXPECT_EQ(map[kMarkerRow + 1][kMarkerCol], static_cast<std::uint8_t>(CharTile::LETTER_X));
}

// (4) The heading is the page's own name, on the row every screen in this family uses, and everything
// the screen and its page did not write is empty.
TEST(PageScreen, TheHeadingNamesTheCurrentPageAndNothingElseIsWritten) {
    GameContext game;
    Probe       probe{3};
    const auto  wiring = probe.wiring();
    open(game, wiring);

    EXPECT_EQ(kTitleRow, kirpich::systems::kScreenTitleRow);

    using C = CharTile;
    const BackgroundMap& map = game.display.map;

    // "p0" is two cells, centred in twenty: columns 9-10.
    EXPECT_EQ(map[kTitleRow][9], static_cast<std::uint8_t>(C::LETTER_P));
    EXPECT_EQ(map[kTitleRow][10], static_cast<std::uint8_t>(C::DIGIT_0));

    const auto written = [](std::size_t row, std::size_t col) {
        if (row == kTitleRow) return col == 9 || col == 10;
        return row == kMarkerRow && col == kMarkerCol;
    };
    for (std::size_t row = 0; row < kScreenRows; ++row) {
        for (std::size_t col = 0; col < kScreenCols; ++col) {
            if (written(row, col)) continue;
            EXPECT_EQ(map[row][col], kSpace) << "row " << row << " col " << col;
        }
    }

    // And the heading follows the page rather than naming the branch.
    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(map[kTitleRow][10], static_cast<std::uint8_t>(C::DIGIT_1));
}

// (5) The paint seam is handed exactly the page on display and no other, once a frame.
TEST(PageScreen, ThePaintSeamIsHandedTheCurrentPageAndNoOther) {
    GameContext game;
    Probe       probe{4};
    const auto  wiring = probe.wiring();
    open(game, wiring);

    probe.painted.clear();
    step(game, wiring, {});
    EXPECT_EQ(probe.painted, std::vector<std::size_t>{0});

    probe.painted.clear();
    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(probe.painted, std::vector<std::size_t>{1});

    probe.painted.clear();
    step(game, wiring, {Action::MenuDown});
    step(game, wiring, {});
    EXPECT_EQ(probe.painted, (std::vector<std::size_t>{2, 2}));
}

// (6) B goes back through the seam when there is one, and pops the navigation stack when there is
// not - which is what every branch of the statistics tree wants and what keeps a page from being a
// dead end by default.
TEST(PageScreen, BackUsesTheSeamOrPopsTheStack) {
    {
        GameContext game;
        Probe       probe{2};
        const auto  wiring = probe.wiring();
        open(game, wiring);

        step(game, wiring, {Action::Back});
        EXPECT_EQ(probe.backs, 1);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    }

    GameContext game;
    Probe       probe{2};
    PageWiring  wiring = probe.wiring();
    wiring.back        = {};

    game.flow.gameState = GameState::STATS_PAGE;
    ASSERT_TRUE(kirpich::systems::pushScreen(game, GameState::STATS_MENU));
    open(game, wiring);

    step(game, wiring, {Action::Back});
    EXPECT_EQ(game.flow.gameState, GameState::STATS_PAGE);
    EXPECT_EQ(game.screens.screenStackDepth, 0u);
    EXPECT_EQ(probe.backs, 0);
}

// (7) A page that owns rows takes the vertical step first, and only a step it did not take turns the
// page. Left and right reach the page too, and the screen has no meaning of its own for them.
TEST(PageScreen, ThePageIsOfferedTheStepBeforeThePageTurns) {
    GameContext game;
    Probe       probe{3};
    probe.consumeWalk = true;
    const auto wiring = probe.wiring();
    open(game, wiring);

    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(probe.walked, std::vector<int>{+1});
    EXPECT_EQ(game.screens.statsPage, 0u) << "a step the page took must not also turn the page";

    step(game, wiring, {Action::MenuUp});
    EXPECT_EQ(probe.walked, (std::vector<int>{+1, -1}));
    EXPECT_EQ(game.screens.statsPage, 0u);

    // A page that declines the step lets it through.
    probe.consumeWalk = false;
    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(game.screens.statsPage, 1u);

    step(game, wiring, {Action::MenuLeft});
    step(game, wiring, {Action::MenuRight});
    EXPECT_EQ(probe.adjusted, (std::vector<int>{-1, +1}));
    EXPECT_EQ(game.screens.statsPage, 1u) << "left and right are the page's, not the screen's";
}

}  // namespace
