// The list screen — behavioral tests over src/systems/list_screen.h, with the statistics chooser's
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

#include "render/settings_overlay.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/screen_ui_state.h"
#include "systems/game_context.h"
#include "systems/list_screen.h"
#include "systems/screen_stack.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::systems::GameContext;
using kirpich::systems::kListFirstRow;
using kirpich::systems::kListRows;
using kirpich::systems::ListWiring;

// The layout the screen draws, pinned here so a move shows up as a test change rather than silently.
constexpr std::size_t kTitleRow  = 2;
constexpr std::size_t kTextCol   = 3;
constexpr std::size_t kCursorCol = 1;
constexpr std::size_t kScreenRows = 18;
constexpr std::size_t kScreenCols = 20;

constexpr auto kSpace  = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kCursor = static_cast<std::uint8_t>(CharTile::HYPHEN);

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

// One painted row: which list row was asked for, and which map line it was told to write. The screen
// hands both to the caller and keeps neither, so recording the pair is how the seam is checked.
struct PaintCall {
    std::size_t row;
    std::size_t line;

    bool operator==(const PaintCall&) const = default;
};

// An instance whose length the test chooses and whose every call is recorded. Each row's text is the
// row's own index as two digits, so a row can be read straight out of the map - which is what lets
// the window's CONTENTS be asserted rather than only its bounds, and two digits rather than one so
// that row 1 and row 11 are not the same picture.
struct Probe {
    std::size_t            rows = 0;
    std::vector<PaintCall> painted;
    std::vector<std::size_t> chosen;
    int                    backs = 0;

    ListWiring wiring() {
        return ListWiring{
            .title = [] { return std::string_view{"list"}; },
            .count = [this] { return rows; },
            .paintRow =
                [this](BackgroundMap& map, std::size_t row, std::size_t line) {
                    painted.push_back({row, line});
                    const auto zero = static_cast<std::uint8_t>(CharTile::DIGIT_0);
                    map[line][kTextCol] = static_cast<std::uint8_t>(zero + (row / 10) % 10);
                    map[line][kTextCol + 1] = static_cast<std::uint8_t>(zero + row % 10);
                },
            .chose = [this](GameContext&, std::size_t row) { chosen.push_back(row); },
            .back  = [this](GameContext&) { ++backs; },
        };
    }
};

// A context sitting on the list, entered through the instance's own init slot.
void open(GameContext& game, const ListWiring& wiring) {
    kirpich::systems::initListScreen(game, wiring, GameState::STATS_MENU);
}

// One frame of the screen with the given press.
void step(GameContext& game, const ListWiring& wiring, std::initializer_list<Action> as) {
    game.audioCues = kirpich::systems::AudioCues{};
    press(game, as);
    kirpich::systems::listScreen(game, wiring);
}

// The row index a visible line is showing, read back out of its two digits.
std::size_t rowShownAt(const BackgroundMap& map, std::size_t offset) {
    const auto zero = static_cast<std::uint8_t>(CharTile::DIGIT_0);
    const auto tens = static_cast<std::size_t>(map[kListFirstRow + offset][kTextCol] - zero);
    const auto ones = static_cast<std::size_t>(map[kListFirstRow + offset][kTextCol + 1] - zero);
    return tens * 10 + ones;
}

// (1) The window shows the rows it should and the selection walks them, with end stops at both ends.
// A shorter list than the window fills only as many lines as it has, and leaves the rest empty.
TEST(ListScreen, SelectionWalksTheRowsAndStopsAtBothEnds) {
    GameContext game;
    Probe       probe;
    probe.rows        = 4;
    const auto wiring = probe.wiring();

    game.screens.listRow = 3;  // a stale selection from an earlier visit
    game.screens.listTop = 2;
    open(game, wiring);

    EXPECT_EQ(game.flow.gameState, GameState::STATS_MENU);
    EXPECT_EQ(game.screens.listRow, 0u) << "an instance opens at its first row";
    EXPECT_EQ(game.screens.listTop, 0u);
    EXPECT_EQ(game.screens.listCount, 4u) << "the screen records what it drew";

    const BackgroundMap& map = game.display.map;
    EXPECT_EQ(map[kListFirstRow][kCursorCol], kCursor);
    for (std::size_t offset = 0; offset < 4; ++offset) {
        EXPECT_EQ(rowShownAt(map, offset), offset) << "offset " << offset;
    }
    // A list shorter than the window leaves the lines it does not reach empty.
    for (std::size_t offset = 4; offset < kListRows; ++offset) {
        EXPECT_EQ(map[kListFirstRow + offset][kTextCol], kSpace) << "offset " << offset;
    }

    // Up at the first row is an end stop: nothing moves and nothing is said.
    step(game, wiring, {Action::MenuUp});
    EXPECT_EQ(game.screens.listRow, 0u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    for (std::uint8_t expected = 1; expected < 4; ++expected) {
        step(game, wiring, {Action::MenuDown});
        EXPECT_EQ(game.screens.listRow, expected);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
        EXPECT_EQ(map[kListFirstRow + expected][kCursorCol], kCursor);
        EXPECT_EQ(map[kListFirstRow][kCursorCol], kSpace) << "only one row carries the cursor";
    }

    // Down at the last row is the other end stop.
    step(game, wiring, {Action::MenuDown});
    EXPECT_EQ(game.screens.listRow, 3u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);
}

// (2) A list longer than the window scrolls, and the window FOLLOWS the selection rather than
// jumping: it moves by one row when the cursor would step off an edge, and not at all until then. So
// the rows a player was reading stay where they were, and the cursor sits at the window's edge rather
// than being re-centred.
TEST(ListScreen, TheWindowFollowsTheSelectionOneRowAtATime) {
    GameContext game;
    Probe       probe;
    probe.rows        = kListRows + 5;
    const auto wiring = probe.wiring();
    open(game, wiring);

    const BackgroundMap& map = game.display.map;

    // Walking down inside the window moves nothing.
    for (std::size_t expected = 1; expected < kListRows; ++expected) {
        step(game, wiring, {Action::MenuDown});
        EXPECT_EQ(game.screens.listRow, expected);
        EXPECT_EQ(game.screens.listTop, 0u) << "the window has not had to move yet";
        EXPECT_EQ(rowShownAt(map, expected), expected);
    }

    // The next step would leave the window, so it scrolls by exactly one row - and the cursor stays
    // on the window's last line.
    for (std::size_t past = 1; past <= 5; ++past) {
        step(game, wiring, {Action::MenuDown});
        EXPECT_EQ(game.screens.listRow, kListRows - 1 + past);
        EXPECT_EQ(game.screens.listTop, past) << "one row per step, not a page";
        EXPECT_EQ(map[kListFirstRow + kListRows - 1][kCursorCol], kCursor);
        EXPECT_EQ(rowShownAt(map, 0), past) << "the window's first row followed";
    }

    // And back up: the window holds until the cursor would leave it the other way, then follows.
    for (std::size_t offset = 1; offset < kListRows; ++offset) {
        step(game, wiring, {Action::MenuUp});
        EXPECT_EQ(game.screens.listTop, 5u) << "still inside the window";
    }
    step(game, wiring, {Action::MenuUp});
    EXPECT_EQ(game.screens.listRow, 4u);
    EXPECT_EQ(game.screens.listTop, 4u);
    EXPECT_EQ(map[kListFirstRow][kCursorCol], kCursor) << "the cursor sits on the window's first line";
}

// (3) The end indicators say where there is more list, and only there. They are sprites the render
// bridge draws from the screen's recorded window, so they are asked for as the frame would ask.
TEST(ListScreen, IndicatorsAppearOnlyWhereThereIsMoreList) {
    const kirpich::render::TileAtlas atlas{};

    const auto keys = [&](const kirpich::ScreenUiState& ui) {
        std::vector<std::string> names;
        for (const auto& sprite : kirpich::render::listArrows(ui, /*ramp=*/0, atlas)) {
            names.push_back(sprite.key.value);
        }
        return names;
    };

    // A list that fits the window draws neither.
    {
        GameContext game;
        Probe       probe;
        probe.rows        = kListRows;
        const auto wiring = probe.wiring();
        open(game, wiring);
        EXPECT_TRUE(keys(game.screens).empty());
    }

    // A longer one: at the top only the down arrow, in the middle both, at the bottom only the up.
    GameContext game;
    Probe       probe;
    probe.rows        = kListRows + 2;
    const auto wiring = probe.wiring();
    open(game, wiring);

    EXPECT_EQ(keys(game.screens), std::vector<std::string>{"list-down"});

    for (std::size_t i = 0; i < kListRows; ++i) step(game, wiring, {Action::MenuDown});
    ASSERT_EQ(game.screens.listTop, 1u);
    EXPECT_EQ(keys(game.screens), (std::vector<std::string>{"list-up", "list-down"}));

    step(game, wiring, {Action::MenuDown});
    ASSERT_EQ(game.screens.listRow, kListRows + 1);
    EXPECT_EQ(keys(game.screens), std::vector<std::string>{"list-up"});
}

// (4) The caller paints its own rows. The screen asks for exactly the rows in the window, at the
// lines it says, and asks for NO row outside it - which is what makes a sixty-row list cost eleven
// calls a frame rather than sixty.
TEST(ListScreen, TheScreenAsksForTheVisibleRowsAndNoOthers) {
    GameContext game;
    Probe       probe;
    probe.rows        = kListRows + 3;
    const auto wiring = probe.wiring();
    open(game, wiring);

    std::vector<PaintCall> expected;
    for (std::size_t offset = 0; offset < kListRows; ++offset) {
        expected.push_back({offset, kListFirstRow + offset});
    }
    EXPECT_EQ(probe.painted, expected);

    // Scrolled down by three, the window's rows shift and the lines do not.
    probe.painted.clear();
    for (std::size_t i = 0; i < kListRows + 2; ++i) step(game, wiring, {Action::MenuDown});
    ASSERT_EQ(game.screens.listTop, 3u);

    probe.painted.clear();
    step(game, wiring, {Action::MenuUp});  // one more frame, from a settled window
    std::vector<PaintCall> shifted;
    for (std::size_t offset = 0; offset < kListRows; ++offset) {
        shifted.push_back({3 + offset, kListFirstRow + offset});
    }
    EXPECT_EQ(probe.painted, shifted);
}

// (5) Acting on a row reports that row's index and nothing else; B goes back rather than choosing.
TEST(ListScreen, ChoosingReportsTheRowAndBackDoesNot) {
    GameContext game;
    Probe       probe;
    probe.rows        = 3;
    const auto wiring = probe.wiring();
    open(game, wiring);

    step(game, wiring, {Action::MenuDown});
    step(game, wiring, {Action::Confirm});
    EXPECT_EQ(probe.chosen, std::vector<std::size_t>{1});
    EXPECT_EQ(probe.backs, 0);

    // Start acts the same way, which is what the game's own selection screens do.
    step(game, wiring, {Action::Start});
    EXPECT_EQ(probe.chosen, (std::vector<std::size_t>{1, 1}));

    step(game, wiring, {Action::Back});
    EXPECT_EQ(probe.backs, 1);
    EXPECT_EQ(probe.chosen.size(), 2u) << "going back is not choosing";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
}

// (6) A wiring that names no back seam pops the navigation stack, which is what every list in the
// statistics tree wants and what keeps a list from being a dead end by default.
TEST(ListScreen, BackWithoutASeamPopsTheStack) {
    GameContext game;
    Probe       probe;
    probe.rows = 2;

    ListWiring wiring = probe.wiring();
    wiring.back       = {};

    game.flow.gameState = GameState::STATS_LIST;
    ASSERT_TRUE(kirpich::systems::pushScreen(game, GameState::STATS_MENU));
    open(game, wiring);

    step(game, wiring, {Action::Back});
    EXPECT_EQ(game.flow.gameState, GameState::STATS_LIST);
    EXPECT_EQ(game.screens.screenStackDepth, 0u);
}

// (7) The title is drawn on the row every screen in this family uses, so a list reads as a sibling of
// the screens the player walked to get here - and everything the screen did not write is empty.
TEST(ListScreen, TitleSitsOnTheSharedHeadingRowAndNothingElseIsWritten) {
    GameContext game;
    Probe       probe;
    probe.rows        = 2;
    const auto wiring = probe.wiring();
    open(game, wiring);

    EXPECT_EQ(kTitleRow, kirpich::systems::kScreenTitleRow);

    using C = CharTile;
    const BackgroundMap& map = game.display.map;
    // "list" is four cells, centred in twenty: columns 8-11.
    for (std::size_t i = 0; i < 4; ++i) {
        const CharTile expected[] = {C::LETTER_L, C::LETTER_I, C::LETTER_S, C::LETTER_T};
        EXPECT_EQ(map[kTitleRow][8 + i], static_cast<std::uint8_t>(expected[i])) << "col " << (8 + i);
    }

    const auto written = [](std::size_t row, std::size_t col) {
        if (row == kTitleRow) return col >= 8 && col < 12;
        // The probe writes each row's index as two digits from the text column.
        if (row == kListFirstRow) {
            return col == kCursorCol || col == kTextCol || col == kTextCol + 1;
        }
        if (row == kListFirstRow + 1) return col == kTextCol || col == kTextCol + 1;
        return false;
    };
    for (std::size_t row = 0; row < kScreenRows; ++row) {
        for (std::size_t col = 0; col < kScreenCols; ++col) {
            if (written(row, col)) continue;
            EXPECT_EQ(map[row][col], kSpace) << "row " << row << " col " << col;
        }
    }
}

}  // namespace
