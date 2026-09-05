// The heart-mode indicator — behavioral tests against src/render/heart_indicator.h.
//
// Device-free. Uploading art needs a renderer; deciding whether a heart belongs on the screen, and
// where, does not — a TileAtlas is a bag of handles, and this file fills one with recognisable values
// so a sprite drawn from the wrong sheet or the wrong palette is visible in the assertion.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/tilemaps.h"  // kTypeADifficultyTilemap
#include "render/heart_indicator.h"
#include "render/tile_atlas.h"
#include "state/display_state.h"    // TileSheet
#include "systems/game_context.h"
#include "systems/menu_screens.h"   // the heading the indicator is placed beside

namespace {

using kirpich::GameState;
using kirpich::render::TileAtlas;
using kirpich::render::heartIndicatorShown;
using kirpich::render::heartIndicatorSprite;

constexpr std::uint8_t kOn  = 1;
constexpr std::uint8_t kOff = 0;

// The ramp the sprites are asked for. Not the default, so a palette taken from the wrong ramp shows.
constexpr std::uint8_t kRamp = 3;

constexpr retropp::PaletteId spritePaletteFor(std::size_t ramp) {
    return static_cast<retropp::PaletteId>(70 + ramp);
}

constexpr TileAtlas makeAtlas() {
    TileAtlas atlas{
        .font             = static_cast<retropp::AtlasId>(11),
        .copyrightTitle   = static_cast<retropp::AtlasId>(22),
        .gameplay         = static_cast<retropp::AtlasId>(33),
        .multiplayerBuran = static_cast<retropp::AtlasId>(44),
    };
    for (std::size_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
        atlas.palettes[ramp].sprite0 = spritePaletteFor(ramp);
    }
    return atlas;
}

constexpr TileAtlas kAtlas = makeAtlas();

// The eight states a difficulty screen can be in, plus name entry, which is the ninth because it has
// no backdrop of its own and is looking at whichever difficulty screen it was entered from.
constexpr GameState kShowing[] = {
    GameState::INIT_TYPE_A_DIFFICULTY, GameState::TYPE_A_LEVEL_SELECTION,
    GameState::INIT_TYPE_B_DIFFICULTY, GameState::TYPE_B_LEVEL_SELECTION,
    GameState::TYPE_B_HEIGHT_SELECTION, GameState::INIT_TYPE_C_DIFFICULTY,
    GameState::TYPE_C_LEVEL_SELECTION, GameState::TYPE_C_RISE_SELECTION,
    GameState::ENTER_TOP_SCORE,
};

bool listedAsShowing(GameState state) {
    for (const GameState s : kShowing) {
        if (s == state) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ── Test 1: GateSweptOverEveryState ─────────────────────────────────────────────────────────────────
// The gate is heart mode being on AND a difficulty screen being on the display. The sweep runs the
// whole state space rather than the states the indicator is meant for, so a state that starts drawing
// a heart it should not is caught by the same case that says which ones should.
TEST(HeartIndicator, GateSweptOverEveryState) {
    for (unsigned raw = 0; raw <= 0xFF; ++raw) {
        const auto state = static_cast<GameState>(raw);

        // Heart mode off: nothing is ever drawn, whatever is on the screen.
        EXPECT_FALSE(heartIndicatorShown(state, kOff)) << "state 0x" << std::hex << raw;

        EXPECT_EQ(heartIndicatorShown(state, kOn), listedAsShowing(state))
            << "state 0x" << std::hex << raw;
    }
}

// ── Test 2: AnyNonZeroFlagCountsAsOn ────────────────────────────────────────────────────────────────
// Heart mode is read as zero / non-zero, never compared to a particular value: the cartridge latched
// the raw held-joypad byte there and the port stores a canonical 1. Both have to read as on, or a
// document or a code path that set the other one would silently show nothing.
TEST(HeartIndicator, AnyNonZeroFlagCountsAsOn) {
    for (unsigned flag = 1; flag <= 0xFF; ++flag) {
        EXPECT_TRUE(heartIndicatorShown(GameState::TYPE_A_LEVEL_SELECTION,
                                        static_cast<std::uint8_t>(flag)))
            << "flag 0x" << std::hex << flag;
    }
    EXPECT_FALSE(heartIndicatorShown(GameState::TYPE_A_LEVEL_SELECTION, 0));
}

// ── Test 3: SpriteFollowsTheHeading ─────────────────────────────────────────────────────────────────
// The glyph, the art it is taken from, the palette it is drawn through, and where it sits. The place
// is asserted against the heading's own published cells rather than against a pair of numbers, so
// moving the heading moves this assertion with it — which is the point of reading them.
TEST(HeartIndicator, SpriteFollowsTheHeading) {
    const auto sprite =
        heartIndicatorSprite(GameState::TYPE_B_LEVEL_SELECTION, kOn, kAtlas, kRamp);
    ASSERT_TRUE(sprite.has_value());

    // $27 is the first tile of the block the gameplay regime carries over from the copyright-and-title
    // art, so the heart resolves to that sheet's cell 0 — the same picture the panel draws beside the
    // level digit during a round.
    const kirpich::render::ResolvedTile expected = kirpich::render::resolveSpriteTile(
        static_cast<std::uint8_t>(kirpich::CharTile::HEART), kirpich::TileSheet::GAMEPLAY,
        /*palette1=*/false, kAtlas, kRamp);
    EXPECT_EQ(sprite->atlas, expected.atlas);
    EXPECT_EQ(sprite->tile, expected.cell);
    EXPECT_EQ(sprite->palette, expected.palette);

    // Drawn through an OBJECT palette, whose lightest shade is see-through. A background palette would
    // paint out the cell it sits in.
    EXPECT_EQ(sprite->palette, spritePaletteFor(kRamp));
    EXPECT_EQ(sprite->atlas, kAtlas.copyrightTitle);

    // One cell past the heading's last, on the heading's own row.
    constexpr int kCell = 8;
    EXPECT_EQ(sprite->x,
              static_cast<int>(kirpich::systems::kDifficultyHeadingCol +
                               kirpich::systems::kDifficultyHeadingCols) *
                      kCell +
                  kirpich::render::kHeartIndicatorXOffset);
    EXPECT_EQ(sprite->y, static_cast<int>(kirpich::systems::kDifficultyHeadingRow) * kCell +
                             kirpich::render::kHeartIndicatorYOffset);

    // Past the heading, not over it.
    EXPECT_GE(sprite->x, static_cast<int>(kirpich::systems::kDifficultyHeadingCol +
                                          kirpich::systems::kDifficultyHeadingCols) *
                             kCell);
}

// ── Test 4: NothingDeclaredWhenItDoesNotBelong ──────────────────────────────────────────────────────
// The whole mechanism: a gated declaration is how the indicator leaves the screen. Nothing removes it,
// so nothing can forget to.
TEST(HeartIndicator, NothingDeclaredWhenItDoesNotBelong) {
    EXPECT_FALSE(
        heartIndicatorSprite(GameState::TYPE_A_LEVEL_SELECTION, kOff, kAtlas, kRamp).has_value());
    EXPECT_FALSE(heartIndicatorSprite(GameState::TITLE_SCREEN, kOn, kAtlas, kRamp).has_value());
    EXPECT_FALSE(heartIndicatorSprite(GameState::NORMAL_GAMEPLAY, kOn, kAtlas, kRamp).has_value());

    // Every state that does show one hands back exactly one sprite, and it is the same sprite on all
    // of them: the heading is in the same place on all three screens.
    const auto first = heartIndicatorSprite(kShowing[0], kOn, kAtlas, kRamp);
    ASSERT_TRUE(first.has_value());
    for (const GameState state : kShowing) {
        const auto sprite = heartIndicatorSprite(state, kOn, kAtlas, kRamp);
        ASSERT_TRUE(sprite.has_value());
        EXPECT_EQ(sprite->x, first->x);
        EXPECT_EQ(sprite->y, first->y);
        EXPECT_EQ(sprite->tile, first->tile);
    }
}

// ── Test 5: TheBackdropIsNotWrittenTo ───────────────────────────────────────────────────────────────
// The other half of the same mechanism: the indicator is drawn over the screen, never into it. Laying
// out a difficulty screen with heart mode on leaves the backdrop exactly as the stored tilemap has it,
// so nothing the player leaves behind has to be cleaned up on the way out.
TEST(HeartIndicator, TheBackdropIsNotWrittenTo) {
    kirpich::systems::GameContext game;
    game.flow.gameState = GameState::INIT_TYPE_A_DIFFICULTY;
    game.flow.heartMode = kOn;
    kirpich::systems::initTypeADifficultyScreen(game);

    for (std::size_t row = 0; row < kirpich::kTilemapScreenRows; ++row) {
        for (std::size_t col = 0; col < kirpich::kTilemapScreenCols; ++col) {
            EXPECT_EQ(game.display.map[row][col], kirpich::kTypeADifficultyTilemap[row][col])
                << "row " << row << " col " << col;
        }
    }
}
