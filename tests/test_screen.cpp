// Screen loading — behavioral tests against docs/contracts/screen.md.
//
// Device-free: stamping a backdrop and choosing a tile set are both writes to the game-state
// aggregate, with no renderer involved. Every asserted value is traced to the tetris.asm lines named
// in the contract.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/tilemaps.h"
#include "state/display_state.h"
#include "state/playing_field_state.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/menu_screens.h"
#include "systems/screen.h"
#include "systems/title_screens.h"

namespace {

using kirpich::DisplayState;
using kirpich::GameType;
using kirpich::PlayingFieldState;
using kirpich::TileSheet;
using kirpich::systems::GameContext;
using kirpich::systems::ScreenTilemap;

constexpr std::uint8_t kSentinel = 0xC7;  // a value no backdrop carries, to prove a cell was written

// Every stored screen that is a full 20x18 backdrop. The nine the game loads through
// LoadTilemap.to9800; the other thirteen stored maps are banners, field overlays, window blocks,
// tower columns and the congratulations strip, which have other shapes and other loaders.
const std::vector<std::pair<std::string, const ScreenTilemap*>>& fullScreens() {
    static const std::vector<std::pair<std::string, const ScreenTilemap*>> screens{
        {"TypeAGameplay", &kirpich::kTypeAGameplayTilemap},
        {"TypeBGameplay", &kirpich::kTypeBGameplayTilemap},
        {"CopyrightScreen", &kirpich::kCopyrightScreenTilemap},
        {"TitleScreen", &kirpich::kTitleScreenTilemap},
        {"ConfigScreen", &kirpich::kConfigScreenTilemap},
        {"TypeADifficulty", &kirpich::kTypeADifficultyTilemap},
        {"TypeBDifficulty", &kirpich::kTypeBDifficultyTilemap},
        {"MultiplayerDifficulty", &kirpich::kMultiplayerDifficultyTilemap},
        {"MultiplayerGameplay", &kirpich::kMultiplayerGameplayTilemap},
    };
    return screens;
}

}  // namespace

// ── Test 1: BackdropStampsTheVisibleCorner ──────────────────────────────────────────────────────────
// LoadTilemap.to9800 (tetris.asm:6410-6431) walks SCRN_Y_B (18) rows of SCRN_X_B (20) cells, stepping
// SCRN_VX_B (32) between rows. Full corpus: every one of the nine stored full screens, every cell.
TEST(Screen, BackdropStampsTheVisibleCorner) {
    for (const auto& [name, tilemap] : fullScreens()) {
        kirpich::DisplayState display;
        for (auto& row : display.map) {
            row.fill(kSentinel);
        }
        PlayingFieldState field;
        for (auto& row : field.board) {
            row.fill(kSentinel);
        }

        kirpich::systems::loadScreenTilemap(display, *tilemap);

        for (std::size_t row = 0; row < kirpich::kBackgroundMapRows; ++row) {
            for (std::size_t col = 0; col < kirpich::kBackgroundMapCols; ++col) {
                const bool inside =
                    row < kirpich::kTilemapScreenRows && col < kirpich::kTilemapScreenCols;
                const std::uint8_t expected = inside ? (*tilemap)[row][col] : kSentinel;
                ASSERT_EQ(display.map[row][col], expected)
                    << name << " cell " << row << "," << col;
            }
        }

        // A backdrop reaches the displayed map and never the board: the board is the game's own copy
        // of the field, filled separately, so a backdrop can neither lay one out nor erase one.
        for (const auto& row : field.board) {
            for (const std::uint8_t cell : row) {
                ASSERT_EQ(cell, kSentinel) << name << " must not touch the board";
            }
        }
    }
}

// ── Test 2: TheEmptyCellMeansTheSameThingOnEveryScreen ──────────────────────────────────────────────
// The loader's window form is not ported: the two call sites it serves write the original's SECOND
// background map, which the port does not model (docs/contracts/screen.md). What is asserted here
// instead is the property that lets one board serve every screen.
//
// LoadGameplayTiles (:6368-6376) copies ten tiles of the copyright art before laying the gameplay art
// over the last of them, so nine survive at $27-$2F under the gameplay regime. The board's empty cell
// is $2F, the last of those nine — so a blank cell is the same picture on a title screen and on a
// gameplay screen, which is what lets one board serve both. If the carry-over were shorter, every
// blank cell on a gameplay screen would draw the wrong tile.
TEST(Screen, TheEmptyCellMeansTheSameThingOnEveryScreen) {
    constexpr auto empty = static_cast<std::uint8_t>(kirpich::CharTile::SPACE);
    EXPECT_EQ(empty, 0x2F);

    // Both backdrops that carry a playing field leave its ten columns empty, which is why stamping
    // one lays out an empty field rather than erasing a live one.
    for (const auto* tilemap : {&kirpich::kTypeAGameplayTilemap, &kirpich::kTypeBGameplayTilemap}) {
        for (std::size_t row = 0; row < kirpich::kTilemapScreenRows; ++row) {
            for (std::size_t col = kirpich::kPlayingFieldOriginCol;
                 col < kirpich::kPlayingFieldOriginCol + kirpich::kPlayingFieldCols; ++col) {
                ASSERT_EQ((*tilemap)[row][col], empty) << "field cell " << row << "," << col;
            }
        }
    }
}

// ── Test 3: RestoredCallSitesLeaveTheRegimeTheROMWouldLeave ─────────────────────────────────────────
// Each screen init leaves the tile set its paired loader call leaves — or leaves the regime alone
// where the original makes no call. Boot is the regime the first screen loads (:481).
TEST(Screen, RestoredCallSitesLeaveTheRegimeTheROMWouldLeave) {
    EXPECT_EQ(DisplayState{}.sheet, TileSheet::COPYRIGHT_TITLE);

    // $24 copyright init: LoadCopyrightAndTitleScreenTiles (:481).
    {
        GameContext game;
        game.display.sheet = TileSheet::GAMEPLAY;  // dirty it, to prove the call writes
        kirpich::systems::initCopyrightScreen(game);
        EXPECT_EQ(game.display.sheet, TileSheet::COPYRIGHT_TITLE);
        EXPECT_EQ(game.display.map[0][0], kirpich::kCopyrightScreenTilemap[0][0]);
    }
    // $06 title init: the same loader (:537), then the title backdrop over the board paint (:556-557).
    {
        GameContext game;
        game.display.sheet = TileSheet::GAMEPLAY;
        kirpich::systems::initTitleScreen(game);
        EXPECT_EQ(game.display.sheet, TileSheet::COPYRIGHT_TITLE);
        EXPECT_EQ(game.display.map[0][0], kirpich::kTitleScreenTilemap[0][0]);
    }
    // $08 config body: LoadGameplayTiles (:3123) and the config backdrop (:3124-3125).
    {
        GameContext game;
        kirpich::systems::loadConfigScreenBody(game);
        EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
        EXPECT_EQ(game.display.map[0][0], kirpich::kConfigScreenTilemap[0][0]);
    }
    // $10 / $12 difficulty inits: a backdrop each (:3319-3320, :3410-3411) and NO loader call — the
    // config screen already loaded the gameplay set, and the original does not reload it.
    {
        GameContext game;
        game.display.sheet = TileSheet::GAMEPLAY;
        kirpich::systems::initTypeADifficultyScreen(game, {});
        EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
        EXPECT_EQ(game.display.map[0][0], kirpich::kTypeADifficultyTilemap[0][0]);

        // The absence of a load is the assertion: an unrelated regime survives the call.
        GameContext other;
        other.display.sheet = TileSheet::COPYRIGHT_TITLE;
        kirpich::systems::initTypeADifficultyScreen(other, {});
        EXPECT_EQ(other.display.sheet, TileSheet::COPYRIGHT_TITLE);
    }
    {
        GameContext game;
        game.display.sheet = TileSheet::GAMEPLAY;
        kirpich::systems::initTypeBDifficultyScreen(game, {});
        EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
        EXPECT_EQ(game.display.map[0][0], kirpich::kTypeBDifficultyTilemap[0][0]);
    }
    // $0A round init: the backdrop forks on game type (:4141 / :4148, loaded :4154), and again no
    // loader call.
    for (const auto [type, tilemap] :
         {std::pair{GameType::TYPE_A, &kirpich::kTypeAGameplayTilemap},
          std::pair{GameType::TYPE_B, &kirpich::kTypeBGameplayTilemap}}) {
        GameContext game;
        game.flow.gameType = type;
        game.display.sheet = TileSheet::GAMEPLAY;
        kirpich::systems::initGame(game, [] { return std::uint8_t{0}; }, {});

        EXPECT_EQ(game.display.sheet, TileSheet::GAMEPLAY);
        // The panel columns, which no later write touches, carry the chosen backdrop.
        for (std::size_t col = kirpich::kTilemapScreenCols - 6; col < kirpich::kTilemapScreenCols;
             ++col) {
            ASSERT_EQ(game.display.map[0][col], (*tilemap)[0][col]) << "panel col " << col;
        }
        // And the walls it supplies bracket the field.
        EXPECT_EQ(game.display.map[0][1], (*tilemap)[0][1]);
        EXPECT_EQ(game.display.map[0][12], (*tilemap)[0][12]);
    }
}

// ── Test 4: ContextCarriesTheDisplayState ───────────────────────────────────────────────────────────
// The aggregate gained a member: it must still boot, still reset whole, and still compare.
TEST(Screen, ContextCarriesTheDisplayState) {
    GameContext game;
    EXPECT_EQ(game.display.sheet, TileSheet::COPYRIGHT_TITLE);
    EXPECT_EQ(game, GameContext{});

    game.display.sheet = TileSheet::GAMEPLAY;
    EXPECT_NE(game, GameContext{});  // the new member participates in equality

    game.reset();
    EXPECT_EQ(game.display.sheet, TileSheet::COPYRIGHT_TITLE);
    EXPECT_EQ(game, GameContext{});

    DisplayState display;
    display.sheet = TileSheet::GAMEPLAY;
    display.reset();
    EXPECT_EQ(display, DisplayState{});
}
