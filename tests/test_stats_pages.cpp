// The statistics pages — behavioral tests over src/systems/stats_pages.h and the shapes the render
// bridge draws beside their counts (src/render/stats_pages.h).
//
// Device-free: the pages are pure logic over the game-state aggregate, and the sprites are asked for
// as the frame would ask. The screens are the port's own, so every asserted value comes from the
// surface's stated contract rather than from tetris.asm.

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
#include <kirpich/game_type.h>
#include <kirpich/piece_kind.h>

#include "data/charmap.h"
#include "render/stats_pages.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/screen_ui_state.h"
#include "state/stats_state.h"
#include "systems/game_context.h"
#include "systems/page_screen.h"
#include "systems/rising_floor.h"
#include "systems/screen_stack.h"
#include "systems/settings_screen.h"
#include "systems/stats.h"
#include "systems/stats_pages.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::StatSlice;
using kirpich::StatsState;
using kirpich::kStatAxisAll;
using kirpich::systems::GameContext;
using kirpich::systems::kStatsLabelCol;
using kirpich::systems::kStatsModeFirstLine;
using kirpich::systems::kStatsFirstLine;
using kirpich::systems::kStatsPickerFirstRow;
using kirpich::systems::kStatsPickerStride;
using kirpich::systems::kStatsValueEndCol;
using kirpich::systems::StatsBranch;

constexpr std::size_t kScreenCols = 20;

constexpr auto kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kZero  = static_cast<std::uint8_t>(CharTile::DIGIT_0);

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

std::size_t centred(std::size_t length) {
    return length >= kScreenCols ? 0 : (kScreenCols - length) / 2;
}

// Whether a row holds `text` from `col`, encoded the way every port screen writes it.
bool holdsText(const BackgroundMap& map, std::size_t row, std::size_t col, std::string_view text) {
    const auto glyphs = kirpich::encodeCharmapText(text);
    if (!glyphs) return false;
    for (std::size_t i = 0; i < glyphs->size(); ++i) {
        if (map[row][col + i] != static_cast<std::uint8_t>((*glyphs)[i])) return false;
    }
    return true;
}

// Whether a row holds `text` ending at the column every figure's value ends at.
bool holdsValue(const BackgroundMap& map, std::size_t row, std::string_view text) {
    return holdsText(map, row, kStatsValueEndCol + 1 - text.size(), text);
}

// The digits a number field holds, read back as characters. A blank cell reads as a space, which is
// what a leading zero is drawn as.
std::string digitsAt(const BackgroundMap& map, std::size_t row, std::size_t col,
                     std::size_t width) {
    std::string read;
    for (std::size_t i = 0; i < width; ++i) {
        const std::uint8_t cell = map[row][col + i];
        if (cell == kSpace) {
            read.push_back(' ');
        } else if (cell >= kZero && cell <= kZero + 9) {
            read.push_back(static_cast<char>('0' + (cell - kZero)));
        } else {
            read.push_back('?');
        }
    }
    return read;
}

// A figure's field: `digitPairs` pairs of digits ending at the shared value column.
std::string figureAt(const BackgroundMap& map, std::size_t row, std::size_t digitPairs) {
    const std::size_t width = 2 * digitPairs;
    return digitsAt(map, row, kStatsValueEndCol + 1 - width, width);
}

// One branch's page screen, driven the way a frame drives it.
struct Screen {
    kirpich::systems::PageWiring wiring = kirpich::systems::statsPageWiring();
    GameContext                  game;

    void open(StatsBranch branch) {
        // What the statistics screen's own row does on the way in: record the branch and start its
        // picker on the mode's own aggregate.
        game.screens.statsBranch    = static_cast<std::uint8_t>(branch);
        game.screens.statsLevel     = kStatAxisAll;
        game.screens.statsVariant   = kStatAxisAll;
        game.screens.statsPickerRow = 0;
        kirpich::systems::initPageScreen(game, wiring, GameState::STATS_PAGE);
    }

    void step(std::initializer_list<Action> as) {
        game.audioCues      = kirpich::systems::AudioCues{};
        game.joypad.pressed = actionSet(as);
        game.joypad.held    = actionSet(as);
        kirpich::systems::pageScreen(game, wiring);
    }

    [[nodiscard]] const BackgroundMap& map() const { return game.display.map; }
};

// A slice whose every field is distinct and derived from its position, so a figure printed under the
// wrong label cannot match by accident.
StatSlice sliceAt(std::uint32_t seed) {
    StatSlice slice{.rounds              = seed + 1,
                    .seconds             = seed + 2,
                    .longestRoundSeconds = seed + 3,
                    .drops               = seed + 4,
                    .score               = seed + 5,
                    .lines               = seed + 6,
                    .singles             = seed + 7,
                    .doubles             = seed + 8,
                    .triples             = seed + 9,
                    .tetrises            = seed + 10};
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        slice.pieces[kind] = seed + 11 + static_cast<std::uint32_t>(kind);
    }
    return slice;
}

// (1) Every branch offers its own pages, each page is named for what it holds, and the screen puts
// that name on the shared heading row.
TEST(StatsPages, EachBranchHasItsOwnPagesAndEachPageItsOwnHeading) {
    using kirpich::systems::statsPageCount;
    using kirpich::systems::statsPageTitle;

    EXPECT_EQ(statsPageCount(StatsBranch::ALL_TIME), 6u);
    EXPECT_EQ(statsPageCount(StatsBranch::MODE_A), 2u);
    EXPECT_EQ(statsPageCount(StatsBranch::MODE_B), 2u);
    EXPECT_EQ(statsPageCount(StatsBranch::MODE_C), 2u);
    EXPECT_EQ(statsPageCount(StatsBranch::ACHIEVEMENTS), 1u);

    constexpr std::string_view kAllTime[] = {"play time", "rounds",  "score",
                                             "clears",    "pieces",  "favourites"};
    for (std::size_t page = 0; page < std::size(kAllTime); ++page) {
        EXPECT_EQ(statsPageTitle(StatsBranch::ALL_TIME, page), kAllTime[page]) << "page " << page;
    }
    EXPECT_EQ(statsPageTitle(StatsBranch::MODE_B, 0), "figures");
    EXPECT_EQ(statsPageTitle(StatsBranch::MODE_B, 1), "pieces");
    EXPECT_EQ(statsPageTitle(StatsBranch::ACHIEVEMENTS, 0), "achievements");

    Screen screen;
    screen.open(StatsBranch::MODE_C);
    EXPECT_TRUE(holdsText(screen.map(), kirpich::systems::kScreenTitleRow, centred(7), "figures"));
}

// (2) Every figure lands under its own label on its own page. Seeded from one combination and read
// back cell by cell, this is the drift guard for the whole unit: four branches of pages is where a
// figure printed under the wrong label hides.
TEST(StatsPages, EveryFigureLandsUnderItsOwnLabel) {
    Screen screen;
    screen.game.stats.typeB[3][2] = sliceAt(1000);
    screen.open(StatsBranch::MODE_B);

    // Pick that one combination, so the page shows the slice and not the mode's total.
    screen.game.screens.statsLevel   = 3;
    screen.game.screens.statsVariant = 2;
    screen.step({});

    const BackgroundMap& map   = screen.map();
    const StatSlice      slice = sliceAt(1000);

    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 0, kStatsLabelCol, "rounds"));
    EXPECT_EQ(figureAt(map, kStatsModeFirstLine + 0, 3), "  1001");
    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 1, kStatsLabelCol, "played"));
    EXPECT_TRUE(holdsValue(map, kStatsModeFirstLine + 1,
                           kirpich::systems::formatDuration(slice.seconds).view()));
    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 2, kStatsLabelCol, "longest"));
    EXPECT_TRUE(holdsValue(map, kStatsModeFirstLine + 2,
                           kirpich::systems::formatDuration(slice.longestRoundSeconds).view()));
    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 3, kStatsLabelCol, "score"));
    EXPECT_EQ(figureAt(map, kStatsModeFirstLine + 3, 5), "      1005");
    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 4, kStatsLabelCol, "lines"));
    EXPECT_EQ(figureAt(map, kStatsModeFirstLine + 4, 3), "  1006");
    EXPECT_TRUE(holdsText(map, kStatsModeFirstLine + 5, kStatsLabelCol, "drops"));
    EXPECT_EQ(figureAt(map, kStatsModeFirstLine + 5, 3), "  1004");

    // The all-time branch's own pages, over the same seeded table.
    Screen all;
    all.game.stats.typeB[3][2] = sliceAt(1000);
    all.open(StatsBranch::ALL_TIME);

    EXPECT_TRUE(holdsText(all.map(), kStatsFirstLine + 0, kStatsLabelCol, "program"));
    EXPECT_TRUE(holdsText(all.map(), kStatsFirstLine + 3, kStatsLabelCol, "at"));
    EXPECT_TRUE(holdsValue(all.map(), kStatsFirstLine + 3, "b-3-2"))
        << "the longest round is not labelled with the combination it was played at";

    all.step({Action::MenuDown});
    EXPECT_TRUE(holdsText(all.map(), kStatsFirstLine + 0, kStatsLabelCol, "rounds"));
    EXPECT_EQ(figureAt(all.map(), kStatsFirstLine + 1, 3), "     0") << "mode a played nothing";
    EXPECT_EQ(figureAt(all.map(), kStatsFirstLine + 2, 3), "  1001") << "mode b played the round";

    all.step({Action::MenuDown});
    EXPECT_TRUE(holdsText(all.map(), kStatsFirstLine + 0, kStatsLabelCol, "score"));
    EXPECT_EQ(figureAt(all.map(), kStatsFirstLine + 0, 5), "      1005");

    all.step({Action::MenuDown});
    EXPECT_TRUE(holdsText(all.map(), kStatsFirstLine + 3, kStatsLabelCol, "tetrises"));
    EXPECT_EQ(figureAt(all.map(), kStatsFirstLine + 3, 3), "  1010");
}

// (3) A figure's value ends at the shared column whatever width it was asked for, so the numbers line
// up down the page and the label has whatever is left.
TEST(StatsPages, AFiguresValueEndsAtTheSharedColumn) {
    BackgroundMap map{};
    for (auto& row : map) row.fill(kSpace);

    kirpich::systems::statLine(map, 5, "one", 7, 1);
    kirpich::systems::statLine(map, 6, "three", 123456, 3);
    kirpich::systems::statLine(map, 7, "five", 1234567890, 5);

    EXPECT_EQ(digitsAt(map, 5, kStatsValueEndCol - 1, 2), " 7");
    EXPECT_EQ(digitsAt(map, 6, kStatsValueEndCol - 5, 6), "123456");
    EXPECT_EQ(digitsAt(map, 7, kStatsValueEndCol - 9, 10), "1234567890");

    // The cell before a field is the label's, and stays empty when the label does not reach it.
    EXPECT_EQ(map[5][kStatsValueEndCol - 2], kSpace);
    EXPECT_TRUE(holdsText(map, 7, kStatsLabelCol, "five"));

    // A value wider than its field draws what the printer draws - the low digits - rather than
    // running into the label.
    kirpich::systems::statLine(map, 8, "narrow", 123456, 2);
    EXPECT_EQ(digitsAt(map, 8, kStatsValueEndCol - 3, 4), "3456");
    EXPECT_EQ(map[8][kStatsValueEndCol - 4], kSpace);
}

// (4) The picker's rise row shows the interval the player picked, never the index it is stored at -
// a walk across all six catches a table read by index, which is invisible on the value that happens
// to equal its own position.
TEST(StatsPages, TheRiseRowShowsTheRiseTheirPlayerPicked) {
    Screen screen;
    screen.open(StatsBranch::MODE_C);

    // Down to the rise row, which is the second of Type C's two.
    screen.step({Action::MenuDown});
    ASSERT_EQ(screen.game.screens.statsPickerRow, 1u);
    EXPECT_TRUE(holdsText(screen.map(), kStatsPickerFirstRow + kStatsPickerStride, kStatsLabelCol,
                          "rise"));
    EXPECT_TRUE(holdsText(screen.map(), kStatsPickerFirstRow + kStatsPickerStride,
                          kirpich::systems::kOptionValueCol, "all"))
        << "a mode's picker opens on its own aggregate";

    for (std::size_t index = 0; index < kirpich::systems::kTypeCRiseChoiceCount; ++index) {
        screen.step({Action::MenuRight});
        ASSERT_EQ(screen.game.screens.statsVariant, index);
        const std::string expected =
            std::to_string(kirpich::systems::kTypeCRiseValues[index]);
        EXPECT_TRUE(holdsText(screen.map(), kStatsPickerFirstRow + kStatsPickerStride,
                              kirpich::systems::kOptionValueCol, expected))
            << "rise index " << index << " did not read as " << expected;
    }

    // The end of the range is an end stop, as every scroller in the game has.
    screen.step({Action::MenuRight});
    EXPECT_EQ(screen.game.screens.statsVariant, kirpich::systems::kTypeCRiseChoiceCount - 1);

    // Type B's second row is its start height, and shows the height itself.
    Screen typeB;
    typeB.open(StatsBranch::MODE_B);
    typeB.step({Action::MenuDown});
    EXPECT_TRUE(holdsText(typeB.map(), kStatsPickerFirstRow + kStatsPickerStride, kStatsLabelCol,
                          "height"));
    typeB.step({Action::MenuRight});
    typeB.step({Action::MenuRight});
    EXPECT_EQ(typeB.game.screens.statsVariant, 1u);
    EXPECT_TRUE(holdsText(typeB.map(), kStatsPickerFirstRow + kStatsPickerStride,
                          kirpich::systems::kOptionValueCol, "1"));

    // The same rule binds the longest round's own label: a Type C round is named by the rise it was
    // played at, and a Type A round carries two components where the other two carry three.
    Screen all;
    all.game.stats.typeC[7][4].rounds              = 1;
    all.game.stats.typeC[7][4].longestRoundSeconds = 90;
    all.open(StatsBranch::ALL_TIME);
    EXPECT_TRUE(holdsValue(all.map(), kStatsFirstLine + 3, "c-7-8"))
        << "the rise reads as its stored index rather than as the interval picked";

    Screen typeAOnly;
    typeAOnly.game.stats.typeA[5].rounds              = 1;
    typeAOnly.game.stats.typeA[5].longestRoundSeconds = 30;
    typeAOnly.open(StatsBranch::ALL_TIME);
    EXPECT_TRUE(holdsValue(typeAOnly.map(), kStatsFirstLine + 3, "a-5"));
}

// (5) The figures on display follow the picker. This is the whole of what the picker is for: moving
// it must change what is on the screen, and to the combination it now names.
TEST(StatsPages, TheFiguresFollowThePicker) {
    Screen screen;
    screen.game.stats.typeB[1][0] = sliceAt(2000);
    screen.game.stats.typeB[1][1] = sliceAt(5000);
    screen.open(StatsBranch::MODE_B);

    // Level 1, height 0.
    screen.game.screens.statsLevel   = 1;
    screen.game.screens.statsVariant = 0;
    screen.step({});
    EXPECT_EQ(figureAt(screen.map(), kStatsModeFirstLine + 3, 5), "      2005");

    // The height row, moved one to the right: a different combination, and different figures.
    screen.step({Action::MenuDown});
    ASSERT_EQ(screen.game.screens.statsPickerRow, 1u);
    screen.step({Action::MenuRight});
    ASSERT_EQ(screen.game.screens.statsVariant, 1u);
    EXPECT_EQ(figureAt(screen.map(), kStatsModeFirstLine + 3, 5), "      5005")
        << "the figures still belong to the combination the player just left";

    // And back to the level's own aggregate, which is both of them.
    screen.step({Action::MenuLeft});
    screen.step({Action::MenuLeft});
    ASSERT_EQ(screen.game.screens.statsVariant, kStatAxisAll);
    EXPECT_EQ(figureAt(screen.map(), kStatsModeFirstLine + 3, 5), "      7010");
}

// (6) The walk down the picker's rows and the turn to the next page are one continuous motion. Up
// from the first row of the first page is an end stop and NOT a way out: B is what leaves a screen,
// everywhere in the game. A mode with one picker row turns the page at its own last row, which is
// its first.
TEST(StatsPages, TheWalkAndThePageTurnAreOneMotion) {
    Screen typeB;
    typeB.game.flow.gameState = GameState::STATS_MENU;
    ASSERT_TRUE(kirpich::systems::pushScreen(typeB.game, GameState::INIT_STATS_PAGE));
    typeB.open(StatsBranch::MODE_B);

    typeB.step({Action::MenuDown});
    EXPECT_EQ(typeB.game.screens.statsPickerRow, 1u);
    EXPECT_EQ(typeB.game.screens.statsPage, 0u);

    typeB.step({Action::MenuDown});
    EXPECT_EQ(typeB.game.screens.statsPage, 1u) << "past the last picker row turns the page";
    EXPECT_EQ(typeB.game.screens.statsPickerRow, 0u) << "and enters the page at its first row";

    // Back up: the rows again, then back to the page before, entering it at its last row.
    typeB.step({Action::MenuDown});
    ASSERT_EQ(typeB.game.screens.statsPickerRow, 1u);
    typeB.step({Action::MenuUp});
    ASSERT_EQ(typeB.game.screens.statsPickerRow, 0u);
    typeB.step({Action::MenuUp});
    EXPECT_EQ(typeB.game.screens.statsPage, 0u);
    EXPECT_EQ(typeB.game.screens.statsPickerRow, 1u);

    // The top of the first page holds. Nothing else in the game backs out on up, and this does not
    // either.
    typeB.step({Action::MenuUp});
    ASSERT_EQ(typeB.game.screens.statsPickerRow, 0u);
    typeB.step({Action::MenuUp});
    EXPECT_EQ(typeB.game.flow.gameState, GameState::STATS_PAGE) << "up must not leave the screen";
    EXPECT_EQ(typeB.game.screens.statsPickerRow, 0u);
    EXPECT_EQ(typeB.game.screens.statsPage, 0u);
    EXPECT_EQ(typeB.game.screens.screenStackDepth, 1u);
    EXPECT_EQ(typeB.game.audioCues.square, kirpich::SquareSfxId::NONE);

    // B is what leaves it, and the objects the picker placed go with it - the screen underneath is
    // returned to at its loop slot, so nothing there would clear them.
    typeB.game.engine.oam[0].x = 0x40;  // an arrow the picker left standing
    typeB.step({Action::Back});
    EXPECT_EQ(typeB.game.flow.gameState, GameState::STATS_MENU);
    EXPECT_EQ(typeB.game.screens.screenStackDepth, 0u);
    EXPECT_EQ(typeB.game.engine.oam, kirpich::EngineState{}.oam)
        << "the picker's arrows are still on the screen underneath";

    // Type A is picked by level alone, so its one row is also its last.
    Screen typeA;
    typeA.open(StatsBranch::MODE_A);
    typeA.step({Action::MenuDown});
    EXPECT_EQ(typeA.game.screens.statsPage, 1u);
    typeA.step({Action::MenuDown});
    EXPECT_EQ(typeA.game.screens.statsPage, 1u) << "the last row of the last page is an end stop";
}

// (7) Both axes folded is exactly the game type's own total. The two folds are asserted against each
// other rather than each against a hand-computed number, because a number they both got wrong would
// pass.
TEST(StatsPages, FoldingBothAxesGivesTheGameTypesOwnTotal) {
    StatsState    stats;
    std::uint32_t seed = 100;
    for (std::size_t level = 0; level < kirpich::kStatLevels; ++level) {
        stats.typeA[level] = sliceAt(seed);
        seed += 100;
        for (std::size_t variant = 0; variant < kirpich::kStatVariants; ++variant) {
            stats.typeB[level][variant] = sliceAt(seed);
            seed += 100;
            stats.typeC[level][variant] = sliceAt(seed);
            seed += 100;
        }
    }

    for (const GameType type : {GameType::TYPE_A, GameType::TYPE_B, GameType::TYPE_C}) {
        const kirpich::systems::StatSelection everything{
            .type = type, .level = kStatAxisAll, .variant = kStatAxisAll};
        EXPECT_EQ(kirpich::systems::totalsForSelection(stats, everything),
                  kirpich::systems::totalsFor(stats, type));
    }

    // One level across its variants: the six slices of that level and no others.
    StatSlice byHand;
    for (std::size_t variant = 0; variant < kirpich::kStatVariants; ++variant) {
        byHand.rounds += stats.typeB[4][variant].rounds;
        byHand.score += stats.typeB[4][variant].score;
    }
    const kirpich::systems::StatSelection oneLevel{
        .type = GameType::TYPE_B, .level = 4, .variant = kStatAxisAll};
    const StatSlice folded = kirpich::systems::totalsForSelection(stats, oneLevel);
    EXPECT_EQ(folded.rounds, byHand.rounds);
    EXPECT_EQ(folded.score, byHand.score);
}

// (8) The pieces page shows the selection's seven counts, in PieceKind order and in their own slots,
// and they change with the picker as the figures do.
TEST(StatsPages, ThePiecesPageShowsTheSelectionsSevenCounts) {
    Screen screen;
    screen.game.stats.typeB[2][3] = sliceAt(3000);
    screen.open(StatsBranch::MODE_B);

    screen.game.screens.statsLevel   = 2;
    screen.game.screens.statsVariant = 3;
    screen.game.screens.statsPage    = 1;
    screen.step({});

    const StatSlice slice = sliceAt(3000);
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        const auto slot = kirpich::systems::statsPieceSlot(kind);
        const auto line = kirpich::systems::statsPieceCountLine(slot.row, /*underPicker=*/true);
        const std::string expected = "  " + std::to_string(slice.pieces[kind]);
        EXPECT_EQ(digitsAt(screen.map(), line, kirpich::systems::kStatsPieceCols[slot.column], 6),
                  expected)
            << "shape " << kind;
    }

    // A different combination, and the seven counts follow it.
    screen.game.screens.statsVariant = 4;
    screen.step({});
    const auto slot = kirpich::systems::statsPieceSlot(0);
    EXPECT_EQ(digitsAt(screen.map(), kirpich::systems::statsPieceCountLine(slot.row, true),
                       kirpich::systems::kStatsPieceCols[slot.column], 6),
              "     0");
}

// (9) A fold with nothing behind it says so rather than showing a first slot as though it were a real
// answer, and the achievements branch is honest about being unbuilt.
TEST(StatsPages, NothingPlayedSaysSoAndAchievementsSayTheyAreNotBuilt) {
    Screen screen;
    screen.open(StatsBranch::ALL_TIME);
    screen.game.screens.statsPage = 5;
    screen.step({});

    EXPECT_TRUE(holdsText(screen.map(), kStatsFirstLine + 0, kStatsLabelCol, "mode"));
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 0, "none"));
    EXPECT_TRUE(holdsText(screen.map(), kStatsFirstLine + 1, kStatsLabelCol, "music"));
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 1, "none"));
    EXPECT_TRUE(holdsText(screen.map(), kStatsFirstLine + 2, kStatsLabelCol, "level"));
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 2, "none"));

    // And with something played, the three name what was played.
    screen.game.stats.typeC[7][0].rounds = 5;
    screen.game.stats.musicRounds[1]     = 3;
    screen.step({});
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 0, "c"));
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 1, "b"));
    EXPECT_TRUE(holdsValue(screen.map(), kStatsFirstLine + 2, "7"));

    Screen achievements;
    achievements.open(StatsBranch::ACHIEVEMENTS);
    EXPECT_TRUE(holdsText(achievements.map(), kStatsFirstLine, kStatsLabelCol, "not built yet"));
    EXPECT_EQ(achievements.game.screens.statsPageCount, 1u);
}

// (10) The shapes are drawn on a pieces page and nowhere else, swept over every state the byte can
// hold and over every branch and page.
TEST(StatsPages, TheShapesAreDrawnOnAPiecesPageAndNowhereElse) {
    kirpich::ScreenUiState ui;

    for (unsigned raw = 0; raw <= 0xFF; ++raw) {
        const auto state = static_cast<GameState>(raw);
        for (std::uint8_t branch = 0; branch < kirpich::systems::kStatsBranchCount; ++branch) {
            const auto        which = kirpich::systems::statsBranchOf(branch);
            const std::size_t pages = kirpich::systems::statsPageCount(which);
            for (std::size_t page = 0; page < pages; ++page) {
                ui.statsBranch = branch;
                ui.statsPage   = static_cast<std::uint8_t>(page);

                const bool isPiecesPage = kirpich::systems::statsPageIsPieces(which, page);
                const bool expected = state == GameState::STATS_PAGE && isPiecesPage;
                EXPECT_EQ(kirpich::render::statsPieceShapesShown(state, ui), expected)
                    << "state " << raw << " branch " << int{branch} << " page " << page;
            }
        }
    }
}

// (11) No page writes into the last cell of a row. A figure that ran to the edge of the display read
// as though it had been cut off there, and left the page with a margin down one side and none down
// the other; this walks every page of every branch over a full table and holds the margin.
TEST(StatsPages, NoPageWritesIntoTheLastColumn) {
    StatsState    stats;
    std::uint32_t seed = 100000;
    for (std::size_t level = 0; level < kirpich::kStatLevels; ++level) {
        stats.typeA[level] = sliceAt(seed);
        seed += 1000;
        for (std::size_t variant = 0; variant < kirpich::kStatVariants; ++variant) {
            stats.typeB[level][variant] = sliceAt(seed);
            stats.typeC[level][variant] = sliceAt(seed + 500);
            seed += 1000;
        }
    }
    stats.applicationSeconds = 3'600'000;
    stats.musicRounds[2]     = 99;

    for (std::uint8_t branch = 0; branch < kirpich::systems::kStatsBranchCount; ++branch) {
        const auto        which = kirpich::systems::statsBranchOf(branch);
        const std::size_t pages = kirpich::systems::statsPageCount(which);
        for (std::size_t page = 0; page < pages; ++page) {
            Screen screen;
            screen.game.stats = stats;
            screen.open(which);
            screen.game.screens.statsPage = static_cast<std::uint8_t>(page);
            screen.step({});

            for (std::size_t row = 0; row < 18; ++row) {
                EXPECT_EQ(screen.map()[row][kScreenCols - 1], kSpace)
                    << "branch " << int{branch} << " page " << page << " row " << row;
            }
        }
    }
}

// (12) The seven shapes sit three to a row with a clear row between them, each over its own count,
// and the lowest row the grid writes clears the down arrow's. A shape is two cells tall, so a grid
// without that clear row reads as one shape rather than seven.
TEST(StatsPages, TheShapesAreSpacedThreeToARowAndClearTheArrowRow) {
    using kirpich::render::statsPieceShapeOrigin;
    using kirpich::systems::kStatsPieceBlockRows;
    using kirpich::systems::kStatsPieceCols;
    using kirpich::systems::kStatsPieceGridCols;
    using kirpich::systems::statsPieceCountLine;
    using kirpich::systems::statsPieceLine;
    using kirpich::systems::statsPieceSlot;

    constexpr int         kCell        = 8;
    constexpr std::size_t kShapeHeight = 2;  // cells, at the spawn orientation

    for (const bool underPicker : {false, true}) {
        for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
            const auto slot = statsPieceSlot(kind);
            EXPECT_EQ(slot.column, kind % kStatsPieceGridCols) << "shape " << kind;
            EXPECT_EQ(slot.row, kind / kStatsPieceGridCols) << "shape " << kind;

            const auto origin = statsPieceShapeOrigin(kind, underPicker);
            EXPECT_EQ(origin.x, static_cast<int>(kStatsPieceCols[slot.column]) * kCell +
                                    kirpich::render::kStatsShapeXOffset);
            EXPECT_EQ(origin.y, static_cast<int>(statsPieceLine(slot.row, underPicker)) * kCell +
                                    kirpich::render::kStatsShapeYOffset);

            // The count sits under its own shape, on the row the shape's lower half stops short of.
            EXPECT_EQ(statsPieceCountLine(slot.row, underPicker),
                      statsPieceLine(slot.row, underPicker) + kShapeHeight)
                << "shape " << kind;
            EXPECT_LT(statsPieceCountLine(slot.row, underPicker),
                      kirpich::systems::kPageDownArrowRow)
                << "shape " << kind;
        }

        // A block is the shape, its count, and nothing else - so the next shape starts a clear row
        // below the count above it.
        EXPECT_EQ(statsPieceLine(1, underPicker) - statsPieceLine(0, underPicker),
                  kStatsPieceBlockRows);
        EXPECT_GT(statsPieceLine(1, underPicker), statsPieceCountLine(0, underPicker))
            << "a shape starts on the row its neighbour's count is written on";
    }

    // The columns are clear of one another and of the last cell of a row: a six-cell column holds a
    // count, and the widest shape takes four of the six.
    for (std::size_t column = 0; column + 1 < kStatsPieceGridCols; ++column) {
        EXPECT_GE(kStatsPieceCols[column + 1], kStatsPieceCols[column] + 6) << "column " << column;
    }
    EXPECT_LT(kStatsPieceCols[kStatsPieceGridCols - 1] + 6, kScreenCols);
}

}  // namespace
