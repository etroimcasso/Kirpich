// The carousel screen — behavioral tests over src/systems/carousel_screen.h, with the fixes
// instance's dispatch slots standing in for any instance's.
//
// Device-free: the handlers are pure logic over the game-state aggregate and the wiring's seams.
// The screen is the port's own, so every asserted value comes from the surface's stated contract.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/settings_overlay.h"
#include "render/tile_atlas.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/screen_ui_state.h"
#include "systems/carousel_screen.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::systems::CarouselOption;
using kirpich::systems::CarouselWiring;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

// The layout the screen draws, pinned here so a move shows up as a test change rather than silently.
constexpr std::size_t kTitleRow  = 2;
constexpr std::size_t kEnableRow = 5;  // the settings screen's own first scroller row
constexpr std::size_t kBodyFirst = 7;
constexpr std::size_t kLabelCol  = 3;
constexpr std::size_t kValueCol  = kirpich::systems::kOptionValueCol;
constexpr std::size_t kCursorCol = 1;

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

void expectGlyphs(const BackgroundMap& map, std::size_t row, std::size_t col,
                  std::initializer_list<CharTile> expected) {
    std::size_t i = 0;
    for (const CharTile tile : expected) {
        EXPECT_EQ(map[row][col + i], static_cast<std::uint8_t>(tile))
            << "row " << row << " col " << (col + i);
        ++i;
    }
}

// A two-option instance whose flags and change count the tests can watch. The titles are chosen so
// the two screens differ at the title row's first written cell.
struct Probe {
    bool first  = false;
    bool second = false;
    int  changed = 0;

    static constexpr std::array<std::string_view, 2> kFirstBody{"one line", "two line"};
    static constexpr std::array<std::string_view, 1> kSecondBody{"other prose"};

    std::array<CarouselOption, 2> options{
        CarouselOption{.title = "alpha", .body = kFirstBody, .enabled = &first},
        CarouselOption{.title = "beta", .body = kSecondBody, .enabled = &second},
    };

    CarouselWiring wiring() {
        return CarouselWiring{.options = options, .changed = [this] { ++changed; }};
    }
};

// A context sitting on the carousel, entered through the instance's own init slot.
void open(GameContext& game, const CarouselWiring& wiring) {
    kirpich::systems::initCarouselScreen(game, wiring, GameState::FIXES_SCREEN);
}

// (1) The init paints the FIRST option's whole screen: its title centred at the title row, the
// enable row in the settings screen's scroller geometry with the value off, the body centred from
// its first row, the cursor on the enable row - and it starts at the first option even when a
// previous visit left the index elsewhere.
TEST(CarouselScreen, InitPaintsTheFirstOptionsScreen) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();

    game.screens.carouselOption = 1;  // a stale index from an earlier visit
    game.engine.oam[5]          = kirpich::OamEntry{.y = 0x40, .x = 0x40, .tile = 0x21};
    open(game, wiring);

    EXPECT_EQ(game.flow.gameState, GameState::FIXES_SCREEN);
    EXPECT_EQ(game.screens.carouselOption, 0u);

    using C = CharTile;
    const BackgroundMap& map = game.display.map;
    // "alpha" is five cells, centred in twenty: columns 7-11.
    expectGlyphs(map, kTitleRow, 7,
                 {C::LETTER_A, C::LETTER_L, C::LETTER_P, C::LETTER_H, C::LETTER_A});
    expectGlyphs(map, kEnableRow, kLabelCol,
                 {C::LETTER_E, C::LETTER_N, C::LETTER_A, C::LETTER_B, C::LETTER_L, C::LETTER_E});
    expectGlyphs(map, kEnableRow, kValueCol, {C::LETTER_O, C::LETTER_F, C::LETTER_F});
    // "one line" is eight cells, centred: columns 6-13.
    expectGlyphs(map, kBodyFirst, 6,
                 {C::LETTER_O, C::LETTER_N, C::LETTER_E, C::SPACE, C::LETTER_L, C::LETTER_I,
                  C::LETTER_N, C::LETTER_E});
    EXPECT_EQ(map[kEnableRow][kCursorCol], kCursor);

    // The buffer was emptied on the way in; what stands in it now is the enable row's right arrow
    // (the option is off, so only "on" is reachable) and nothing else.
    EXPECT_EQ(game.engine.oam[5], kirpich::OamEntry{});
    EXPECT_EQ(game.engine.oam[0], kirpich::OamEntry{}) << "off has no left arrow";
    EXPECT_NE(game.engine.oam[1], kirpich::OamEntry{}) << "on is reachable, so the right arrow is";
}

// (2) Right turns the shown option on and left turns it off - the flag, the sound, the seam, the
// value cells and the arrows all move together, and a press toward the value already held is an end
// stop that fires none of them.
TEST(CarouselScreen, LeftAndRightToggleTheShownOption) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    open(game, wiring);

    const SettingsWiring settings{};
    press(game, {Action::MenuRight});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_TRUE(probe.first);
    EXPECT_FALSE(probe.second) << "the option on show is the one that toggles";
    EXPECT_EQ(probe.changed, 1);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    using C = CharTile;
    expectGlyphs(game.display.map, kEnableRow, kValueCol, {C::LETTER_O, C::LETTER_N});
    EXPECT_EQ(game.display.map[kEnableRow][kValueCol + 2], kSpace)
        << "the value field's third cell is blanked, or off would show through as onf";
    EXPECT_NE(game.engine.oam[0], kirpich::OamEntry{}) << "off is reachable now";
    EXPECT_EQ(game.engine.oam[1], kirpich::OamEntry{}) << "and on is not";

    // The end stop: already on, right says and does nothing.
    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuRight});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_TRUE(probe.first);
    EXPECT_EQ(probe.changed, 1);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuLeft});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_FALSE(probe.first);
    EXPECT_EQ(probe.changed, 2);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
}

// (3) Down moves to the next option and repaints the WHOLE screen - a different option is a
// different screen, so the new title and body stand where the old ones stood - and each end of the
// walk is an end stop that moves nothing and says nothing.
TEST(CarouselScreen, UpAndDownWalkTheOptions) {
    GameContext game;
    Probe       probe;
    probe.second       = true;  // so the move must repaint the value too
    const auto  wiring = probe.wiring();
    open(game, wiring);

    const SettingsWiring settings{};

    // The top end stop: up from the first option is a silent no-op.
    press(game, {Action::MenuUp});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_EQ(game.screens.carouselOption, 0u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuDown});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_EQ(game.screens.carouselOption, 1u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    using C = CharTile;
    const BackgroundMap& map = game.display.map;
    // "beta" is four cells, centred in twenty: columns 8-11 - and the cell "alpha" began in is
    // space again, which is what proves the title was repainted rather than overwritten in place.
    expectGlyphs(map, kTitleRow, 8, {C::LETTER_B, C::LETTER_E, C::LETTER_T, C::LETTER_A});
    EXPECT_EQ(map[kTitleRow][7], kSpace);
    // The second option's body stands where the first one's stood...
    expectGlyphs(map, kBodyFirst, 4,
                 {C::LETTER_O, C::LETTER_T, C::LETTER_H, C::LETTER_E, C::LETTER_R, C::SPACE,
                  C::LETTER_P, C::LETTER_R, C::LETTER_O, C::LETTER_S, C::LETTER_E});
    // ...and its own value: the second flag was seeded on.
    expectGlyphs(map, kEnableRow, kValueCol, {C::LETTER_O, C::LETTER_N});
    EXPECT_EQ(map[kBodyFirst + 1][kLabelCol], kSpace)
        << "the first option's second body line is gone";

    // The bottom end stop.
    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuDown});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_EQ(game.screens.carouselOption, 1u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    // And back up, with the first option's screen returning whole.
    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuUp});
    kirpich::systems::carouselScreen(game, wiring, settings);
    EXPECT_EQ(game.screens.carouselOption, 0u);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    expectGlyphs(map, kTitleRow, 7,
                 {C::LETTER_A, C::LETTER_L, C::LETTER_P, C::LETTER_H, C::LETTER_A});
}

// (4) Left and right act on the option ON SHOW: after a move, the toggle reaches the second flag
// and leaves the first alone.
TEST(CarouselScreen, TheToggleFollowsTheWalk) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    open(game, wiring);

    const SettingsWiring settings{};
    press(game, {Action::MenuDown});
    kirpich::systems::carouselScreen(game, wiring, settings);
    press(game, {Action::MenuRight});
    kirpich::systems::carouselScreen(game, wiring, settings);

    EXPECT_FALSE(probe.first);
    EXPECT_TRUE(probe.second);
    EXPECT_EQ(probe.changed, 1);
}

// (5) The option arrows follow the walk under the range-end law, and they are the game's own
// selector stood on end - up ABOVE the title (outside what the option owns, so it cannot read as
// the description scrolling) and down below the body. With one option neither is drawn.
TEST(CarouselScreen, OptionArrowsFollowTheRangeEndLaw) {
    kirpich::render::TileAtlas atlas;
    atlas.copyrightTitle = static_cast<retropp::AtlasId>(22);
    for (std::size_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
        atlas.palettes[ramp].sprite0 = static_cast<retropp::PaletteId>(70 + ramp);
    }
    const auto expected = kirpich::render::resolveSpriteTile(
        0x58, kirpich::TileSheet::COPYRIGHT_TITLE, false, atlas, 0);

    kirpich::ScreenUiState ui;

    // One option: nowhere to go, nothing drawn.
    ui.carouselOption = 0;
    EXPECT_TRUE(kirpich::render::carouselArrows(ui, 0, atlas, 1).empty());

    // The first of three: only down, below the body.
    {
        const auto arrows = kirpich::render::carouselArrows(ui, 0, atlas, 3);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].tile, expected.cell) << "the game's own selector, not a new shape";
        EXPECT_EQ(arrows[0].rotation, retropp::Rotation::Rot90);
        EXPECT_EQ(arrows[0].y, static_cast<int>(kirpich::systems::kPageDownArrowRow) * 8);
    }

    // The middle: both, with up standing ABOVE the title row.
    ui.carouselOption = 1;
    {
        const auto arrows = kirpich::render::carouselArrows(ui, 0, atlas, 3);
        ASSERT_EQ(arrows.size(), 2u);
        EXPECT_EQ(arrows[0].rotation, retropp::Rotation::Rot270);
        EXPECT_EQ(arrows[0].y, static_cast<int>(kirpich::systems::kPageUpArrowRow) * 8);
        EXPECT_LT(arrows[0].y, static_cast<int>(kTitleRow) * 8)
            << "the up arrow sits above the title, or it reads as the description scrolling";
        EXPECT_EQ(arrows[1].rotation, retropp::Rotation::Rot90);
    }

    // The last: only up.
    ui.carouselOption = 2;
    {
        const auto arrows = kirpich::render::carouselArrows(ui, 0, atlas, 3);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].rotation, retropp::Rotation::Rot270);
    }

    // Whichever ramp is on, an arrow is coloured by that ramp's object palette.
    for (std::uint8_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
        const auto arrows = kirpich::render::carouselArrows(ui, ramp, atlas, 3);
        ASSERT_EQ(arrows.size(), 1u);
        EXPECT_EQ(arrows[0].palette, static_cast<retropp::PaletteId>(70 + ramp)) << "ramp " << +ramp;
    }
}

// (6) B goes back to the settings screen, which repaints itself over this one.
TEST(CarouselScreen, BackReturnsToTheSettingsScreen) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();

    // Enter the way a player does, so the settings screen has a caller to return toward.
    kirpich::Settings    settings;
    const SettingsWiring settingsWiring{.settings = &settings};
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, settingsWiring);
    open(game, wiring);

    press(game, {Action::Back});
    kirpich::systems::carouselScreen(game, wiring, settingsWiring);

    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    using C = CharTile;
    // The settings header stands where the carousel's screen was: "settings 1" centred at row 2.
    expectGlyphs(game.display.map, 2, 5,
                 {C::LETTER_S, C::LETTER_E, C::LETTER_T, C::LETTER_T, C::LETTER_I, C::LETTER_N,
                  C::LETTER_G, C::LETTER_S});
}

// (7) Each screen-opening row on the settings screen's second page opens its own screen's init slot
// - by the right arrow it carries, and by Confirm and Start - and each lands on its own: the ghost
// row on the ghost screen, the fixes row on the fixes screen, and the new-modes row on the mode
// screen it has always opened.
TEST(CarouselScreen, TheOpeningRowsEachOpenTheirOwnScreen) {
    kirpich::Settings    settings;
    const SettingsWiring settingsWiring{.settings = &settings};

    struct Opener {
        kirpich::SettingsRow row;
        GameState            init;
    };
    for (const Opener opener : {Opener{kirpich::SettingsRow::GHOST_PIECE,
                                       GameState::INIT_GHOST_SCREEN},
                                Opener{kirpich::SettingsRow::FIXES, GameState::INIT_FIXES_SCREEN},
                                Opener{kirpich::SettingsRow::NEW_MODES,
                                       GameState::INIT_MODE_SCREEN}}) {
        for (const Action action : {Action::MenuRight, Action::Confirm, Action::Start}) {
            GameContext game;
            game.flow.gameState = GameState::TITLE_SCREEN;
            kirpich::systems::openSettings(game);
            kirpich::systems::initSettingsScreen(game, settingsWiring);

            game.screens.settingsRow = opener.row;
            press(game, {action});
            kirpich::systems::settingsScreen(game, settingsWiring);

            EXPECT_EQ(game.flow.gameState, opener.init)
                << "row " << static_cast<int>(opener.row) << " action "
                << static_cast<int>(action);
            EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
        }
    }
}

}  // namespace
