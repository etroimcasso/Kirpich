// The Display settings screen — behavioral tests over its logic (src/systems/display_settings_screen.h)
// and its components (src/render/display_settings/, src/render/option_row.h,
// src/render/scroller_arrows.h).
//
// Device-free. The logic is pure state over the game aggregate and the wiring's seams, and the
// components are pure functions returning primitives, so the atlas here is a plain value with
// distinguishable handles rather than anything uploaded. The screen is the port's own, so every
// asserted value comes from its stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/display_settings/layout.h"
#include "render/display_settings/screen.h"
#include "render/glyphs.h"
#include "render/palettes.h"
#include "render/scroller_arrows.h"
#include "retropp/input.h"
#include "state/display_settings_state.h"
#include "state/settings.h"
#include "systems/display_settings_screen.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::DisplaySettingsRow;
using kirpich::GameState;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::render::TileAtlas;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;

namespace layout = kirpich::render::display_settings;

constexpr std::uint8_t kPlayerRamp = 7;

// The interval the game's own selection screens blink their cursors on (tetris.asm:3597-3608).
constexpr std::uint8_t kBlinkFrames = 16;

// The first settings page's own cells, pinned here so the screen is held to them rather than to its
// own layout header: the label on column 3, the value on column 15, the arrows on 13 and 19, the
// rows on lines 5 and 8, the heading on line 2.
constexpr int kLabelX      = 3 * 8;
constexpr int kValueX      = 15 * 8;
constexpr int kLeftArrowX  = 13 * 8;
constexpr int kRightArrowX = 19 * 8;
constexpr int kCursorX     = 1 * 8;
constexpr int kFirstLineY  = 5 * 8;
constexpr int kSecondLineY = 8 * 8;
constexpr int kHeadingY    = 2 * 8;
constexpr int kHeadingX    = 6 * 8;  // "display" - seven cells, centered in twenty

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
        atlas.palettes[ramp].font          = id(1);
        atlas.palettes[ramp].content       = id(2);
        atlas.palettes[ramp].fontDim       = id(3);
        atlas.palettes[ramp].fontSprite    = id(4);
        atlas.palettes[ramp].sprite0       = id(5);
        atlas.palettes[ramp].sprite1       = id(6);
        atlas.palettes[ramp].fontSpriteDim = id(7);
        atlas.palettes[ramp].spriteDim     = id(8);
    }
    return atlas;
}

const TileAtlas kAtlas = makeAtlas();

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// A one-frame press: the handlers read the pressed edge, which the dispatcher normally derives.
void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held    = actionSet(as);
    game.audioCues      = kirpich::systems::AudioCues{};
}

// The wiring plus the counts its seams record, so a test can ask what fired.
struct Probe {
    Settings settings{.shadeRamp = kPlayerRamp};
    Settings lastApplied{};
    int      applied = 0;
    int      saved   = 0;

    SettingsWiring wiring() {
        return SettingsWiring{
            .settings = &settings,
            .apply    = [this](const Settings& s) { ++applied; lastApplied = s; },
            .save     = [this](const Settings&) { ++saved; },
        };
    }
};

// The settings screen opened from the title screen, with the cursor on its Display row and the
// Display screen opened from it.
GameContext openDisplay(const SettingsWiring& wiring) {
    GameContext game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
    game.screens.settingsRow = SettingsRow::DISPLAY;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, wiring);
    return game;
}

std::span<const retropp::Sprite> contentOf(const kirpich::render::Layers& layers) {
    return std::get<retropp::SpriteContent>(layers[1].content).sprites;
}

std::optional<retropp::Sprite> named(std::span<const retropp::Sprite> sprites,
                                     std::string_view key) {
    for (const retropp::Sprite& s : sprites) {
        if (s.key.value == key) return s;
    }
    return std::nullopt;
}

// Whether the content draws `text` from (x, y), glyph for glyph, and nothing in the cell after it.
void expectText(std::span<const retropp::Sprite> content, std::string_view text, int x, int y) {
    const kirpich::render::Sprites expected =
        kirpich::render::Glyphs(text, x, y, 8, kAtlas, kPlayerRamp);
    ASSERT_FALSE(expected.empty()) << text;
    for (const retropp::Sprite& want : expected) {
        const auto got = named(content, want.key.value);
        ASSERT_TRUE(got.has_value()) << "\"" << text << "\" is missing a glyph at " << want.x;
        EXPECT_EQ(got->tile, want.tile) << "\"" << text << "\" at x " << want.x;
        EXPECT_EQ(got->y, y);
    }
    const int after = x + static_cast<int>(text.size()) * 8;
    EXPECT_FALSE(named(content, "glyph-" + std::to_string(after) + "-" + std::to_string(y)))
        << "something follows \"" << text << "\"";
}

kirpich::render::Layers draw(const Settings& s, DisplaySettingsRow row = DisplaySettingsRow::FULLSCREEN,
                             bool blinkOn = true) {
    const kirpich::DisplaySettingsState ui{.row = row};
    return kirpich::render::DisplaySettingsScreen(ui, s, blinkOn, kAtlas);
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// Opening puts the cursor on the first row whatever the last visit left, shows it, and arms the blink.
TEST(DisplaySettingsScreen, OpeningResetsTheRowAndArmsTheBlink) {
    GameContext game;
    game.displaySettings.row   = DisplaySettingsRow::WINDOW_SCALE;
    game.screens.cursorVisible = false;

    kirpich::systems::openDisplaySettings(game);

    EXPECT_EQ(game.displaySettings.row, DisplaySettingsRow::FULLSCREEN);
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);
    EXPECT_EQ(game.flow.gameState, GameState::DISPLAY_SETTINGS);
}

// The cursor walks the two rows and stops at both ends. A move cues the menu move; an end stop moves
// nothing and says nothing.
TEST(DisplaySettingsScreen, TheCursorWalksBothRowsAndStopsAtEachEnd) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openDisplay(wiring);

    press(game, {Action::MenuUp});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(game.displaySettings.row, DisplaySettingsRow::FULLSCREEN);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the top is an end stop";

    press(game, {Action::MenuDown});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(game.displaySettings.row, DisplaySettingsRow::WINDOW_SCALE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    press(game, {Action::MenuDown});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(game.displaySettings.row, DisplaySettingsRow::WINDOW_SCALE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the bottom is an end stop";

    press(game, {Action::MenuUp});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(game.displaySettings.row, DisplaySettingsRow::FULLSCREEN);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    EXPECT_EQ(probe.applied, 0) << "walking the rows changes no setting";
}

// Right turns fullscreen on and left turns it off, each change cued and reaching both seams in order.
// Pressing into the value already held is an end stop and fires nothing.
TEST(DisplaySettingsScreen, FullscreenTogglesAndFiresBothSeams) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openDisplay(wiring);

    press(game, {Action::MenuRight});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_TRUE(probe.settings.fullscreen);
    EXPECT_TRUE(probe.lastApplied.fullscreen);
    EXPECT_EQ(probe.applied, 1);
    EXPECT_EQ(probe.saved, 1);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    press(game, {Action::MenuRight});  // already on
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(probe.applied, 1) << "an end stop must not re-apply";
    EXPECT_EQ(probe.saved, 1) << "an end stop must not re-save";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    press(game, {Action::MenuLeft});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_FALSE(probe.settings.fullscreen);
    EXPECT_EQ(probe.applied, 2);
    EXPECT_EQ(probe.saved, 2);

    EXPECT_EQ(probe.settings.windowScale, kirpich::kDefaultWindowScale)
        << "the fullscreen row leaves the size alone";
}

// The size row steps through every scale the build offers, both ways, and stops at both ends.
TEST(DisplaySettingsScreen, SizeStepsEveryScaleAndStopsAtBothEnds) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openDisplay(wiring);

    press(game, {Action::MenuDown});
    kirpich::systems::displaySettingsScreen(game, wiring);
    ASSERT_EQ(game.displaySettings.row, DisplaySettingsRow::WINDOW_SCALE);

    for (int scale = kirpich::kDefaultWindowScale; scale < kirpich::kMaxWindowScale; ++scale) {
        press(game, {Action::MenuRight});
        kirpich::systems::displaySettingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.windowScale, scale + 1);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    }
    const int appliedAtCeiling = probe.applied;
    press(game, {Action::MenuRight});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.windowScale, kirpich::kMaxWindowScale);
    EXPECT_EQ(probe.applied, appliedAtCeiling) << "the ceiling is an end stop";

    for (int scale = kirpich::kMaxWindowScale; scale > kirpich::kMinWindowScale; --scale) {
        press(game, {Action::MenuLeft});
        kirpich::systems::displaySettingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.windowScale, scale - 1);
    }
    const int appliedAtFloor = probe.applied;
    press(game, {Action::MenuLeft});
    kirpich::systems::displaySettingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.windowScale, kirpich::kMinWindowScale);
    EXPECT_EQ(probe.applied, appliedAtFloor) << "the floor is an end stop";
    EXPECT_FALSE(probe.settings.fullscreen) << "the size row leaves fullscreen alone";
}

// B goes back to the settings screen, painted again, with its cursor on the Display row.
TEST(DisplaySettingsScreen, BackReturnsToTheSettingsScreenOnItsRow) {
    Probe       probe;
    const auto  wiring = probe.wiring();
    GameContext game   = openDisplay(wiring);
    ASSERT_EQ(game.flow.gameState, GameState::DISPLAY_SETTINGS);

    press(game, {Action::MenuDown});
    kirpich::systems::displaySettingsScreen(game, wiring);
    press(game, {Action::Back});
    kirpich::systems::displaySettingsScreen(game, wiring);

    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::DISPLAY);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    const std::size_t line = kirpich::systems::settingsRowLine(SettingsRow::DISPLAY);
    EXPECT_EQ(game.display.map[line][kirpich::systems::kCursorCol],
              static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
    EXPECT_EQ(game.display.map[line][kirpich::systems::kLabelCol],
              static_cast<std::uint8_t>(kirpich::CharTile::LETTER_D));
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

TEST(DisplaySettingsScreen, TheScreenIsABackdropAndItsContent) {
    const kirpich::render::Layers layers = draw(Settings{.shadeRamp = kPlayerRamp});

    ASSERT_EQ(layers.size(), 2u) << "a backdrop of tiles, and everything else over it";
    EXPECT_EQ(layers[0].z, 0);
    EXPECT_LT(layers[0].z, layers[1].z) << "the content draws over the backdrop";
    EXPECT_TRUE(std::holds_alternative<retropp::TileContent>(layers[0].content));
    EXPECT_TRUE(std::holds_alternative<retropp::SpriteContent>(layers[1].content));
}

// The heading, the labels and the values stand on the first settings page's own cells, so the screen
// reads as that page does.
TEST(DisplaySettingsScreen, TheTextStandsOnTheSettingsPagesCells) {
    const kirpich::render::Layers layers =
        draw(Settings{.fullscreen = false, .windowScale = 4, .shadeRamp = kPlayerRamp});
    const auto content = contentOf(layers);

    expectText(content, "display", kHeadingX, kHeadingY);
    expectText(content, "fullscreen", kLabelX, kFirstLineY);
    expectText(content, "off", kValueX, kFirstLineY);
    expectText(content, "size", kLabelX, kSecondLineY);
    expectText(content, "4x", kValueX, kSecondLineY);

    EXPECT_EQ(layout::kHeadingX, kHeadingX);
    EXPECT_EQ(layout::kHeadingY, kHeadingY);
}

// An arrow stands where the value can still move and nowhere else: fullscreen off has only its right
// arrow, on only its left; the size has both between its ends and loses one at each end. The left
// arrow is the selector flipped.
TEST(DisplaySettingsScreen, ArrowsStandOnlyWhereTheValueCanMove) {
    const auto selector = kirpich::render::resolveSpriteTile(
        kirpich::render::kSelectorTile, kirpich::TileSheet::COPYRIGHT_TITLE, false, kAtlas,
        kPlayerRamp);

    const auto arrows = [](const Settings& s, int line) {
        const auto content = contentOf(draw(s));
        return std::pair{named(content, "scroller-" + std::to_string(line) + "-left"),
                         named(content, "scroller-" + std::to_string(line) + "-right")};
    };

    auto [left, right] = arrows(Settings{.fullscreen = false, .shadeRamp = kPlayerRamp}, 5);
    EXPECT_FALSE(left) << "off cannot go further off";
    ASSERT_TRUE(right);
    EXPECT_EQ(right->tile, selector.cell);
    EXPECT_EQ(right->x, kRightArrowX);
    EXPECT_EQ(right->y, kFirstLineY);
    EXPECT_FALSE(right->flipX);

    std::tie(left, right) = arrows(Settings{.fullscreen = true, .shadeRamp = kPlayerRamp}, 5);
    ASSERT_TRUE(left);
    EXPECT_FALSE(right) << "on cannot go further on";
    EXPECT_EQ(left->tile, selector.cell);
    EXPECT_EQ(left->x, kLeftArrowX);
    EXPECT_TRUE(left->flipX) << "the left arrow is the selector flipped, not a second tile";

    std::tie(left, right) =
        arrows(Settings{.windowScale = kirpich::kMinWindowScale, .shadeRamp = kPlayerRamp}, 8);
    EXPECT_FALSE(left);
    EXPECT_TRUE(right);

    std::tie(left, right) =
        arrows(Settings{.windowScale = kirpich::kMaxWindowScale, .shadeRamp = kPlayerRamp}, 8);
    EXPECT_TRUE(left);
    EXPECT_FALSE(right);

    std::tie(left, right) = arrows(
        Settings{.windowScale = kirpich::kMinWindowScale + 1, .shadeRamp = kPlayerRamp}, 8);
    EXPECT_TRUE(left);
    EXPECT_TRUE(right);
}

// The values are read from the settings the screen is handed each frame, so a change made from
// outside the screen - the fullscreen shortcut - is what the screen shows.
TEST(DisplaySettingsScreen, TheScreenDrawsTheSettingsItIsHanded) {
    Settings s{.fullscreen = false, .shadeRamp = kPlayerRamp};
    expectText(contentOf(draw(s)), "off", kValueX, kFirstLineY);

    s.fullscreen = true;
    expectText(contentOf(draw(s)), "on", kValueX, kFirstLineY);
}

// The cursor is a hyphen on column 1 of the row it is on, and it is gone while the blink is off.
TEST(DisplaySettingsScreen, TheCursorMarksItsRowAndIsGoneWithTheBlinkOff) {
    const Settings s{.shadeRamp = kPlayerRamp};
    const auto     hyphen = kirpich::render::Glyphs("-", kCursorX, kFirstLineY, 8, kAtlas,
                                                    kPlayerRamp)
                            .front()
                            .tile;

    const auto cursorAt = [](const kirpich::render::Layers& layers, int y) {
        return named(contentOf(layers), "glyph-" + std::to_string(kCursorX) + "-" + std::to_string(y));
    };

    auto on = draw(s, DisplaySettingsRow::FULLSCREEN, true);
    ASSERT_TRUE(cursorAt(on, kFirstLineY));
    EXPECT_EQ(cursorAt(on, kFirstLineY)->tile, hyphen);
    EXPECT_FALSE(cursorAt(on, kSecondLineY));

    on = draw(s, DisplaySettingsRow::WINDOW_SCALE, true);
    EXPECT_FALSE(cursorAt(on, kFirstLineY));
    EXPECT_TRUE(cursorAt(on, kSecondLineY));

    const auto off = draw(s, DisplaySettingsRow::WINDOW_SCALE, false);
    EXPECT_FALSE(cursorAt(off, kFirstLineY));
    EXPECT_FALSE(cursorAt(off, kSecondLineY));
}

// Within one frame no two objects share a name, which is what the renderer requires.
TEST(DisplaySettingsScreen, NoTwoObjectsInAFrameShareAName) {
    for (const bool fullscreen : {false, true}) {
        for (const DisplaySettingsRow row :
             {DisplaySettingsRow::FULLSCREEN, DisplaySettingsRow::WINDOW_SCALE}) {
            const auto layers = draw(Settings{.fullscreen  = fullscreen,
                                              .windowScale = kirpich::kMinWindowScale + 1,
                                              .shadeRamp   = kPlayerRamp},
                                     row, true);
            std::vector<std::string> keys;
            for (const retropp::Sprite& s : contentOf(layers)) {
                keys.push_back(s.key.value);
            }
            for (std::size_t i = 0; i < keys.size(); ++i) {
                for (std::size_t j = i + 1; j < keys.size(); ++j) {
                    EXPECT_NE(keys[i], keys[j]) << "two objects are named " << keys[i];
                }
            }
        }
    }
}
