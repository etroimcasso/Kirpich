// The doors to Type C — the game-type grid and the Type C difficulty screen.
//
// Device-free. The grid is background cells and one cursor slot; the difficulty screen is the Type A
// screen's walk over Type C's own stored level.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/sprite_id.h>

#include <retropp/input.h>

#include "data/tilemaps.h"
#include "systems/game_context.h"
#include "systems/menu_screens.h"

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
