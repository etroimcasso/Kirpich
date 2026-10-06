// The Controls screen — behavioral tests over its logic (src/systems/controls_screen.h) and its
// components (src/render/controls/).
//
// Device-free. The platform's capture is a fake that holds one answer until it is asked again, as the
// engine's does, and the frames that need the game's own press edge run through the real input system
// and the real action map the bindings derive. The screen is the port's own, so every asserted value
// comes from its stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/controls/layout.h"
#include "render/controls/names.h"
#include "render/controls/screen.h"
#include "render/glyphs.h"
#include "render/palettes.h"
#include "retropp/input.h"
#include "retropp/input_actions.h"
#include "state/controls.h"
#include "state/controls_screen_state.h"
#include "state/settings.h"
#include "systems/controls.h"
#include "systems/controls_screen.h"
#include "systems/game_context.h"
#include "systems/input.h"
#include "systems/settings_screen.h"

namespace {

using kirpich::Action;
using kirpich::Controls;
using kirpich::ControlsColumn;
using kirpich::ControlsRow;
using kirpich::ControlsScreenState;
using kirpich::GameState;
using kirpich::GbButton;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::render::TileAtlas;
using kirpich::systems::ControlsWiring;
using kirpich::systems::GameContext;
using kirpich::systems::SettingsWiring;
using retropp::ControllerType;
using retropp::PadButton;

constexpr std::uint8_t kPlayerRamp = 7;

// The screen's cells, pinned here so the screen is held to them rather than to its own layout header:
// the heading on line 2, the column heads on line 4, the buttons on lines 5 to 12, the labels from
// column 1, the keyboard cursor on 8 and its names from 9, the controller cursor on 14 and its names
// from 15, the restore row on line 14 with its label from column 3 and its cursor on 1, the prompt on
// line 16 and the cancel hint on line 17.
constexpr int kHeadingX       = 6 * 8;  // "controls" - eight cells, centered in twenty
constexpr int kHeadingY       = 2 * 8;
constexpr int kColumnHeadY    = 4 * 8;
constexpr int kFirstButtonY   = 5 * 8;
constexpr int kLabelX         = 1 * 8;
constexpr int kKeyCursorX     = 8 * 8;
constexpr int kKeyNameX       = 9 * 8;
constexpr int kPadCursorX     = 14 * 8;
constexpr int kPadNameX       = 15 * 8;
constexpr int kRestoreY       = 14 * 8;
constexpr int kRestoreLabelX  = 3 * 8;
constexpr int kRestoreCursorX = 1 * 8;
constexpr int kPromptY        = 16 * 8;
constexpr int kCancelHintY    = 17 * 8;
constexpr int kScreenWidth    = 160;
constexpr int kScreenHeight   = 144;

int rowY(GbButton button) {
    return kFirstButtonY + 8 * static_cast<int>(button);
}

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

// The actions the game holds while `keys` are down under `controls`: every action the derived map
// binds to one of them.
retropp::ActionSet heldUnder(const Controls& controls, std::initializer_list<SDL_Scancode> keys) {
    retropp::ActionSet held;
    const retropp::ActionMap map = kirpich::systems::actionMapFor(controls);
    for (const retropp::ActionBinding& row : map.rows()) {
        if (row.source.kind != retropp::Source::Kind::Key) continue;
        for (const SDL_Scancode key : keys) {
            if (row.source.key == key) held.set(row.action, true);
        }
    }
    return held;
}

retropp::CapturedSource keyPress(SDL_Scancode key) {
    return retropp::CapturedSource{
        .source = retropp::Source{key},
        .device = {.kind = retropp::DeviceKind::KeyboardMouse},
    };
}

retropp::CapturedSource padPress(PadButton position, ControllerType family) {
    return retropp::CapturedSource{
        .source = retropp::Source{position},
        .device = {.kind = retropp::DeviceKind::Gamepad, .family = family},
    };
}

retropp::CapturedSource mousePress() {
    return retropp::CapturedSource{
        .source = retropp::Source{retropp::MouseButton::Left},
        .device = {.kind = retropp::DeviceKind::KeyboardMouse},
    };
}

// The two wirings plus what their seams record. The capture holds one answer until the screen asks
// for another press, which clears it - the engine's behavior.
struct Probe {
    Settings settings{.shadeRamp = kPlayerRamp};
    Controls controls = kirpich::kDefaultControls;

    std::optional<retropp::CapturedSource> answer;
    int                                    listens = 0;
    std::vector<std::string>               log;  // "apply" and "save", in the order they fire
    Controls                               lastApplied{};
    Controls                               lastSaved{};

    SettingsWiring settingsWiring() {
        return SettingsWiring{.settings = &settings};
    }

    ControlsWiring controlsWiring() {
        return ControlsWiring{
            .controls = &controls,
            .listen   = [this] { ++listens; answer.reset(); },
            .captured = [this] { return answer; },
            .apply    = [this](const Controls& c) { log.push_back("apply"); lastApplied = c; },
            .save     = [this](const Controls& c) { log.push_back("save"); lastSaved = c; },
        };
    }
};

// One frame of the screen with the actions given pressed.
void frame(GameContext& game, Probe& probe, std::initializer_list<Action> as) {
    press(game, as);
    kirpich::systems::controlsScreen(game, probe.settingsWiring(), probe.controlsWiring());
}

// The settings screen opened from the title screen, with the Controls screen opened from its
// Controls row.
GameContext openControls(Probe& probe) {
    const SettingsWiring settings = probe.settingsWiring();
    GameContext          game;
    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, settings);
    game.screens.settingsRow = SettingsRow::CONTROLS;
    press(game, {Action::Confirm});
    kirpich::systems::settingsScreen(game, settings);
    return game;
}

// The screen waiting on the cell (row, column), with nothing captured yet.
GameContext listeningOn(Probe& probe, GbButton row, ControlsColumn column) {
    GameContext game          = openControls(probe);
    game.controlsScreen.row    = kirpich::rowOf(row);
    game.controlsScreen.column = column;
    frame(game, probe, {Action::Confirm});
    return game;
}

kirpich::render::Layers draw(const ControlsScreenState& ui, const Controls& controls,
                             ControllerType family = ControllerType::Standard,
                             bool blinkOn = true) {
    return kirpich::render::ControlsScreen(ui, controls, family, blinkOn, kAtlas, kPlayerRamp);
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

std::optional<retropp::Sprite> glyphAt(std::span<const retropp::Sprite> sprites, int x, int y) {
    return named(sprites, "glyph-" + std::to_string(x) + "-" + std::to_string(y));
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
    EXPECT_FALSE(glyphAt(content, after, y)) << "something follows \"" << text << "\"";
}

// Every character a name may hold: what the font draws.
bool drawable(std::string_view text) {
    for (const char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

// ── The logic ─────────────────────────────────────────────────────────────────────────────────────

// Opening puts the cursor on the first cell, idle, whatever the last visit left, shows it, and arms
// the blink.
TEST(ControlsScreen, OpeningResetsTheScreenAndArmsTheBlink) {
    GameContext game;
    game.controlsScreen = ControlsScreenState{.row             = ControlsRow::START,
                                              .column          = ControlsColumn::CONTROLLER,
                                              .listening       = true,
                                              .awaitingRelease = true};
    game.screens.cursorVisible = false;

    kirpich::systems::openControlsSettings(game);

    EXPECT_EQ(game.controlsScreen, ControlsScreenState{});
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::UP);
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::KEYBOARD);
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kirpich::systems::kScreenBlinkFrames);
    EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS);
}

// The cursor walks the eight button rows and the restore row under them, and the two columns, and
// stops at every edge. A move cues the menu move; an edge moves nothing and says nothing. The restore
// row is one item, so Left and Right are edges on it, and the column a button row was on comes back
// when the cursor leaves it.
TEST(ControlsScreen, TheCursorWalksEveryCellAndStopsAtEachEdge) {
    Probe       probe;
    GameContext game = openControls(probe);

    frame(game, probe, {Action::MenuUp});
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::UP);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the top is an edge";

    for (std::size_t i = 1; i < kirpich::kControlsRowCount; ++i) {
        frame(game, probe, {Action::MenuDown});
        EXPECT_EQ(game.controlsScreen.row, static_cast<ControlsRow>(i));
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    }
    ASSERT_EQ(game.controlsScreen.row, ControlsRow::RESTORE_DEFAULTS);
    frame(game, probe, {Action::MenuDown});
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::RESTORE_DEFAULTS);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the bottom is an edge";

    for (const Action side : {Action::MenuRight, Action::MenuLeft}) {
        frame(game, probe, {side});
        EXPECT_EQ(game.controlsScreen.column, ControlsColumn::KEYBOARD);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE)
            << "the restore row has no columns to move between";
    }

    frame(game, probe, {Action::MenuUp});
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::SELECT);

    frame(game, probe, {Action::MenuLeft});
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::KEYBOARD);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the left is an edge";

    frame(game, probe, {Action::MenuRight});
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::CONTROLLER);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    frame(game, probe, {Action::MenuRight});
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::CONTROLLER);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "the right is an edge";

    frame(game, probe, {Action::MenuLeft});
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::KEYBOARD);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    // Through the restore row and back keeps the column.
    frame(game, probe, {Action::MenuRight});
    frame(game, probe, {Action::MenuDown});
    frame(game, probe, {Action::MenuUp});
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::SELECT);
    EXPECT_EQ(game.controlsScreen.column, ControlsColumn::CONTROLLER);

    EXPECT_EQ(probe.listens, 0) << "walking starts no wait";
    EXPECT_TRUE(probe.log.empty()) << "walking changes no binding";
}

// A and Start each start waiting for a press on the cell, asking the platform once. While the screen
// waits, none of its own actions does anything - not the arrows, not B, not another A - and the
// platform is not asked again.
TEST(ControlsScreen, ConfirmOrStartWaitsAndTheScreenIgnoresItsActionsWhileItWaits) {
    for (const Action open : {Action::Confirm, Action::Start}) {
        Probe       probe;
        GameContext game = openControls(probe);

        frame(game, probe, {open});
        EXPECT_TRUE(game.controlsScreen.listening);
        EXPECT_EQ(probe.listens, 1);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

        for (const Action act : {Action::MenuDown, Action::MenuRight, Action::Back,
                                 Action::Confirm, Action::Start}) {
            frame(game, probe, {act});
            EXPECT_TRUE(game.controlsScreen.listening) << "action " << int(act);
            EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS) << "action " << int(act);
        }
        EXPECT_EQ(game.controlsScreen.row, ControlsRow::UP);
        EXPECT_EQ(game.controlsScreen.column, ControlsColumn::KEYBOARD);
        EXPECT_EQ(probe.listens, 1);
        EXPECT_TRUE(probe.log.empty());
    }
}

// A key captured on the keyboard column is bound to the row's button. A key another button holds is
// swapped: the A row taking Z gives B the A row's old X. The bindings are put into effect and then
// saved, each once, the menu move is cued, and the wait ends.
TEST(ControlsScreen, AKeyIsBoundToTheRowAndSwapsWithItsHolder) {
    Probe       probe;
    GameContext game = listeningOn(probe, GbButton::A, ControlsColumn::KEYBOARD);

    probe.answer = keyPress(SDL_SCANCODE_Z);
    frame(game, probe, {});

    EXPECT_EQ(probe.controls[GbButton::A].key, SDL_SCANCODE_Z);
    EXPECT_EQ(probe.controls[GbButton::B].key, SDL_SCANCODE_X);
    EXPECT_EQ(probe.controls[GbButton::A].pad, kirpich::kDefaultControls[GbButton::A].pad)
        << "a key leaves the controller column alone";
    EXPECT_EQ(probe.log, (std::vector<std::string>{"apply", "save"}));
    EXPECT_EQ(probe.lastApplied, probe.controls);
    EXPECT_EQ(probe.lastSaved, probe.controls);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    EXPECT_FALSE(game.controlsScreen.listening);
    EXPECT_TRUE(game.controlsScreen.awaitingRelease);

    // A plain assignment: a key no button holds.
    game = listeningOn(probe, GbButton::START, ControlsColumn::KEYBOARD);
    probe.answer = keyPress(SDL_SCANCODE_SPACE);
    frame(game, probe, {});
    EXPECT_EQ(probe.controls[GbButton::START].key, SDL_SCANCODE_SPACE);
}

// A controller button captured on the controller column is bound with the family of the pad it came
// from. From the defaults, the B row taking a Nintendo pad's east button - the printed A there - swaps
// with the A row, and both now name positions.
TEST(ControlsScreen, AControllerButtonIsBoundWithItsPadsFamily) {
    Probe       probe;
    GameContext game = listeningOn(probe, GbButton::B, ControlsColumn::CONTROLLER);

    probe.answer = padPress(PadButton::FaceEast, ControllerType::Nintendo);
    frame(game, probe, {});

    EXPECT_EQ(probe.controls[GbButton::B].pad, PadButton::FaceEast);
    EXPECT_EQ(probe.controls[GbButton::A].pad, PadButton::FaceSouth);
    EXPECT_EQ(probe.controls[GbButton::B].key, kirpich::kDefaultControls[GbButton::B].key)
        << "a controller button leaves the keyboard column alone";
    EXPECT_EQ(probe.log, (std::vector<std::string>{"apply", "save"}));
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    EXPECT_FALSE(game.controlsScreen.listening);
}

// Escape ends the wait on either column with the bindings as they were: nothing put into effect,
// nothing saved, nothing cued.
TEST(ControlsScreen, EscapeCancelsOnEitherColumn) {
    for (const ControlsColumn column : {ControlsColumn::KEYBOARD, ControlsColumn::CONTROLLER}) {
        Probe       probe;
        GameContext game = listeningOn(probe, GbButton::A, column);

        probe.answer = keyPress(kirpich::kCancelKey);
        frame(game, probe, {});

        EXPECT_EQ(probe.controls, kirpich::kDefaultControls) << "column " << int(column);
        EXPECT_TRUE(probe.log.empty());
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);
        EXPECT_FALSE(game.controlsScreen.listening) << "column " << int(column);
        EXPECT_EQ(probe.listens, 1) << "a cancel does not ask for another press";
    }
}

// A press the cell cannot take - a controller button on the keyboard column, a key on the controller
// column, a mouse button on either - asks the platform for another press and keeps waiting, with the
// bindings untouched.
TEST(ControlsScreen, APressOfTheWrongKindKeepsTheScreenWaiting) {
    struct Case {
        ControlsColumn          column;
        retropp::CapturedSource press;
    };
    const Case cases[] = {
        {ControlsColumn::KEYBOARD, padPress(PadButton::FaceSouth, ControllerType::Xbox)},
        {ControlsColumn::KEYBOARD, mousePress()},
        {ControlsColumn::CONTROLLER, keyPress(SDL_SCANCODE_C)},
        {ControlsColumn::CONTROLLER, mousePress()},
    };
    for (const Case& c : cases) {
        Probe       probe;
        GameContext game = listeningOn(probe, GbButton::A, c.column);
        ASSERT_EQ(probe.listens, 1);

        probe.answer = c.press;
        frame(game, probe, {});

        EXPECT_TRUE(game.controlsScreen.listening) << "column " << int(c.column);
        EXPECT_EQ(probe.listens, 2) << "the platform is asked for another press";
        EXPECT_FALSE(probe.answer) << "so the press it caught is not read again";
        EXPECT_EQ(probe.controls, kirpich::kDefaultControls);
        EXPECT_TRUE(probe.log.empty());
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

        // And the next press of the right kind binds.
        probe.answer = c.column == ControlsColumn::KEYBOARD
                           ? keyPress(SDL_SCANCODE_C)
                           : padPress(PadButton::FaceNorth, ControllerType::Xbox);
        frame(game, probe, {});
        EXPECT_FALSE(game.controlsScreen.listening);
        EXPECT_EQ(probe.log, (std::vector<std::string>{"apply", "save"}));
    }
}

// The release guard, through the game's real press edge: each frame's held actions come from the keys
// down under the bindings as they stand, and the input system derives the press from them.
//
// The A row takes C. On the next frame C is still down, and under the new bindings it is A, so the
// game reads A as pressed - it was not held on the frame before. The screen must not take that as the
// player asking to bind the cell again. Once nothing is held, A works as it should.
TEST(ControlsScreen, TheKeyJustBoundDoesNotActOnTheScreenUntilItIsLetGo) {
    Probe                         probe;
    GameContext                   game = openControls(probe);
    kirpich::systems::InputSystem input;
    game.controlsScreen.row = ControlsRow::A;

    const auto step = [&](std::initializer_list<SDL_Scancode> keys) {
        game.joypad    = input.sample(heldUnder(probe.controls, keys));
        game.audioCues = kirpich::systems::AudioCues{};
        kirpich::systems::controlsScreen(game, probe.settingsWiring(), probe.controlsWiring());
    };

    step({SDL_SCANCODE_X});  // A, on the A row's cell: start waiting
    ASSERT_TRUE(game.controlsScreen.listening);
    ASSERT_EQ(probe.listens, 1);

    step({});
    probe.answer = keyPress(SDL_SCANCODE_C);
    step({SDL_SCANCODE_C});  // the press the platform caught
    ASSERT_EQ(probe.controls[GbButton::A].key, SDL_SCANCODE_C);
    ASSERT_FALSE(game.controlsScreen.listening);

    step({SDL_SCANCODE_C});  // still down, and A under the new bindings
    ASSERT_TRUE(game.joypad.pressed.test(retropp::actionId(Action::Confirm)))
        << "the game reads the held key as a fresh press of A";
    EXPECT_FALSE(game.controlsScreen.listening) << "the key just bound started another wait";
    EXPECT_EQ(probe.listens, 1);

    step({SDL_SCANCODE_C});
    EXPECT_FALSE(game.controlsScreen.listening);

    step({});  // let go
    EXPECT_FALSE(game.controlsScreen.awaitingRelease);

    step({SDL_SCANCODE_C});  // a real press of A
    EXPECT_TRUE(game.controlsScreen.listening);
    EXPECT_EQ(probe.listens, 2);
}

// The same guard where the bound key turns into B: the B row taking X gives the held X the meaning B,
// which would otherwise leave the screen the frame after the binding.
TEST(ControlsScreen, AKeyThatBecomesBDoesNotLeaveTheScreen) {
    Probe                         probe;
    GameContext                   game = openControls(probe);
    kirpich::systems::InputSystem input;
    game.controlsScreen.row = ControlsRow::B;

    const auto step = [&](std::initializer_list<SDL_Scancode> keys) {
        game.joypad    = input.sample(heldUnder(probe.controls, keys));
        game.audioCues = kirpich::systems::AudioCues{};
        kirpich::systems::controlsScreen(game, probe.settingsWiring(), probe.controlsWiring());
    };

    step({SDL_SCANCODE_X});
    ASSERT_TRUE(game.controlsScreen.listening);
    step({});
    probe.answer = keyPress(SDL_SCANCODE_X);
    step({SDL_SCANCODE_X});
    ASSERT_EQ(probe.controls[GbButton::B].key, SDL_SCANCODE_X);
    ASSERT_EQ(probe.controls[GbButton::A].key, SDL_SCANCODE_Z);

    step({SDL_SCANCODE_X});  // B under the new bindings
    ASSERT_TRUE(game.joypad.pressed.test(retropp::actionId(Action::Back)));
    EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS);
}

// A probe whose bindings are not the defaults, with the screen's cursor on the restore row.
Probe rebound() {
    Probe probe;
    probe.controls[GbButton::A].key  = SDL_SCANCODE_SPACE;
    probe.controls[GbButton::B].pad  = PadButton::ShoulderR;
    probe.controls[GbButton::UP].key = SDL_SCANCODE_W;
    return probe;
}

// A or Start on the restore row asks first: the question opens on "no" with the screen-change cue,
// and nothing is restored, put into effect or saved yet. With the bindings already the defaults there
// is nothing to restore, so the press asks nothing and says nothing.
TEST(ControlsScreen, RestoringAsksFirstAndOpensOnNo) {
    for (const Action act : {Action::Confirm, Action::Start}) {
        Probe       probe   = rebound();
        const Controls before = probe.controls;
        GameContext game    = openControls(probe);
        game.controlsScreen.row = ControlsRow::RESTORE_DEFAULTS;

        frame(game, probe, {act});
        EXPECT_TRUE(game.controlsScreen.confirmingRestore) << "action " << int(act);
        EXPECT_FALSE(game.controlsScreen.confirmYes) << "the question opens on no";
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
        EXPECT_EQ(probe.controls, before);
        EXPECT_TRUE(probe.log.empty());
        EXPECT_EQ(probe.listens, 0);
    }

    Probe       probe;
    GameContext game        = openControls(probe);
    game.controlsScreen.row = ControlsRow::RESTORE_DEFAULTS;
    frame(game, probe, {Action::Confirm});
    EXPECT_FALSE(game.controlsScreen.confirmingRestore) << "nothing to restore, nothing to ask";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);
    EXPECT_TRUE(probe.log.empty());
}

// Left and Right move between the answers, an edge saying nothing. "No", and B, close the question
// with the bindings untouched, back on the restore row.
TEST(ControlsScreen, NoAndBLeaveTheBindingsAlone) {
    Probe          probe  = rebound();
    const Controls before = probe.controls;
    GameContext    game   = openControls(probe);
    game.controlsScreen.row = ControlsRow::RESTORE_DEFAULTS;

    frame(game, probe, {Action::Confirm});
    frame(game, probe, {Action::MenuLeft});
    EXPECT_FALSE(game.controlsScreen.confirmYes);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "no is the left edge";
    frame(game, probe, {Action::MenuRight});
    EXPECT_TRUE(game.controlsScreen.confirmYes);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    frame(game, probe, {Action::MenuRight});
    EXPECT_TRUE(game.controlsScreen.confirmYes);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE) << "yes is the right edge";
    frame(game, probe, {Action::MenuLeft});
    EXPECT_FALSE(game.controlsScreen.confirmYes);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    frame(game, probe, {Action::Confirm});  // on "no"
    EXPECT_FALSE(game.controlsScreen.confirmingRestore);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    EXPECT_EQ(probe.controls, before);
    EXPECT_TRUE(probe.log.empty());
    EXPECT_EQ(game.controlsScreen.row, ControlsRow::RESTORE_DEFAULTS);
    EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS);

    frame(game, probe, {Action::Confirm});
    frame(game, probe, {Action::MenuRight});
    frame(game, probe, {Action::Back});  // B, even with the cursor on "yes"
    EXPECT_FALSE(game.controlsScreen.confirmingRestore);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    EXPECT_EQ(probe.controls, before);
    EXPECT_TRUE(probe.log.empty());
    EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS) << "B leaves the question, not the screen";
}

// "Yes" puts every binding back to the defaults, put into effect and then saved, closes the question,
// and holds input until it is let go - on A and on Start alike.
TEST(ControlsScreen, YesPutsEveryBindingBack) {
    for (const Action act : {Action::Confirm, Action::Start}) {
        Probe       probe = rebound();
        GameContext game  = openControls(probe);
        game.controlsScreen.row = ControlsRow::RESTORE_DEFAULTS;

        frame(game, probe, {act});
        frame(game, probe, {Action::MenuRight});
        frame(game, probe, {act});

        EXPECT_EQ(probe.controls, kirpich::kDefaultControls) << "action " << int(act);
        EXPECT_EQ(probe.log, (std::vector<std::string>{"apply", "save"}));
        EXPECT_EQ(probe.lastApplied, kirpich::kDefaultControls);
        EXPECT_EQ(probe.lastSaved, kirpich::kDefaultControls);
        EXPECT_FALSE(game.controlsScreen.confirmingRestore);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
        EXPECT_TRUE(game.controlsScreen.awaitingRelease);
        EXPECT_EQ(game.controlsScreen.row, ControlsRow::RESTORE_DEFAULTS);
        EXPECT_EQ(probe.listens, 0);
    }
}

// The release guard covers a restore too. With A on Z and B on X, the player answers "yes" with Z -
// which the defaults give to B. Still held on the next frame, it reads as B, and without the guard it
// would leave the screen.
TEST(ControlsScreen, RestoringDoesNotLetTheHeldKeyActOnTheScreen) {
    Probe probe;
    probe.controls[GbButton::A].key = SDL_SCANCODE_Z;
    probe.controls[GbButton::B].key = SDL_SCANCODE_X;
    GameContext                   game = openControls(probe);
    kirpich::systems::InputSystem input;
    game.controlsScreen.row = ControlsRow::RESTORE_DEFAULTS;

    const auto step = [&](std::initializer_list<SDL_Scancode> keys) {
        game.joypad    = input.sample(heldUnder(probe.controls, keys));
        game.audioCues = kirpich::systems::AudioCues{};
        kirpich::systems::controlsScreen(game, probe.settingsWiring(), probe.controlsWiring());
    };

    step({SDL_SCANCODE_Z});  // A under the player's bindings: ask
    ASSERT_TRUE(game.controlsScreen.confirmingRestore);
    step({});
    step({SDL_SCANCODE_RIGHT});  // to "yes"
    step({});
    step({SDL_SCANCODE_Z});  // A: restore
    ASSERT_EQ(probe.controls, kirpich::kDefaultControls);

    step({SDL_SCANCODE_Z});  // B under the defaults
    ASSERT_TRUE(game.joypad.pressed.test(retropp::actionId(Action::Back)));
    EXPECT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS);
}

// B goes back to the settings screen, painted again, with its cursor on the Controls row.
TEST(ControlsScreen, BackReturnsToTheSettingsScreenOnItsRow) {
    Probe       probe;
    GameContext game = openControls(probe);
    ASSERT_EQ(game.flow.gameState, GameState::CONTROLS_SETTINGS);

    frame(game, probe, {Action::MenuDown});
    frame(game, probe, {Action::Back});

    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::CONTROLS);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    const std::size_t line = kirpich::systems::settingsRowLine(SettingsRow::CONTROLS);
    EXPECT_EQ(game.display.map[line][kirpich::systems::kCursorCol],
              static_cast<std::uint8_t>(kirpich::CharTile::HYPHEN));
    EXPECT_EQ(game.display.map[line][kirpich::systems::kLabelCol],
              static_cast<std::uint8_t>(kirpich::CharTile::LETTER_C));
}

// ── The components ────────────────────────────────────────────────────────────────────────────────

// The screen is a backdrop and its content, with the heading, both column heads, each button's label
// and the names of its key and controller button standing on the screen's cells.
TEST(ControlsScreen, TheDefaultsReadOnTheScreensCells) {
    const kirpich::render::Layers layers = draw(ControlsScreenState{}, kirpich::kDefaultControls);
    ASSERT_EQ(layers.size(), 2u) << "a backdrop of tiles, and everything else over it";
    EXPECT_LT(layers[0].z, layers[1].z);
    const auto content = contentOf(layers);

    expectText(content, "controls", kHeadingX, kHeadingY);
    expectText(content, "key", kKeyNameX, kColumnHeadY);
    expectText(content, "pad", kPadNameX, kColumnHeadY);
    expectText(content, "restore defaults", kRestoreLabelX, kRestoreY);

    struct Row {
        GbButton         button;
        std::string_view label;
        std::string_view key;
        std::string_view pad;
    };
    const Row rows[] = {
        {GbButton::UP, "up", "up", "up"},
        {GbButton::DOWN, "down", "down", "down"},
        {GbButton::LEFT, "left", "left", "left"},
        {GbButton::RIGHT, "right", "right", "right"},
        {GbButton::A, "a", "x", "a"},
        {GbButton::B, "b", "z", "b"},
        {GbButton::START, "start", "enter", "start"},
        {GbButton::SELECT, "select", "bksp", "selct"},
    };
    for (const Row& r : rows) {
        expectText(content, r.label, kLabelX, rowY(r.button));
        expectText(content, r.key, kKeyNameX, rowY(r.button));
        expectText(content, r.pad, kPadNameX, rowY(r.button));
    }

    EXPECT_EQ(kirpich::render::controls::kHeadingX, kHeadingX);
    EXPECT_EQ(kirpich::render::controls::rowY(GbButton::SELECT), rowY(GbButton::SELECT));
}

// A binding shows on the screen as the bindings it is handed, and a position is named for the pad the
// player has: the east button reads a on a Nintendo pad and b on an Xbox pad.
TEST(ControlsScreen, TheScreenDrawsTheBindingsAndThePadItIsHanded) {
    Controls c         = kirpich::kDefaultControls;
    c[GbButton::A].key = SDL_SCANCODE_SPACE;
    c[GbButton::A].pad = PadButton::FaceEast;
    c[GbButton::B].pad = PadButton::FaceSouth;
    const int y        = rowY(GbButton::A);

    const auto nintendo = draw(ControlsScreenState{}, c, ControllerType::Nintendo);
    expectText(contentOf(nintendo), "space", kKeyNameX, y);
    expectText(contentOf(nintendo), "a", kPadNameX, y);
    expectText(contentOf(nintendo), "b", kPadNameX, rowY(GbButton::B));

    const auto xbox = draw(ControlsScreenState{}, c, ControllerType::Xbox);
    expectText(contentOf(xbox), "b", kPadNameX, y);
    expectText(contentOf(xbox), "a", kPadNameX, rowY(GbButton::B));
}

// The cursor is a hyphen in the cell before the name it marks, gone while the blink is off and held
// while the screen waits. The cell being waited on reads "...", and the bottom of the screen says
// what kind of press it wants and that Escape cancels - and says nothing while the screen is idle.
TEST(ControlsScreen, TheCursorAndTheWaitAreDrawn) {
    const Controls& c      = kirpich::kDefaultControls;
    const auto      hyphen = kirpich::render::Glyphs("-", 0, 0, 8, kAtlas, kPlayerRamp).front().tile;

    const ControlsScreenState idleKey{.row = ControlsRow::B, .column = ControlsColumn::KEYBOARD};
    auto content = contentOf(draw(idleKey, c, ControllerType::Standard, true));
    ASSERT_TRUE(glyphAt(content, kKeyCursorX, rowY(GbButton::B)));
    EXPECT_EQ(glyphAt(content, kKeyCursorX, rowY(GbButton::B))->tile, hyphen);
    EXPECT_FALSE(glyphAt(content, kPadCursorX, rowY(GbButton::B)));
    EXPECT_FALSE(glyphAt(content, 32, kPromptY)) << "an idle screen asks for nothing";
    EXPECT_FALSE(glyphAt(content, 32, kCancelHintY));

    const ControlsScreenState idlePad{.row = ControlsRow::B, .column = ControlsColumn::CONTROLLER};
    content = contentOf(draw(idlePad, c, ControllerType::Standard, true));
    EXPECT_TRUE(glyphAt(content, kPadCursorX, rowY(GbButton::B)));
    EXPECT_FALSE(glyphAt(content, kKeyCursorX, rowY(GbButton::B)));

    content = contentOf(draw(idlePad, c, ControllerType::Standard, false));
    EXPECT_FALSE(glyphAt(content, kPadCursorX, rowY(GbButton::B))) << "the blink is off";

    ControlsScreenState waitingKey = idleKey;
    waitingKey.listening           = true;
    content = contentOf(draw(waitingKey, c, ControllerType::Standard, false));
    EXPECT_TRUE(glyphAt(content, kKeyCursorX, rowY(GbButton::B))) << "held while waiting";
    expectText(content, "...", kKeyNameX, rowY(GbButton::B));
    expectText(content, "b", kPadNameX, rowY(GbButton::B));  // only the waiting cell changes
    expectText(content, "press a key", 4 * 8, kPromptY);
    expectText(content, "esc cancels", 4 * 8, kCancelHintY);

    ControlsScreenState waitingPad = idlePad;
    waitingPad.listening           = true;
    content = contentOf(draw(waitingPad, c, ControllerType::Standard, true));
    expectText(content, "...", kPadNameX, rowY(GbButton::B));
    expectText(content, "z", kKeyNameX, rowY(GbButton::B));
    expectText(content, "press a button", 3 * 8, kPromptY);
    expectText(content, "esc cancels", 4 * 8, kCancelHintY);

    // On the restore row the cursor stands before its label, whichever column the buttons were on,
    // and no button cell is marked.
    for (const ControlsColumn column : {ControlsColumn::KEYBOARD, ControlsColumn::CONTROLLER}) {
        const ControlsScreenState restore{.row = ControlsRow::RESTORE_DEFAULTS, .column = column};
        content = contentOf(draw(restore, c, ControllerType::Standard, true));
        ASSERT_TRUE(glyphAt(content, kRestoreCursorX, kRestoreY));
        EXPECT_EQ(glyphAt(content, kRestoreCursorX, kRestoreY)->tile, hyphen);
        for (std::size_t i = 0; i < kirpich::kGbButtonCount; ++i) {
            const int y = rowY(static_cast<GbButton>(i));
            EXPECT_FALSE(glyphAt(content, kKeyCursorX, y));
            EXPECT_FALSE(glyphAt(content, kPadCursorX, y));
        }
    }
}

// The restore question takes the screen as the settings confirms do, on their cells: its title on the
// heading line, the question on lines 5 and 7, "no" on column 6 and "yes" on column 12 of line 11, and
// the cursor two cells before the answer chosen. Nothing of the bindings table is drawn under it.
TEST(ControlsScreen, TheRestoreQuestionTakesTheScreen) {
    const auto hyphen = kirpich::render::Glyphs("-", 0, 0, 8, kAtlas, kPlayerRamp).front().tile;
    const ControlsScreenState onNo{.row               = ControlsRow::RESTORE_DEFAULTS,
                                   .confirmingRestore = true,
                                   .confirmYes        = false};

    auto content = contentOf(draw(onNo, kirpich::kDefaultControls));
    expectText(content, "restore defaults", 2 * 8, 2 * 8);
    expectText(content, "restore every", 3 * 8, 5 * 8);
    expectText(content, "key and button", 3 * 8, 7 * 8);
    expectText(content, "no", 6 * 8, 11 * 8);
    expectText(content, "yes", 12 * 8, 11 * 8);
    ASSERT_TRUE(glyphAt(content, 4 * 8, 11 * 8));
    EXPECT_EQ(glyphAt(content, 4 * 8, 11 * 8)->tile, hyphen);
    EXPECT_FALSE(glyphAt(content, 10 * 8, 11 * 8));

    EXPECT_FALSE(glyphAt(content, kLabelX, rowY(GbButton::UP))) << "the button rows are not drawn";
    EXPECT_FALSE(glyphAt(content, kKeyNameX, kColumnHeadY)) << "the column heads are not drawn";
    EXPECT_FALSE(glyphAt(content, kRestoreLabelX, kRestoreY)) << "the restore row is not drawn";

    ControlsScreenState onYes = onNo;
    onYes.confirmYes          = true;
    content                   = contentOf(draw(onYes, kirpich::kDefaultControls));
    EXPECT_TRUE(glyphAt(content, 10 * 8, 11 * 8));
    EXPECT_FALSE(glyphAt(content, 4 * 8, 11 * 8));

    content = contentOf(draw(onYes, kirpich::kDefaultControls, ControllerType::Standard, false));
    EXPECT_FALSE(glyphAt(content, 10 * 8, 11 * 8)) << "the blink is off";
}

// Within one frame no two objects share a name, and every object is on the screen.
TEST(ControlsScreen, EveryObjectHasItsOwnNameAndIsOnTheScreen) {
    const ControlsScreenState states[] = {
        {.row = ControlsRow::SELECT},
        {.row = ControlsRow::SELECT, .listening = true},
        {.row = ControlsRow::RESTORE_DEFAULTS},
        {.row = ControlsRow::RESTORE_DEFAULTS, .confirmingRestore = true},
        {.row = ControlsRow::RESTORE_DEFAULTS, .confirmingRestore = true, .confirmYes = true},
    };
    for (const ControlsColumn column : {ControlsColumn::KEYBOARD, ControlsColumn::CONTROLLER}) {
        for (ControlsScreenState ui : states) {
            ui.column = column;
            std::vector<std::string> keys;
            for (const retropp::Sprite& s : contentOf(draw(ui, kirpich::kDefaultControls))) {
                EXPECT_GE(s.x, 0);
                EXPECT_LE(s.x + 8, kScreenWidth) << s.key.value;
                EXPECT_GE(s.y, 0);
                EXPECT_LE(s.y + 8, kScreenHeight) << s.key.value;
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

// ── The names ─────────────────────────────────────────────────────────────────────────────────────

// Letters and digits are themselves, the named keys have their short names, and every other key is
// k and its scancode. Swept over every scancode: each name is at most five characters the font draws.
TEST(ControlsScreen, KeyNames) {
    using kirpich::render::keyName;

    EXPECT_EQ(keyName(SDL_SCANCODE_A), "a");
    EXPECT_EQ(keyName(SDL_SCANCODE_Z), "z");
    EXPECT_EQ(keyName(SDL_SCANCODE_1), "1");
    EXPECT_EQ(keyName(SDL_SCANCODE_9), "9");
    EXPECT_EQ(keyName(SDL_SCANCODE_0), "0");
    EXPECT_EQ(keyName(SDL_SCANCODE_F1), "f1");
    EXPECT_EQ(keyName(SDL_SCANCODE_F12), "f12");
    EXPECT_EQ(keyName(SDL_SCANCODE_KP_1), "kp1");
    EXPECT_EQ(keyName(SDL_SCANCODE_KP_0), "kp0");

    const std::pair<SDL_Scancode, std::string_view> named[] = {
        {SDL_SCANCODE_RETURN, "enter"},    {SDL_SCANCODE_ESCAPE, "esc"},
        {SDL_SCANCODE_BACKSPACE, "bksp"},  {SDL_SCANCODE_TAB, "tab"},
        {SDL_SCANCODE_SPACE, "space"},     {SDL_SCANCODE_MINUS, "-"},
        {SDL_SCANCODE_PERIOD, "."},        {SDL_SCANCODE_UP, "up"},
        {SDL_SCANCODE_DOWN, "down"},       {SDL_SCANCODE_LEFT, "left"},
        {SDL_SCANCODE_RIGHT, "right"},     {SDL_SCANCODE_LSHIFT, "lshft"},
        {SDL_SCANCODE_RSHIFT, "rshft"},    {SDL_SCANCODE_LCTRL, "lctrl"},
        {SDL_SCANCODE_RCTRL, "rctrl"},     {SDL_SCANCODE_LALT, "lalt"},
        {SDL_SCANCODE_RALT, "ralt"},       {SDL_SCANCODE_LGUI, "lmeta"},
        {SDL_SCANCODE_RGUI, "rmeta"},      {SDL_SCANCODE_CAPSLOCK, "caps"},
        {SDL_SCANCODE_INSERT, "ins"},      {SDL_SCANCODE_DELETE, "del"},
        {SDL_SCANCODE_HOME, "home"},       {SDL_SCANCODE_END, "end"},
        {SDL_SCANCODE_PAGEUP, "pgup"},     {SDL_SCANCODE_PAGEDOWN, "pgdn"},
        {SDL_SCANCODE_KP_ENTER, "kpent"},  {SDL_SCANCODE_KP_PERIOD, "kp."},
        {SDL_SCANCODE_KP_MINUS, "kp-"},    {SDL_SCANCODE_KP_PLUS, "kpadd"},
        {SDL_SCANCODE_KP_MULTIPLY, "kpmul"}, {SDL_SCANCODE_KP_DIVIDE, "kpdiv"},
    };
    for (const auto& [key, name] : named) {
        EXPECT_EQ(keyName(key), name) << "scancode " << int(key);
    }

    EXPECT_EQ(keyName(SDL_SCANCODE_COMMA), "k" + std::to_string(int(SDL_SCANCODE_COMMA)));
    EXPECT_EQ(keyName(SDL_SCANCODE_SLASH), "k" + std::to_string(int(SDL_SCANCODE_SLASH)));

    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code) {
        const std::string name = keyName(static_cast<SDL_Scancode>(code));
        EXPECT_FALSE(name.empty()) << "scancode " << code;
        EXPECT_LE(name.size(), kirpich::render::kControlsNameWidth) << "scancode " << code;
        EXPECT_TRUE(drawable(name)) << "scancode " << code << " is \"" << name << "\"";
    }
}

// A face position is named for the letter printed there on the pad: Nintendo transposes the Xbox
// layout, and PlayStation and pads without letters take the Xbox letters. A face button bound by its
// letter is that letter on every pad. Shoulders, triggers and stick clicks take each family's names.
// Swept over every button on every family: each name is at most five characters the font draws.
TEST(ControlsScreen, ControllerButtonNames) {
    using kirpich::render::padButtonName;

    EXPECT_EQ(padButtonName(PadButton::FaceEast, ControllerType::Nintendo), "a");
    EXPECT_EQ(padButtonName(PadButton::FaceSouth, ControllerType::Nintendo), "b");
    EXPECT_EQ(padButtonName(PadButton::FaceNorth, ControllerType::Nintendo), "x");
    EXPECT_EQ(padButtonName(PadButton::FaceWest, ControllerType::Nintendo), "y");
    for (const ControllerType family :
         {ControllerType::Xbox, ControllerType::PlayStation, ControllerType::Standard,
          ControllerType::Unknown}) {
        EXPECT_EQ(padButtonName(PadButton::FaceSouth, family), "a") << int(family);
        EXPECT_EQ(padButtonName(PadButton::FaceEast, family), "b") << int(family);
        EXPECT_EQ(padButtonName(PadButton::FaceWest, family), "x") << int(family);
        EXPECT_EQ(padButtonName(PadButton::FaceNorth, family), "y") << int(family);
    }

    const ControllerType families[] = {ControllerType::Unknown, ControllerType::Xbox,
                                       ControllerType::PlayStation, ControllerType::Nintendo,
                                       ControllerType::Standard};
    for (const ControllerType family : families) {
        EXPECT_EQ(padButtonName(PadButton::FaceLabelA, family), "a");
        EXPECT_EQ(padButtonName(PadButton::FaceLabelB, family), "b");
        EXPECT_EQ(padButtonName(PadButton::FaceLabelX, family), "x");
        EXPECT_EQ(padButtonName(PadButton::FaceLabelY, family), "y");
        EXPECT_EQ(padButtonName(PadButton::Start, family), "start");
        EXPECT_EQ(padButtonName(PadButton::Select, family), "selct");
        EXPECT_EQ(padButtonName(PadButton::DpadUp, family), "up");
    }

    EXPECT_EQ(padButtonName(PadButton::ShoulderL, ControllerType::Xbox), "lb");
    EXPECT_EQ(padButtonName(PadButton::ShoulderL, ControllerType::PlayStation), "l1");
    EXPECT_EQ(padButtonName(PadButton::ShoulderL, ControllerType::Nintendo), "l");
    EXPECT_EQ(padButtonName(PadButton::TriggerR, ControllerType::Xbox), "rt");
    EXPECT_EQ(padButtonName(PadButton::TriggerR, ControllerType::PlayStation), "r2");
    EXPECT_EQ(padButtonName(PadButton::TriggerR, ControllerType::Nintendo), "zr");
    EXPECT_EQ(padButtonName(PadButton::StickClickL, ControllerType::PlayStation), "l3");
    EXPECT_EQ(padButtonName(PadButton::StickClickL, ControllerType::Standard), "ls");
    EXPECT_EQ(padButtonName(PadButton::LeftStickLeft, ControllerType::Xbox), "lslft");

    for (const ControllerType family : families) {
        for (int b = 0; b <= static_cast<int>(PadButton::RightStickRight); ++b) {
            const std::string name = padButtonName(static_cast<PadButton>(b), family);
            EXPECT_FALSE(name.empty()) << "button " << b << " family " << int(family);
            EXPECT_LE(name.size(), kirpich::render::kControlsNameWidth) << "button " << b;
            EXPECT_TRUE(drawable(name)) << "button " << b << " is \"" << name << "\"";
        }
    }
}
