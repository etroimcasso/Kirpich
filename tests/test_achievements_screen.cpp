// The achievements screen — behavioral tests over its logic (src/systems/achievements_screen.h) and
// its components (src/render/achievements/).
//
// Device-free. The logic is pure state over the game aggregate, and the components are pure functions
// returning primitives, so the atlas here is a plain value with distinguishable handles rather than
// anything uploaded — the same way the background bridge is tested. The screen is the port's own, so
// every asserted value comes from its stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <variant>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/achievements/badge.h"
#include "render/achievements/badge_grid.h"
#include "render/achievements/badge_panel.h"
#include "render/achievements/layout.h"
#include "render/achievements/screen.h"
#include "render/glyphs.h"
#include "retropp/input.h"
#include "systems/achievements.h"
#include "systems/achievements_screen.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/list_screen.h"
#include "systems/screen_stack.h"
#include "systems/settings_screen.h"
#include "systems/stats_pages.h"
#include "systems/stats_screens.h"

namespace {

using kirpich::Action;
using kirpich::AchievementId;
using kirpich::GameState;
using kirpich::render::TileAtlas;
using kirpich::systems::AchievementSection;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::kAchievementSectionCount;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// A shade ramp the player might have chosen, distinct from the greyscale a locked badge falls back to.
constexpr std::uint8_t kPlayerRamp = 7;

// An atlas whose handles say which sheet and which ramp a resolved tile came from, so a palette
// choice is readable in a test without a device.
TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
    };
    for (std::size_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
        const auto id = [ramp](int kind) {
            return static_cast<retropp::PaletteId>(1000 * kind + static_cast<int>(ramp));
        };
        atlas.palettes[ramp].font       = id(1);
        atlas.palettes[ramp].content    = id(2);
        atlas.palettes[ramp].fontDim    = id(3);
        atlas.palettes[ramp].fontSprite = id(4);
        atlas.palettes[ramp].sprite0    = id(5);
        atlas.palettes[ramp].sprite1    = id(6);
    }
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

// A screen sitting on its own state, ready to take a press.
GameContext openScreen(GameStateDispatcher& dispatcher) {
    GameContext game;
    game.flow.gameState = GameState::INIT_ACHIEVEMENTS;
    dispatcher.tick(game, retropp::ActionSet{});
    return game;
}

void press(GameStateDispatcher& dispatcher, GameContext& game, Action action) {
    dispatcher.tick(game, retropp::ActionSet{});  // a released frame, so the next one is an edge
    dispatcher.tick(game, actionSet({action}));
}

std::span<const retropp::Sprite> spritesOf(const retropp::DrawLayer& layer) {
    return std::get<retropp::SpriteContent>(layer.content).sprites;
}

std::size_t sectionSize(AchievementSection section) {
    return kirpich::systems::achievementSectionBadgeCount(section);
}

// The last section, and its last badge - the far end of the whole walk.
AchievementSection lastSection() {
    return kirpich::systems::achievementSectionAt(
        static_cast<std::uint8_t>(kAchievementSectionCount - 1));
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

TEST(AchievementsScreen, OpensOnTheFirstSectionAtItsFirstBadge) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);

    GameContext game = openScreen(dispatcher);

    EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENTS);
    EXPECT_EQ(game.achievementScreen.section, 0);
    EXPECT_EQ(game.achievementScreen.cursor, 0);
    EXPECT_FALSE(game.achievementScreen.open) << "nothing is open until a badge is chosen";
}

TEST(AchievementsScreen, TheCursorWalksItsRowAndStopsAtBothEnds) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    press(dispatcher, game, Action::MenuLeft);
    EXPECT_EQ(game.achievementScreen.cursor, 0) << "left from the first badge stays put";

    press(dispatcher, game, Action::MenuRight);
    EXPECT_EQ(game.achievementScreen.cursor, 1);
    press(dispatcher, game, Action::MenuRight);
    EXPECT_EQ(game.achievementScreen.cursor, 2);

    // The row is as wide as the grid, so the third badge is its end.
    press(dispatcher, game, Action::MenuRight);
    EXPECT_EQ(game.achievementScreen.cursor, 2) << "right stops at the row's end rather than wrapping";

    press(dispatcher, game, Action::MenuLeft);
    EXPECT_EQ(game.achievementScreen.cursor, 1);
}

TEST(AchievementsScreen, TheCursorWalksBetweenTheRowsOfASection) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    ASSERT_GT(sectionSize(AchievementSection::ASCENT), kirpich::systems::kAchievementGridCols)
        << "this case needs a section with a second row";

    press(dispatcher, game, Action::MenuDown);
    EXPECT_EQ(game.achievementScreen.cursor, kirpich::systems::kAchievementGridCols);
    EXPECT_EQ(game.achievementScreen.section, 0) << "a row within the section is not a section turn";

    press(dispatcher, game, Action::MenuUp);
    EXPECT_EQ(game.achievementScreen.cursor, 0);
    EXPECT_EQ(game.achievementScreen.section, 0);
}

TEST(AchievementsScreen, WalkingOffTheBottomTurnsToTheNextSection) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    // Walk down until the section changes: off the last row is the turn.
    const std::uint8_t from = game.achievementScreen.section;
    for (int step = 0; step < 8 && game.achievementScreen.section == from; ++step) {
        press(dispatcher, game, Action::MenuDown);
    }

    EXPECT_EQ(game.achievementScreen.section, from + 1) << "down off the last row turns the page";
    EXPECT_EQ(game.achievementScreen.cursor, 0) << "and lands on the first badge of the next section";
}

TEST(AchievementsScreen, WalkingOffTheTopKeepsItsColumnInTheRowAbove) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    constexpr std::size_t kCols = kirpich::systems::kAchievementGridCols;

    // A column that is neither the first nor the last badge of the row it lands in. The last would
    // be indistinguishable from landing on the section's final badge, which is the behaviour this
    // case exists to rule out.
    constexpr std::size_t kColumn = 1;

    const std::size_t above   = sectionSize(AchievementSection::ASCENT);
    const std::size_t lastRow = (above - 1) / kCols;
    ASSERT_LT(lastRow * kCols + kColumn, above - 1)
        << "this case needs a column strictly inside the last row of the section above";

    // The second section's top row, one column in.
    game.achievementScreen.section = 1;
    game.achievementScreen.cursor  = static_cast<std::uint8_t>(kColumn);

    press(dispatcher, game, Action::MenuUp);

    EXPECT_EQ(game.achievementScreen.section, 0);
    EXPECT_EQ(game.achievementScreen.cursor, lastRow * kCols + kColumn)
        << "up lands in the last row of the section above, in the column it left";
}

TEST(AchievementsScreen, WalkingOffTheBottomKeepsItsColumnInTheRowBelow) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    constexpr std::size_t kCols   = kirpich::systems::kAchievementGridCols;
    constexpr std::size_t kColumn = 2;

    const std::size_t here    = sectionSize(AchievementSection::ASCENT);
    const std::size_t lastRow = (here - 1) / kCols;
    ASSERT_LT(lastRow * kCols + kColumn, here)
        << "this case needs to start from that column in this section's last row";
    ASSERT_GT(sectionSize(AchievementSection::ENDURANCE), kColumn)
        << "this case needs the section below to reach that column in its first row";

    game.achievementScreen.section = 0;
    game.achievementScreen.cursor  = static_cast<std::uint8_t>(lastRow * kCols + kColumn);

    press(dispatcher, game, Action::MenuDown);

    EXPECT_EQ(game.achievementScreen.section, 1);
    EXPECT_EQ(game.achievementScreen.cursor, kColumn)
        << "down lands in the first row of the section below, in the column it left";
}

TEST(AchievementsScreen, AShorterRowClampsTheColumnToItsLastBadge) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    constexpr std::size_t kCols   = kirpich::systems::kAchievementGridCols;
    constexpr std::size_t kColumn = 2;

    // A section whose last row stops short of that column: the cursor lands on the badge that is
    // there rather than past the end of the section.
    const std::size_t above   = sectionSize(AchievementSection::ENDURANCE);
    const std::size_t lastRow = (above - 1) / kCols;
    ASSERT_GE(lastRow * kCols + kColumn, above)
        << "this case needs the section above to have a short last row";
    ASSERT_GT(sectionSize(AchievementSection::THE_TETRIS), kColumn)
        << "this case needs to start from that column";

    game.achievementScreen.section = 2;
    game.achievementScreen.cursor  = static_cast<std::uint8_t>(kColumn);

    press(dispatcher, game, Action::MenuUp);

    EXPECT_EQ(game.achievementScreen.section, 1);
    EXPECT_EQ(game.achievementScreen.cursor, above - 1)
        << "the column clamps to the last badge in the row it arrives at";
}

TEST(AchievementsScreen, TheWholeWalkHasExactlyTwoEndStops) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    // The very top: up from the first badge of the first section moves nothing.
    press(dispatcher, game, Action::MenuUp);
    EXPECT_EQ(game.achievementScreen.section, 0);
    EXPECT_EQ(game.achievementScreen.cursor, 0);

    // The very bottom: down from the last badge of the last section moves nothing.
    game.achievementScreen.section =
        static_cast<std::uint8_t>(kAchievementSectionCount - 1);
    game.achievementScreen.cursor =
        static_cast<std::uint8_t>(sectionSize(lastSection()) - 1);

    press(dispatcher, game, Action::MenuDown);
    EXPECT_EQ(game.achievementScreen.section, kAchievementSectionCount - 1);
    EXPECT_EQ(game.achievementScreen.cursor, sectionSize(lastSection()) - 1);
}

TEST(AchievementsScreen, ConfirmOpensTheBadgeUnderTheCursor) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    press(dispatcher, game, Action::MenuRight);
    const AchievementId expected =
        kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 1).id;

    press(dispatcher, game, Action::Confirm);

    EXPECT_TRUE(game.achievementScreen.open);
    EXPECT_EQ(game.achievementScreen.openId, expected) << "the badge opened is the one selected";
    EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENTS) << "the panel is a gate, not a screen";
}

TEST(AchievementsScreen, BackClosesAnOpenBadgeAndLeavesTheCursorWhereItWas) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    press(dispatcher, game, Action::MenuRight);
    press(dispatcher, game, Action::Confirm);
    ASSERT_TRUE(game.achievementScreen.open);

    press(dispatcher, game, Action::Back);

    EXPECT_FALSE(game.achievementScreen.open);
    EXPECT_EQ(game.achievementScreen.cursor, 1) << "it comes back to the badge just read";
    EXPECT_EQ(game.flow.gameState, GameState::ACHIEVEMENTS);
}

TEST(AchievementsScreen, TheGridIgnoresEverythingButBackWhileABadgeIsOpen) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);
    GameContext game = openScreen(dispatcher);

    press(dispatcher, game, Action::Confirm);
    ASSERT_TRUE(game.achievementScreen.open);

    const kirpich::AchievementScreenState before = game.achievementScreen;
    for (const Action a : {Action::MenuLeft, Action::MenuRight, Action::MenuUp, Action::MenuDown,
                           Action::Confirm}) {
        press(dispatcher, game, a);
    }
    EXPECT_EQ(game.achievementScreen, before) << "a press with a badge open moves nothing";
}

TEST(AchievementsScreen, BackOnTheGridLeavesTheScreen) {
    GameStateDispatcher dispatcher;
    kirpich::systems::installAchievementsScreen(dispatcher);

    GameContext game;
    game.flow.gameState = GameState::STATS_MENU;
    kirpich::systems::pushScreen(game, GameState::INIT_ACHIEVEMENTS);
    dispatcher.tick(game, retropp::ActionSet{});
    ASSERT_EQ(game.flow.gameState, GameState::ACHIEVEMENTS);

    press(dispatcher, game, Action::Back);

    EXPECT_EQ(game.flow.gameState, GameState::STATS_MENU) << "B goes back where it came from";
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

TEST(AchievementsScreen, TheScreenIsABackdropAndItsContent) {
    const kirpich::AchievementScreenState ui{};
    const kirpich::AchievementState       earned{};

    const kirpich::render::Layers layers =
        kirpich::render::AchievementsScreen(ui, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp);

    ASSERT_EQ(layers.size(), 2u) << "a backdrop of tiles, and everything else over it";
    EXPECT_EQ(layers[0].z, 0);
    EXPECT_LT(layers[0].z, layers[1].z) << "the content draws over the backdrop";
    EXPECT_TRUE(std::holds_alternative<retropp::TileContent>(layers[0].content));
    EXPECT_TRUE(std::holds_alternative<retropp::SpriteContent>(layers[1].content));
}

TEST(AchievementsScreen, TheHeadingNamesTheSectionOnThePage) {
    const kirpich::AchievementState earned{};

    for (std::uint8_t section = 0; section < kAchievementSectionCount; ++section) {
        kirpich::AchievementScreenState ui{};
        ui.section = section;

        const kirpich::render::Layers layers =
            kirpich::render::AchievementsScreen(ui, earned, /*blinkOn=*/false, kAtlas, kPlayerRamp);

        const std::string_view title = kirpich::systems::achievementSectionTitle(
            kirpich::systems::achievementSectionAt(section));
        const kirpich::render::Sprites heading = kirpich::render::Glyphs(
            title, kirpich::render::kAchHeadingX, kirpich::render::kAchHeadingY,
            kirpich::render::kAchGlyphPitch, kAtlas, kPlayerRamp);

        ASSERT_FALSE(heading.empty()) << "section " << int{section} << " has no title";
        const std::span<const retropp::Sprite> content = spritesOf(layers[1]);
        ASSERT_GE(content.size(), heading.size());
        for (std::size_t i = 0; i < heading.size(); ++i) {
            EXPECT_EQ(content[i].tile, heading[i].tile) << "section " << int{section} << " glyph " << i;
            EXPECT_EQ(content[i].x, heading[i].x);
        }
    }
}

TEST(AchievementsScreen, TheGridIsTheSectionsOwnBadgesAndNoOthers) {
    const AchievementSection        section = AchievementSection::ASCENT;
    const kirpich::AchievementState earned{};

    const kirpich::render::Sprites grid =
        kirpich::render::BadgeGrid(section, earned, kAtlas, kPlayerRamp);

    kirpich::render::Sprites expected;
    for (std::size_t slot = 0; slot < sectionSize(section); ++slot) {
        const kirpich::render::Sprites badge = kirpich::render::AchievementBadge(
            kirpich::systems::achievementSectionBadge(section, slot).id,
            kirpich::render::achGridX(slot), kirpich::render::achGridY(slot),
            /*unlocked=*/false, kAtlas, kPlayerRamp);
        expected.insert(expected.end(), badge.begin(), badge.end());
    }

    ASSERT_EQ(grid.size(), expected.size());
    for (std::size_t i = 0; i < grid.size(); ++i) {
        EXPECT_EQ(grid[i].x, expected[i].x) << "sprite " << i;
        EXPECT_EQ(grid[i].y, expected[i].y) << "sprite " << i;
        EXPECT_EQ(grid[i].palette, expected[i].palette) << "sprite " << i;
    }
}

TEST(AchievementsScreen, AnOpenBadgeReplacesTheGridWithItsPanel) {
    const kirpich::AchievementState earned{};

    kirpich::AchievementScreenState grid{};
    kirpich::AchievementScreenState open{};
    open.open   = true;
    open.openId = kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 0).id;

    const std::size_t gridSprites =
        spritesOf(kirpich::render::AchievementsScreen(grid, earned, false, kAtlas, kPlayerRamp)[1])
            .size();
    const std::size_t panelSprites =
        spritesOf(kirpich::render::AchievementsScreen(open, earned, false, kAtlas, kPlayerRamp)[1])
            .size();

    EXPECT_NE(gridSprites, panelSprites)
        << "the open badge's panel is drawn in place of the grid, not beside it";
}

TEST(AchievementsScreen, EarnedAndLockedBadgesDrawThroughDifferentPalettes) {
    const AchievementId id = kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 0).id;

    const kirpich::render::Sprites locked =
        kirpich::render::AchievementBadge(id, 0, 0, /*unlocked=*/false, kAtlas, kPlayerRamp);
    const kirpich::render::Sprites earned =
        kirpich::render::AchievementBadge(id, 0, 0, /*unlocked=*/true, kAtlas, kPlayerRamp);

    ASSERT_FALSE(locked.empty());
    ASSERT_EQ(locked.size(), earned.size()) << "the same art either way - only the palette differs";
    EXPECT_NE(locked[0].palette, earned[0].palette);
    EXPECT_EQ(locked[0].tile, earned[0].tile) << "locking is not a different picture";
}

TEST(AchievementsScreen, ALockedBadgeIgnoresThePlayersChosenRamp) {
    const AchievementId id = kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 0).id;

    const kirpich::render::Sprites one =
        kirpich::render::AchievementBadge(id, 0, 0, /*unlocked=*/false, kAtlas, /*ramp=*/2);
    const kirpich::render::Sprites other =
        kirpich::render::AchievementBadge(id, 0, 0, /*unlocked=*/false, kAtlas, /*ramp=*/9);

    ASSERT_FALSE(one.empty());
    EXPECT_EQ(one[0].palette, other[0].palette)
        << "a locked badge is grey whatever colours the player picked";
}

// The cursor is drawn around the badge, not into it: what is selected keeps its own colours, so a
// grid reads the same however it is being walked.
TEST(AchievementsScreen, TheCursorIsBracketsAroundTheBadgeAndLeavesItAlone) {
    const kirpich::AchievementState earned{};

    kirpich::AchievementScreenState first{};
    kirpich::AchievementScreenState second{};
    second.cursor = 1;

    const kirpich::render::Layers on =
        kirpich::render::AchievementsScreen(first, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp);
    const kirpich::render::Layers moved =
        kirpich::render::AchievementsScreen(second, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp);

    // The badges are identical wherever the cursor is - it changes none of them.
    const std::span<const retropp::Sprite> a = spritesOf(on[1]);
    const std::span<const retropp::Sprite> b = spritesOf(moved[1]);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].palette, b[i].palette) << "sprite " << i << " changed with the cursor";
        EXPECT_EQ(a[i].tile, b[i].tile) << "sprite " << i << " changed with the cursor";
    }

    // The brackets are what moved.
    ASSERT_FALSE(on[1].regions.empty()) << "the cursor draws nothing";
    ASSERT_EQ(on[1].regions.size(), moved[1].regions.size());
    EXPECT_NE(on[1].regions.front().shape.points, moved[1].regions.front().shape.points)
        << "the brackets did not follow the cursor";
}

TEST(AchievementsScreen, TheCursorBlinksAndIsGoneWhileABadgeIsOpen) {
    const kirpich::AchievementState earned{};
    kirpich::AchievementScreenState ui{};

    EXPECT_FALSE(
        kirpich::render::AchievementsScreen(ui, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp)[1]
            .regions.empty());
    EXPECT_TRUE(
        kirpich::render::AchievementsScreen(ui, earned, /*blinkOn=*/false, kAtlas, kPlayerRamp)[1]
            .regions.empty())
        << "it blinks with every other cursor in the game";

    ui.open = true;
    EXPECT_TRUE(
        kirpich::render::AchievementsScreen(ui, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp)[1]
            .regions.empty())
        << "there is nothing to select while a badge is open";
}

// Every achievement has both a title and a description, and both are spellable. The font has the
// letters, the digits, a period and a hyphen and nothing else, so a comma or an apostrophe would go
// silently missing on the panel rather than failing anywhere.
TEST(AchievementsScreen, EveryAchievementHasATitleAndADescriptionTheFontCanSpell) {
    const auto spellable = [](std::string_view text) {
        for (const char c : text) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                            c == '-' || c == ' ';
            if (!ok) return false;
        }
        return true;
    };

    for (const kirpich::systems::AchievementDef& def : kirpich::systems::achievementSet()) {
        const std::size_t index = kirpich::achievementIndex(def.id);
        EXPECT_FALSE(def.title.empty()) << "achievement " << index << " has no title";
        EXPECT_FALSE(def.description.empty()) << "achievement " << index << " has no description";
        EXPECT_TRUE(spellable(def.title)) << "title of " << index << ": " << def.title;
        EXPECT_TRUE(spellable(def.description)) << "description of " << index << ": " << def.description;
    }
}

// An opened badge shows what it is called and what it asks for.
TEST(AchievementsScreen, AnOpenBadgeShowsItsTitleAndItsDescription) {
    const kirpich::systems::AchievementDef& def =
        kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 0);
    ASSERT_FALSE(def.hidden) << "this case needs one that is readable while still locked";

    kirpich::AchievementState earned{};
    const kirpich::render::Sprites panel =
        kirpich::render::BadgePanel(def.id, earned, kAtlas, kPlayerRamp);

    const kirpich::render::Sprites title =
        kirpich::render::Glyphs(def.title, kirpich::render::kAchPanelTextX,
                                kirpich::render::kAchPanelTitleY, kirpich::render::kAchGlyphPitch,
                                kAtlas, kPlayerRamp);
    ASSERT_FALSE(title.empty());

    const auto carries = [&panel](const retropp::Sprite& glyph) {
        for (const retropp::Sprite& s : panel) {
            if (s.tile == glyph.tile && s.x == glyph.x && s.y == glyph.y) return true;
        }
        return false;
    };
    for (const retropp::Sprite& glyph : title) {
        EXPECT_TRUE(carries(glyph)) << "the panel does not draw its title";
    }

    // And the description, whose first line starts below the title.
    const kirpich::render::Sprites firstLine = kirpich::render::Glyphs(
        def.description.substr(0, std::min<std::size_t>(def.description.size(),
                                                        kirpich::render::kAchPanelTextCells)),
        kirpich::render::kAchPanelTextX, kirpich::render::kAchPanelDescY,
        kirpich::render::kAchGlyphPitch, kAtlas, kPlayerRamp);
    ASSERT_FALSE(firstLine.empty());
    EXPECT_TRUE(carries(firstLine.front())) << "the panel does not draw its description";
}

TEST(AchievementsScreen, AHiddenLockedBadgeSaysNothingUntilItIsEarned) {
    const std::span<const kirpich::systems::AchievementDef> set = kirpich::systems::achievementSet();
    const kirpich::systems::AchievementDef*                 hidden = nullptr;
    for (const kirpich::systems::AchievementDef& def : set) {
        if (def.hidden) {
            hidden = &def;
            break;
        }
    }
    ASSERT_NE(hidden, nullptr) << "the set is meant to carry hidden achievements";

    kirpich::AchievementState earned{};

    // The real title, drawn where a panel puts its title.
    const kirpich::render::Sprites realTitle = kirpich::render::Glyphs(
        hidden->title, kirpich::render::kAchPanelTextX, kirpich::render::kAchPanelTitleY,
        kirpich::render::kAchGlyphPitch, kAtlas, kPlayerRamp);
    ASSERT_FALSE(realTitle.empty());

    const auto carries = [](const kirpich::render::Sprites& panel, const retropp::Sprite& glyph) {
        for (const retropp::Sprite& s : panel) {
            if (s.tile == glyph.tile && s.x == glyph.x && s.y == glyph.y) return true;
        }
        return false;
    };

    // Still locked: it says there is something here without saying what.
    const kirpich::render::Sprites secret =
        kirpich::render::BadgePanel(hidden->id, earned, kAtlas, kPlayerRamp);
    const kirpich::render::Sprites badgeAlone = kirpich::render::AchievementBadge(
        hidden->id, kirpich::render::kAchPanelBadgeX, kirpich::render::kAchPanelBadgeY,
        /*unlocked=*/false, kAtlas, kPlayerRamp);

    EXPECT_GT(secret.size(), badgeAlone.size())
        << "a hidden badge still says something - a page with only art reads as broken";
    EXPECT_FALSE(carries(secret, realTitle.front()))
        << "but it does not give away the title it is hiding";

    // Earned: the real title appears.
    earned.unlocked[kirpich::achievementIndex(hidden->id)].unlocked = true;
    const kirpich::render::Sprites told =
        kirpich::render::BadgePanel(hidden->id, earned, kAtlas, kPlayerRamp);
    EXPECT_TRUE(carries(told, realTitle.front())) << "earning it reveals what it was";
}

// ── Where the count is shown ──────────────────────────────────────────────────────────────────────

namespace {

// Earn the first `n` of the set.
void earnFirst(GameContext& game, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        game.achievements.unlocked[i].unlocked = true;
    }
}

std::uint8_t digit(char c) {
    return static_cast<std::uint8_t>(static_cast<int>(kirpich::CharTile::DIGIT_0) + (c - '0'));
}

}  // namespace

TEST(AchievementsScreen, TheChooserRowSaysHowManyHaveBeenEarned) {
    kirpich::Settings   settings;
    GameStateDispatcher dispatcher;
    kirpich::systems::installStatsScreens(dispatcher, settings, {},
                                          kirpich::systems::SettingsWiring{});

    GameContext game;
    earnFirst(game, 3);
    game.flow.gameState = GameState::INIT_STATS_MENU;
    dispatcher.tick(game, retropp::ActionSet{});

    // The achievements row is the fifth, and the count sits one cell past the word.
    constexpr std::string_view kWord = "achievements";
    const std::size_t          line  = kirpich::systems::kListFirstRow + 4;
    const std::size_t          at    = kirpich::systems::kListTextCol + kWord.size() + 1;

    EXPECT_EQ(game.display.displayedMap()[line][at], digit('3'))
        << "the row does not carry how many are earned";
}

TEST(AchievementsScreen, TheAllTimePageSaysHowMuchOfTheSetIsEarned) {
    kirpich::Settings   settings;
    GameStateDispatcher dispatcher;
    kirpich::systems::installStatsScreens(dispatcher, settings, {},
                                          kirpich::systems::SettingsWiring{});

    GameContext game;
    earnFirst(game, 3);
    game.screens.statsBranch = static_cast<std::uint8_t>(kirpich::systems::StatsBranch::ALL_TIME);
    game.screens.statsScope  = kirpich::StatScope::ALL;
    game.flow.gameState      = GameState::INIT_STATS_PAGE;
    dispatcher.tick(game, retropp::ActionSet{});

    // "earned", and the figure of two parts ending at the column every value on the page ends at.
    const std::size_t line = kirpich::systems::kStatsFirstLine + 4;
    const auto&       map  = game.display.displayedMap();

    EXPECT_EQ(map[line][kirpich::systems::kStatsLabelCol],
              static_cast<std::uint8_t>(kirpich::CharTile::LETTER_E));

    const std::string_view expected = "3-36";
    const std::size_t      start = kirpich::systems::kStatsValueEndCol - expected.size() + 1;
    EXPECT_EQ(map[line][start], digit('3'));
    EXPECT_EQ(map[line][start + 1], static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
    EXPECT_EQ(map[line][start + 2], digit('3'));
    EXPECT_EQ(map[line][start + 3], digit('6'))
        << "the whole set's size is the second half of the figure";
}

// The same achievement drawn in the grid and again on its own panel is two objects, not one moved.
// Keyed alike, the engine reconciles them and eases the grid badge across the screen into the panel
// when a badge is opened - a badge that slides into place instead of appearing there.
TEST(AchievementsScreen, AGridBadgeAndAPanelBadgeAreDifferentObjects) {
    const AchievementId id =
        kirpich::systems::achievementSectionBadge(AchievementSection::ASCENT, 0).id;

    const kirpich::render::Sprites inGrid = kirpich::render::AchievementBadge(
        id, kirpich::render::achGridX(0), kirpich::render::achGridY(0), /*unlocked=*/false, kAtlas,
        kPlayerRamp);
    const kirpich::render::Sprites onPanel = kirpich::render::AchievementBadge(
        id, kirpich::render::kAchPanelBadgeX, kirpich::render::kAchPanelBadgeY, /*unlocked=*/false,
        kAtlas, kPlayerRamp);

    ASSERT_FALSE(inGrid.empty());
    ASSERT_EQ(inGrid.size(), onPanel.size());
    for (std::size_t i = 0; i < inGrid.size(); ++i) {
        EXPECT_NE(inGrid[i].key, onPanel[i].key)
            << "part " << i << " carries the same name in both places";
    }
}

// And within one frame no two objects share a name at all, which is what the renderer requires.
TEST(AchievementsScreen, NoTwoObjectsInAFrameShareAName) {
    const kirpich::AchievementState earned{};

    for (std::uint8_t section = 0; section < kAchievementSectionCount; ++section) {
        for (const bool open : {false, true}) {
            kirpich::AchievementScreenState ui{};
            ui.section = section;
            ui.open    = open;
            ui.openId  = kirpich::systems::achievementSectionBadge(
                            kirpich::systems::achievementSectionAt(section), 0)
                            .id;

            const kirpich::render::Layers layers = kirpich::render::AchievementsScreen(
                ui, earned, /*blinkOn=*/true, kAtlas, kPlayerRamp);

            std::vector<retropp::ObjectKey> keys;
            for (const retropp::Sprite& s : spritesOf(layers[1])) {
                keys.push_back(s.key);
            }
            for (std::size_t i = 0; i < keys.size(); ++i) {
                for (std::size_t j = i + 1; j < keys.size(); ++j) {
                    EXPECT_NE(keys[i], keys[j])
                        << "section " << int{section} << (open ? " panel" : " grid")
                        << ": two objects share a name";
                }
            }
        }
    }
}

TEST(AchievementsScreen, TheScreenIsShownOnItsOwnStateAndNoOther) {
    EXPECT_TRUE(kirpich::render::achievementScreenShown(GameState::ACHIEVEMENTS));
    for (const GameState s : {GameState::INIT_ACHIEVEMENTS, GameState::STATS_MENU,
                              GameState::STATS_PAGE, GameState::NORMAL_GAMEPLAY,
                              GameState::TITLE_SCREEN, GameState::SETTINGS}) {
        EXPECT_FALSE(kirpich::render::achievementScreenShown(s)) << "state " << int(s);
    }
}
