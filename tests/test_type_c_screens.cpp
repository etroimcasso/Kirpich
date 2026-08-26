// The doors to Type C — the game-type grid and the Type C difficulty screen.
//
// Device-free. The grid is background cells and one cursor slot; the difficulty screen is the Type B
// screen with two words changed, over Type C's own stored level and rise. What the rise values look
// like is the render layer's (src/render/type_c_difficulty.h); what is here is which one is chosen.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/sprite_id.h>

#include <retropp/input.h>

#include <kirpich/char_tile.h>

#include "data/tilemaps.h"
#include "render/type_c_difficulty.h"        // riseValuesShown
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"   // kGameStateCount
#include "systems/menu_screens.h"
#include "systems/rising_floor.h"            // kTypeCRiseChoiceCount

namespace {

using kirpich::Action;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::kConfigScreenTilemap;
using kirpich::SpriteId;
using kirpich::systems::GameContext;

constexpr std::size_t kGameCursorSlot = 1;

// The rows the grid builds, and the rows of the stored screen it builds them from.
constexpr std::size_t kDividerRow      = 6;
constexpr std::size_t kSecondChoiceRow = 7;
constexpr std::size_t kBottomRow       = 8;
constexpr std::size_t kStoredMusicDivider      = 13;
constexpr std::size_t kStoredMusicSecondChoice = 14;
constexpr std::size_t kStoredGameBoxBottom     = 6;

// The two halves of a choice row.
constexpr std::size_t kLeftFirstCol  = 3;
constexpr std::size_t kLeftLastCol   = 8;
constexpr std::size_t kRightFirstCol = 11;
constexpr std::size_t kRightLastCol  = 16;

constexpr std::uint8_t kSpaceTile = 0x2F;

void press(GameContext& game, Action action) {
    retropp::ActionSet set;
    set.set(retropp::actionId(action), true);
    game.joypad.pressed = set;
    game.joypad.held    = set;
}

}  // namespace

// 1. The grid grows the game-type box into the blank rows below it, in the music box's form, and the
// bottom-right cell is left empty because there is no fourth mode yet.
TEST(TypeCScreens, GridGrowsTheGameTypeBox) {
    GameContext game;
    kirpich::systems::initConfigScreen(game, /*showSection=*/false, /*showGrid=*/true);

    const auto& map = game.display.map;

    // The rule between the two choice rows, and the box's bottom, are the screen's own art.
    for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
        EXPECT_EQ(map[kDividerRow][col], kConfigScreenTilemap[kStoredMusicDivider][col])
            << "divider column " << col;
        EXPECT_EQ(map[kBottomRow][col], kConfigScreenTilemap[kStoredGameBoxBottom][col])
            << "bottom column " << col;
    }

    // The second choice row carries the c-type label on its left half - the same art the music box
    // uses for its own third choice.
    for (std::size_t col = kLeftFirstCol; col <= kLeftLastCol; ++col) {
        EXPECT_EQ(map[kSecondChoiceRow][col], kConfigScreenTilemap[kStoredMusicSecondChoice][col])
            << "c-type label column " << col;
    }

    // And nothing on its right half.
    for (std::size_t col = kRightFirstCol; col <= kRightLastCol; ++col) {
        EXPECT_EQ(map[kSecondChoiceRow][col], kSpaceTile) << "empty cell column " << col;
    }

    // The music box below is untouched.
    for (std::size_t row = 9; row <= 15; ++row) {
        for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
            EXPECT_EQ(map[row][col], kConfigScreenTilemap[row][col])
                << "music box (" << row << ", " << col << ")";
        }
    }
}

// 2. With the modes off the screen is the cartridge's, cell for cell.
TEST(TypeCScreens, MasterOffLeavesTheStoredScreen) {
    GameContext game;
    kirpich::systems::initConfigScreen(game, /*showSection=*/false, /*showGrid=*/false);

    for (std::size_t row = 0; row < kirpich::kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
            EXPECT_EQ(game.display.map[row][col], kConfigScreenTilemap[row][col])
                << "(" << row << ", " << col << ")";
        }
    }
}

// 3. The walk covers all three live cells and refuses the empty one.
TEST(TypeCScreens, GridWalkCoversThreeCellsAndSkipsTheEmptyOne) {
    GameContext game;
    // The boot seeds Type A before any screen runs (systems/boot.cpp); the config screen reads the
    // choice rather than setting it, so a test that starts at a screen has to seed it too.
    game.flow.gameType = GameType::TYPE_A;
    kirpich::systems::initConfigScreen(game, /*showSection=*/false, /*showGrid=*/true);

    const auto step = [&](Action action) {
        press(game, action);
        kirpich::systems::selectGameType(game, /*showSection=*/false, /*showGrid=*/true);
    };
    const auto label = [&] { return game.spriteRenderer.slots[kGameCursorSlot].spriteId; };

    step(Action::MenuRight);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
    EXPECT_EQ(label(), SpriteId::B_TYPE);

    // Nothing below B: the empty cell takes no cursor.
    step(Action::MenuDown);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);

    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);

    step(Action::MenuDown);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_C);
    EXPECT_EQ(label(), SpriteId::C_TYPE);

    // Nothing right of C either, and nothing below it.
    step(Action::MenuRight);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_C);
    step(Action::MenuDown);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_C);

    step(Action::MenuUp);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
}

// 4. With the modes off the walk is the cartridge's: two choices, left and right, and C unreachable.
TEST(TypeCScreens, MasterOffWalksTwoChoicesOnly) {
    GameContext game;
    game.flow.gameType = GameType::TYPE_A;
    kirpich::systems::initConfigScreen(game, /*showSection=*/false, /*showGrid=*/false);

    const auto step = [&](Action action) {
        press(game, action);
        kirpich::systems::selectGameType(game, /*showSection=*/false, /*showGrid=*/false);
    };

    step(Action::MenuDown);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A) << "down reaches nothing on the stored screen";

    step(Action::MenuRight);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
    step(Action::MenuDown);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
}

// 5. Start from each cell reaches that mode's own difficulty screen.
TEST(TypeCScreens, StartReachesEachModesDifficultyScreen) {
    struct Vector {
        GameType  type;
        GameState expected;
    };
    for (const Vector v : {Vector{GameType::TYPE_A, GameState::INIT_TYPE_A_DIFFICULTY},
                           Vector{GameType::TYPE_B, GameState::INIT_TYPE_B_DIFFICULTY},
                           Vector{GameType::TYPE_C, GameState::INIT_TYPE_C_DIFFICULTY}}) {
        GameContext game;
        kirpich::systems::initConfigScreen(game, /*showSection=*/false, /*showGrid=*/true);
        game.flow.gameType = v.type;

        press(game, Action::Start);
        kirpich::systems::selectGameType(game, /*showSection=*/false, /*showGrid=*/true);

        EXPECT_EQ(game.flow.gameState, v.expected);
    }
}

// 6. The Type C difficulty screen picks a level, writes Type C's own byte, and begins the round.
TEST(TypeCScreens, TypeCDifficultyScreenPicksItsOwnLevel) {
    GameContext game;
    game.flow.gameType  = GameType::TYPE_C;
    game.flow.typeALevel = 4;  // the other modes' choices must not move
    game.flow.typeBLevel = 7;
    game.flow.typeCLevel = 0;

    kirpich::systems::initTypeCDifficultyScreen(game);
    EXPECT_EQ(game.flow.gameState, GameState::TYPE_C_LEVEL_SELECTION);

    const auto step = [&](Action action) {
        press(game, action);
        kirpich::systems::selectTypeCLevel(game);
    };

    step(Action::MenuRight);
    EXPECT_EQ(game.flow.typeCLevel, 1);
    step(Action::MenuDown);
    EXPECT_EQ(game.flow.typeCLevel, 6);
    step(Action::MenuUp);
    EXPECT_EQ(game.flow.typeCLevel, 1);
    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.typeCLevel, 0);

    // Both end stops.
    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.typeCLevel, 0);
    step(Action::MenuUp);
    EXPECT_EQ(game.flow.typeCLevel, 0);

    EXPECT_EQ(game.flow.typeALevel, 4) << "the other modes' levels are their own";
    EXPECT_EQ(game.flow.typeBLevel, 7);

    step(Action::Start);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME);
}

// 7. Back from the Type C level picker returns to the config screen, as the Type A one does.
TEST(TypeCScreens, BackFromTypeCLevelReturnsToTheConfigScreen) {
    GameContext game;
    game.flow.gameType = GameType::TYPE_C;
    kirpich::systems::initTypeCDifficultyScreen(game);

    press(game, Action::Back);
    kirpich::systems::selectTypeCLevel(game);

    EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_SELECTION);
}

// 8. The borrowed difficulty screen names Type C, not the mode it was drawn for. The name-entry screen
// paints over whichever difficulty screen it was entered from and draws no backdrop of its own, so
// this heading is what a player sees while typing their name too.
TEST(TypeCScreens, TheDifficultyScreenNamesTypeC) {
    using C = kirpich::CharTile;

    GameContext game;
    game.flow.gameType = GameType::TYPE_C;
    kirpich::systems::initTypeCDifficultyScreen(game);

    constexpr std::size_t kRow = 1;
    constexpr std::size_t kCol = 2;
    const C expected[] = {C::LETTER_C, C::HYPHEN, C::LETTER_T, C::LETTER_Y, C::LETTER_P, C::LETTER_E};
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        EXPECT_EQ(game.display.map[kRow][kCol + i], static_cast<std::uint8_t>(expected[i]))
            << "heading cell " << i;
    }

    // The stored screen it borrows still says what it always said.
    EXPECT_EQ(kirpich::kTypeBDifficultyTilemap[kRow][kCol],
              static_cast<std::uint8_t>(C::LETTER_B))
        << "the stored screen is read, never edited";
}

// 9. The screen is the Type B one with two words changed. Its right-hand box is labelled "rise" in the
// four cells Type B labels "high", and every other cell of the backdrop is the stored screen's own -
// the boxes are not rebuilt, because the rise values are drawn over them by the render layer.
TEST(TypeCScreens, TheDifficultyScreenIsTypeBsWithTwoWordsChanged) {
    using C = kirpich::CharTile;

    GameContext game;
    game.flow.gameType = GameType::TYPE_C;
    kirpich::systems::initTypeCDifficultyScreen(game);

    constexpr std::size_t kLabelRow = 4;
    constexpr std::size_t kLabelCol = 13;
    const C label[] = {C::LETTER_R, C::LETTER_I, C::LETTER_S, C::LETTER_E};
    for (std::size_t i = 0; i < std::size(label); ++i) {
        EXPECT_EQ(game.display.map[kLabelRow][kLabelCol + i], static_cast<std::uint8_t>(label[i]))
            << "label cell " << i;
    }

    // "high" and "rise" are the same four letters wide, so the cell after the label is still the box's
    // own right edge rather than a leftover glyph.
    EXPECT_EQ(game.display.map[kLabelRow][kLabelCol + 4],
              kirpich::kTypeBDifficultyTilemap[kLabelRow][kLabelCol + 4]);

    // The six compartments are emptied. The values that go in them are two glyphs each and are drawn
    // over the box by the render layer through a see-through palette, so the stored single digit would
    // otherwise still be readable under them.
    const auto isCompartment = [](std::size_t row, std::size_t col) {
        for (const std::size_t r : kirpich::systems::kRiseValueRows) {
            for (const std::size_t c : kirpich::systems::kRiseValueCols) {
                if (r == row && c == col) return true;
            }
        }
        return false;
    };
    for (const std::size_t row : kirpich::systems::kRiseValueRows) {
        for (const std::size_t col : kirpich::systems::kRiseValueCols) {
            EXPECT_EQ(game.display.map[row][col], 0x2F) << "compartment " << row << "," << col;
            EXPECT_NE(game.display.map[row][col], kirpich::kTypeBDifficultyTilemap[row][col])
                << "the stored digit is gone";
        }
    }

    // Everything the two words and the six compartments did not touch is the stored screen, cell for
    // cell - including both boxes' frames, every rule between the compartments, and the level grid,
    // which Type C uses exactly as Type B does.
    for (std::size_t row = 0; row < kirpich::kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
            const bool heading = row == 1 && col >= 2 && col < 8;
            const bool labelled = row == kLabelRow && col >= kLabelCol && col < kLabelCol + 4;
            if (heading || labelled || isCompartment(row, col)) {
                continue;
            }
            EXPECT_EQ(game.display.map[row][col], kirpich::kTypeBDifficultyTilemap[row][col])
                << "row " << row << " col " << col;
        }
    }
}

// 10. The rise picker walks the six values as a grid of three across and two down - the shape the
// stored box has - with a stop at each edge. The index is what moves; what it means is the rise table's
// business.
TEST(TypeCScreens, TheRisePickerWalksThreeAcrossAndTwoDown) {
    GameContext game;
    game.flow.gameType = GameType::TYPE_C;
    kirpich::systems::initTypeCDifficultyScreen(game);

    press(game, Action::Confirm);
    kirpich::systems::selectTypeCLevel(game);
    ASSERT_EQ(game.flow.gameState, GameState::TYPE_C_RISE_SELECTION)
        << "Confirm from the level picker goes on to the rise picker, as Type B's does";

    const auto step = [&](Action action) {
        press(game, action);
        kirpich::systems::selectTypeCRise(game);
    };

    EXPECT_EQ(game.flow.typeCRise, 0);

    step(Action::MenuRight);
    EXPECT_EQ(game.flow.typeCRise, 1);
    step(Action::MenuRight);
    EXPECT_EQ(game.flow.typeCRise, 2);
    step(Action::MenuRight);
    EXPECT_EQ(game.flow.typeCRise, 2) << "the right edge of the top row";

    step(Action::MenuDown);
    EXPECT_EQ(game.flow.typeCRise, 5);
    step(Action::MenuDown);
    EXPECT_EQ(game.flow.typeCRise, 5) << "the bottom row";

    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.typeCRise, 4);
    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.typeCRise, 3);
    step(Action::MenuLeft);
    EXPECT_EQ(game.flow.typeCRise, 3) << "the left edge of the bottom row";

    step(Action::MenuUp);
    EXPECT_EQ(game.flow.typeCRise, 0);
    step(Action::MenuUp);
    EXPECT_EQ(game.flow.typeCRise, 0) << "the top row";

    // The walk never leaves the table it indexes.
    EXPECT_LT(game.flow.typeCRise, kirpich::systems::kTypeCRiseChoiceCount);
}

// 11. The rise picker's two ways out: begin the round, or step back to the level picker. Both leave the
// current value drawn rather than wherever the blink had got to.
TEST(TypeCScreens, TheRisePickerLeavesForTheRoundOrTheLevel) {
    for (const Action action : {Action::Start, Action::Confirm}) {
        GameContext game;
        game.flow.gameType  = GameType::TYPE_C;
        game.flow.gameState = GameState::TYPE_C_RISE_SELECTION;
        game.screens.cursorVisible = false;

        press(game, action);
        kirpich::systems::selectTypeCRise(game);

        EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME);
        EXPECT_TRUE(game.screens.cursorVisible) << "the chosen value is left drawn";
    }

    GameContext back;
    back.flow.gameType  = GameType::TYPE_C;
    back.flow.gameState = GameState::TYPE_C_RISE_SELECTION;
    back.flow.typeCRise = 4;
    back.screens.cursorVisible = false;

    press(back, Action::Back);
    kirpich::systems::selectTypeCRise(back);

    EXPECT_EQ(back.flow.gameState, GameState::TYPE_C_LEVEL_SELECTION);
    EXPECT_EQ(back.flow.typeCRise, 4) << "stepping back keeps the rise the player had picked";
    EXPECT_TRUE(back.screens.cursorVisible);
    EXPECT_FALSE(back.spriteRenderer.slots[0].hidden) << "and the level cursor is drawn again";
}

// 11b. The values are drawn for as long as the screen is up, which is longer than the picker that
// walks them: name entry paints over the difficulty screen it was entered from and draws no backdrop of
// its own, so a Type C round that earns a top score is still looking at this box. The compartments are
// blank in the map, so a state this gate misses shows six empty boxes.
TEST(TypeCScreens, TheRiseValuesAreDrawnWhereverTheScreenIsUp) {
    using kirpich::render::riseValuesShown;

    for (const GameState state : {GameState::INIT_TYPE_C_DIFFICULTY,
                                  GameState::TYPE_C_LEVEL_SELECTION,
                                  GameState::TYPE_C_RISE_SELECTION}) {
        EXPECT_TRUE(riseValuesShown(state, GameType::TYPE_C));
    }

    // Name entry forks on the round's own type, because the other two modes reach it over their own
    // difficulty screens.
    EXPECT_TRUE(riseValuesShown(GameState::ENTER_TOP_SCORE, GameType::TYPE_C));
    EXPECT_FALSE(riseValuesShown(GameState::ENTER_TOP_SCORE, GameType::TYPE_A));
    EXPECT_FALSE(riseValuesShown(GameState::ENTER_TOP_SCORE, GameType::TYPE_B));

    // And nowhere else. A sweep rather than a handful of cases: the screen is one of very few states,
    // and drawing its values over a round or another mode's picker would be plain wrong.
    for (std::size_t raw = 0; raw < kirpich::systems::kGameStateCount; ++raw) {
        const auto state = static_cast<GameState>(raw);
        const bool expected = state == GameState::INIT_TYPE_C_DIFFICULTY ||
                              state == GameState::TYPE_C_LEVEL_SELECTION ||
                              state == GameState::TYPE_C_RISE_SELECTION ||
                              state == GameState::ENTER_TOP_SCORE;
        EXPECT_EQ(riseValuesShown(state, GameType::TYPE_C), expected) << "state " << raw;
    }
}

// 12. A move on either picker refreshes the top scores, which is what puts the table for the pair the
// player is now looking at on screen. The rise picker earns this the same way the level picker does.
TEST(TypeCScreens, MovingEitherPickerRefreshesTheTopScores) {
    int refreshes = 0;
    const auto refresh = [&refreshes](GameContext&) { ++refreshes; };

    GameContext game;
    game.flow.gameType  = GameType::TYPE_C;
    game.flow.gameState = GameState::TYPE_C_RISE_SELECTION;

    press(game, Action::MenuRight);
    kirpich::systems::selectTypeCRise(game, refresh);
    EXPECT_EQ(refreshes, 1) << "a move refreshes";

    // A move that lands nowhere - the end stop - refreshes nothing, because the pair has not changed.
    game.flow.typeCRise = 2;
    press(game, Action::MenuRight);
    kirpich::systems::selectTypeCRise(game, refresh);
    EXPECT_EQ(refreshes, 1) << "an end stop is not a move";
}
