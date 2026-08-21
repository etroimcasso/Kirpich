// The settings screen and its reset confirm — behavioral tests over src/systems/settings_screen.h.
//
// Device-free: the four handlers are pure logic over the game-state aggregate and the wiring's
// seams. These screens are the port's own, so every asserted value comes from the surface's stated
// contract rather than from tetris.asm — with two exceptions that do come from the cartridge and are
// asserted as such: the blink interval the selection screens use, and the title screen's own
// player-count laws, which the third item must leave alone.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/palettes.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "state/high_score_state.h"
#include "state/screen_ui_state.h"
#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/gameplay.h"
#include "systems/settings_screen.h"
#include "systems/title_screens.h"

namespace {

using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::HighScoreState;
using kirpich::Action;
using kirpich::Settings;
using kirpich::SettingsRow;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::SettingsWiring;

// The layout the screen draws, pinned here so a move shows up as a test change rather than silently.
constexpr std::size_t kTitleRow = 2;
constexpr std::size_t kTitleCol = 5;  // "settings 1" - ten cells, centred in twenty
constexpr std::size_t kFullscreenRow = 5;
constexpr std::size_t kScaleRow = 8;
constexpr std::size_t kPaletteRow = 11;
constexpr std::size_t kExitRow  = 14;  // the last row of the first page
constexpr std::size_t kResetRow = 5;   // the only row of the second page
constexpr std::size_t kLabelCol = 3;
constexpr std::size_t kValueCol = 15;
constexpr std::size_t kCursorCol = 1;
constexpr std::size_t kScreenRows = 18;
constexpr std::size_t kScreenCols = 20;

// The confirm's layout.
constexpr std::size_t kConfirmRow1 = 5;
constexpr std::size_t kConfirmCol1 = 5;
constexpr std::size_t kConfirmRow2 = 7;
constexpr std::size_t kConfirmCol2 = 4;
constexpr std::size_t kChoiceRow = 11;
constexpr std::size_t kNoCol = 6;
constexpr std::size_t kYesCol = 12;
constexpr std::size_t kCursorGap = 2;

// The interval the game's own selection screens blink their cursors on (tetris.asm:3597-3608).
constexpr std::uint8_t kBlinkFrames = 16;

constexpr auto kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kCursor = static_cast<std::uint8_t>(CharTile::HYPHEN);

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
}

// Compare a run of map cells against the glyphs they should hold. The expected side names CharTile
// enumerators rather than running the encoder, so a wrong encoding cannot agree with itself.
void expectGlyphs(const BackgroundMap& map, std::size_t row, std::size_t col,
                  std::initializer_list<CharTile> expected) {
    std::size_t i = 0;
    for (const CharTile tile : expected) {
        EXPECT_EQ(map[row][col + i], static_cast<std::uint8_t>(tile))
            << "row " << row << " col " << (col + i);
        ++i;
    }
}

// The wiring plus the counts its seams record, so a test can ask what fired.
struct Probe {
    Settings settings{};
    Settings lastApplied{};
    int      applied    = 0;
    int      saved      = 0;
    int      savedScores = 0;

    SettingsWiring wiring() {
        return SettingsWiring{
            .settings = &settings,
            .apply    = [this](const Settings& s) { ++applied; lastApplied = s; },
            .save     = [this](const Settings&) { ++saved; },
            .saveScores = [this](const HighScoreState&) { ++savedScores; },
        };
    }
};

// A context sitting on the settings screen, opened from `from`, with the caller's map filled with a
// recognisable pattern so a restore can be checked cell by cell.
void openFrom(GameContext& game, const SettingsWiring& wiring, GameState from) {
    game.flow.gameState = from;
    kirpich::systems::openSettings(game);
    kirpich::systems::initSettingsScreen(game, wiring);
}

// ── Opening ───────────────────────────────────────────────────────────────────────────────────────

// (1) Opening records whichever state asked for the screen, so the same screen returns to either
// caller. It does not paint - the init does that on the next tick.
TEST(SettingsScreen, OpeningRemembersTheCaller) {
    for (const GameState caller : {GameState::TITLE_SCREEN, GameState::NORMAL_GAMEPLAY}) {
        GameContext game;
        game.flow.gameState = caller;
        const BackgroundMap before = game.display.map;

        kirpich::systems::openSettings(game);

        EXPECT_EQ(game.flow.gameState, GameState::INIT_SETTINGS);
        EXPECT_EQ(game.screens.settingsReturn, caller);
        EXPECT_TRUE(game.display.map == before) << "opening must not paint";
    }
}

// ── The screen itself ─────────────────────────────────────────────────────────────────────────────

// (2) The init saves what it is about to cover, empties the object buffer, paints, and enters the
// screen's own state with the cursor at the top.
TEST(SettingsScreen, InitSavesTheCallerScreenAndPaints) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();

    // A recognisable caller screen and a live object.
    game.display.map[0][0]  = 0x77;
    game.display.map[17][19] = 0x88;
    game.engine.oam[0].tile = 0x58;
    game.engine.oam[0].y    = 0x80;
    game.flow.timer1        = 99;
    const BackgroundMap callerMap = game.display.map;

    openFrom(game, wiring, GameState::TITLE_SCREEN);

    EXPECT_TRUE(game.screens.savedMap == callerMap);
    EXPECT_EQ(game.screens.savedOam[0].tile, 0x58);
    EXPECT_EQ(game.screens.savedTimer1, 99);

    // The buffer is emptied, so the caller's objects do not sit over this screen.
    EXPECT_EQ(game.engine.oam[0].tile, 0u);
    EXPECT_EQ(game.engine.oam[0].y, 0u);

    EXPECT_EQ(game.screens.settingsRow, SettingsRow::FULLSCREEN);
    EXPECT_TRUE(game.screens.cursorVisible);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);
    EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
}

// (3) The painted screen, cell for cell: the title, the three labels, both values, the cursor on the
// first row, and empty everywhere else. The sweep is what catches a stray write.
TEST(SettingsScreen, PaintedLayoutIsExact) {
    GameContext game;
    Probe       probe;
    probe.settings = Settings{.fullscreen = false, .windowScale = 4};
    const auto wiring = probe.wiring();

    openFrom(game, wiring, GameState::TITLE_SCREEN);
    const BackgroundMap& map = game.display.map;

    using C = CharTile;
    // The header names the page as well as the screen; the font has no slash, so a space stands in.
    expectGlyphs(map, kTitleRow, kTitleCol,
                 {C::LETTER_S, C::LETTER_E, C::LETTER_T, C::LETTER_T, C::LETTER_I, C::LETTER_N,
                  C::LETTER_G, C::LETTER_S, C::SPACE, C::DIGIT_1});
    expectGlyphs(map, kFullscreenRow, kLabelCol,
                 {C::LETTER_F, C::LETTER_U, C::LETTER_L, C::LETTER_L, C::LETTER_S, C::LETTER_C,
                  C::LETTER_R, C::LETTER_E, C::LETTER_E, C::LETTER_N});
    expectGlyphs(map, kScaleRow, kLabelCol, {C::LETTER_S, C::LETTER_I, C::LETTER_Z, C::LETTER_E});
    expectGlyphs(map, kPaletteRow, kLabelCol,
                 {C::LETTER_P, C::LETTER_A, C::LETTER_L, C::LETTER_E, C::LETTER_T, C::LETTER_T,
                  C::LETTER_E});

    expectGlyphs(map, kFullscreenRow, kValueCol, {C::LETTER_O, C::LETTER_F, C::LETTER_F});
    expectGlyphs(map, kScaleRow, kValueCol, {C::DIGIT_4, C::LETTER_X});

    // The palette row is a scroller: only its number is text. The cells its arrows occupy are left
    // empty, because the arrows are shapes the render bridge draws over the frame. The number is
    // right-aligned across two cells, so ramp one reads as a blank tens cell and a 1.
    EXPECT_EQ(map[kPaletteRow][kirpich::systems::kPaletteValueCol], kSpace);
    EXPECT_EQ(map[kPaletteRow][kirpich::systems::kPaletteValueCol + 1],
              static_cast<std::uint8_t>(C::DIGIT_1));
    EXPECT_EQ(map[kPaletteRow][kirpich::systems::kPaletteLeftArrowCol], kSpace);
    EXPECT_EQ(map[kPaletteRow][kirpich::systems::kPaletteRightArrowCol], kSpace);

    expectGlyphs(map, kExitRow, kLabelCol,
                 {C::LETTER_E, C::LETTER_X, C::LETTER_I, C::LETTER_T, C::SPACE, C::LETTER_G,
                  C::LETTER_A, C::LETTER_M, C::LETTER_E});

    EXPECT_EQ(map[kFullscreenRow][kCursorCol], kCursor);
    EXPECT_EQ(map[kScaleRow][kCursorCol], kSpace);
    EXPECT_EQ(map[kExitRow][kCursorCol], kSpace);

    // Everything the screen did not write is empty. The written spans are excluded by extent.
    const auto written = [](std::size_t row, std::size_t col) {
        if (row == kTitleRow) return col >= kTitleCol && col < kTitleCol + 10;
        if (row == kFullscreenRow || row == kScaleRow) {
            return col == kCursorCol || (col >= kLabelCol && col < kLabelCol + 10) ||
                   (col >= kValueCol && col < kValueCol + 3);
        }
        if (row == kPaletteRow) {
            return col == kCursorCol || (col >= kLabelCol && col < kLabelCol + 7) ||
                   (col >= kirpich::systems::kPaletteValueCol &&
                    col < kirpich::systems::kPaletteValueCol +
                              kirpich::systems::kPaletteValueWidth);
        }
        if (row == kExitRow) return col == kCursorCol || (col >= kLabelCol && col < kLabelCol + 9);
        return false;
    };
    for (std::size_t row = 0; row < kScreenRows; ++row) {
        for (std::size_t col = 0; col < kScreenCols; ++col) {
            if (written(row, col)) continue;
            EXPECT_EQ(map[row][col], kSpace) << "row " << row << " col " << col;
        }
    }
}

// (4) The cursor walks every row and stops at both ends, turning the page when it crosses between
// them. A move says so; an end stop does not. The page turn is what the labels prove: crossing onto
// the second page must lay the second page's labels out, not merely move the cursor.
TEST(SettingsScreen, CursorWalksAndStopsAtBothEnds) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    openFrom(game, wiring, GameState::TITLE_SCREEN);

    const auto step = [&](Action action) {
        game.audioCues = kirpich::systems::AudioCues{};
        press(game, {action});
        kirpich::systems::settingsScreen(game, wiring);
    };

    step(Action::MenuDown);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::WINDOW_SCALE);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    step(Action::MenuDown);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::SHADE_RAMP);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);

    step(Action::MenuDown);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::EXIT_GAME);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    EXPECT_EQ(game.display.map[kExitRow][kCursorCol], kCursor);

    // Down again crosses onto the second page: its label is laid out, the first page's are gone, and
    // the header counts up.
    step(Action::MenuDown);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::RESET_SCORES);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::TINK);
    {
        using C = CharTile;
        const BackgroundMap& map = game.display.map;
        expectGlyphs(map, kResetRow, kLabelCol,
                     {C::LETTER_R, C::LETTER_E, C::LETTER_S, C::LETTER_E, C::LETTER_T, C::SPACE,
                      C::LETTER_S, C::LETTER_C, C::LETTER_O, C::LETTER_R, C::LETTER_E, C::LETTER_S});
        EXPECT_EQ(map[kResetRow][kCursorCol], kCursor);
        EXPECT_EQ(map[kTitleRow][kTitleCol + 9], static_cast<std::uint8_t>(C::DIGIT_2));
        // The first page's rows are not on this one.
        EXPECT_EQ(map[kPaletteRow][kLabelCol], kSpace);
        EXPECT_EQ(map[kExitRow][kLabelCol], kSpace);
    }

    step(Action::MenuDown);  // the bottom end stop
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::RESET_SCORES);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    // Back up, across the page boundary again, to the top.
    for (int i = 0; i < 4; ++i) step(Action::MenuUp);
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::FULLSCREEN);
    {
        using C = CharTile;
        const BackgroundMap& map = game.display.map;
        EXPECT_EQ(map[kTitleRow][kTitleCol + 9], static_cast<std::uint8_t>(C::DIGIT_1));
        expectGlyphs(map, kExitRow, kLabelCol,
                     {C::LETTER_E, C::LETTER_X, C::LETTER_I, C::LETTER_T, C::SPACE, C::LETTER_G,
                      C::LETTER_A, C::LETTER_M, C::LETTER_E});
    }

    step(Action::MenuUp);  // the top end stop
    EXPECT_EQ(game.screens.settingsRow, SettingsRow::FULLSCREEN);
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);
}

// (5) The fullscreen row: right turns it on and left turns it off, each change reaching the value
// cells and both seams. Pressing into the value already held is an end stop and fires nothing.
TEST(SettingsScreen, FullscreenRowTogglesAndFiresBothSeams) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    openFrom(game, wiring, GameState::TITLE_SCREEN);

    using C = CharTile;

    press(game, {Action::MenuRight});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_TRUE(probe.settings.fullscreen);
    EXPECT_TRUE(probe.lastApplied.fullscreen);
    EXPECT_EQ(probe.applied, 1);
    EXPECT_EQ(probe.saved, 1);
    expectGlyphs(game.display.map, kFullscreenRow, kValueCol, {C::LETTER_O, C::LETTER_N});
    // The third cell of the field must be blanked, or "off" would show through as "onf".
    EXPECT_EQ(game.display.map[kFullscreenRow][kValueCol + 2], kSpace);

    game.audioCues = kirpich::systems::AudioCues{};
    press(game, {Action::MenuRight});  // already on
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_EQ(probe.applied, 1) << "an end stop must not re-apply";
    EXPECT_EQ(probe.saved, 1) << "an end stop must not re-save";
    EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::NONE);

    press(game, {Action::MenuLeft});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_FALSE(probe.settings.fullscreen);
    EXPECT_EQ(probe.applied, 2);
    expectGlyphs(game.display.map, kFullscreenRow, kValueCol,
                 {C::LETTER_O, C::LETTER_F, C::LETTER_F});
}

// (6) The size row steps through every scale the build offers and stops at both ends, and the value
// cells show the digit it is on.
TEST(SettingsScreen, WindowScaleRowStepsAndStops) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    openFrom(game, wiring, GameState::TITLE_SCREEN);

    press(game, {Action::MenuDown});
    kirpich::systems::settingsScreen(game, wiring);
    ASSERT_EQ(game.screens.settingsRow, SettingsRow::WINDOW_SCALE);

    // Up to the ceiling, one step at a time, checking the drawn digit each time.
    for (int scale = kirpich::kDefaultWindowScale; scale < kirpich::kMaxWindowScale; ++scale) {
        press(game, {Action::MenuRight});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.windowScale, scale + 1);
        EXPECT_EQ(game.display.map[kScaleRow][kValueCol],
                  static_cast<std::uint8_t>(CharTile::DIGIT_0) + (scale + 1));
        EXPECT_EQ(game.display.map[kScaleRow][kValueCol + 1],
                  static_cast<std::uint8_t>(CharTile::LETTER_X));
    }

    const int appliedAtCeiling = probe.applied;
    press(game, {Action::MenuRight});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.windowScale, kirpich::kMaxWindowScale);
    EXPECT_EQ(probe.applied, appliedAtCeiling) << "the ceiling is an end stop";

    // And all the way back down to the floor.
    for (int scale = kirpich::kMaxWindowScale; scale > kirpich::kMinWindowScale; --scale) {
        press(game, {Action::MenuLeft});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.windowScale, scale - 1);
    }

    const int appliedAtFloor = probe.applied;
    press(game, {Action::MenuLeft});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.windowScale, kirpich::kMinWindowScale);
    EXPECT_EQ(probe.applied, appliedAtFloor) << "the floor is an end stop";
}

// (6b) The palette row scrolls through every ramp the build offers and stops at both ends. The
// number is right-aligned across two cells, so it reads correctly either side of ten.
TEST(SettingsScreen, PaletteRowScrollsEveryRamp) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    openFrom(game, wiring, GameState::TITLE_SCREEN);

    press(game, {Action::MenuDown});
    kirpich::systems::settingsScreen(game, wiring);
    press(game, {Action::MenuDown});
    kirpich::systems::settingsScreen(game, wiring);
    ASSERT_EQ(game.screens.settingsRow, SettingsRow::SHADE_RAMP);

    const std::size_t tens  = kirpich::systems::kPaletteValueCol;
    const std::size_t units = tens + 1;
    const auto        digit = [](int value) {
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(CharTile::DIGIT_0) + value);
    };

    // Up through every ramp, checking the drawn number at each step.
    for (std::uint8_t ramp = 0; ramp + 1 < kirpich::render::kShadeRampCount; ++ramp) {
        press(game, {Action::MenuRight});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.shadeRamp, ramp + 1);

        const int number = ramp + 2;  // counted from one
        EXPECT_EQ(game.display.map[kPaletteRow][tens],
                  number >= 10 ? digit(number / 10) : kSpace)
            << "tens cell at ramp number " << number;
        EXPECT_EQ(game.display.map[kPaletteRow][units], digit(number % 10))
            << "units cell at ramp number " << number;
    }

    const int appliedAtTop = probe.applied;
    press(game, {Action::MenuRight});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.shadeRamp, kirpich::render::kShadeRampCount - 1);
    EXPECT_EQ(probe.applied, appliedAtTop) << "the last ramp is an end stop";

    // All the way back down to the first.
    for (std::uint8_t ramp = kirpich::render::kShadeRampCount - 1; ramp > 0; --ramp) {
        press(game, {Action::MenuLeft});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(probe.settings.shadeRamp, ramp - 1);
    }
    const int appliedAtBottom = probe.applied;
    press(game, {Action::MenuLeft});
    kirpich::systems::settingsScreen(game, wiring);
    EXPECT_EQ(probe.settings.shadeRamp, 0);
    EXPECT_EQ(probe.applied, appliedAtBottom) << "the first ramp is an end stop";
}

// (7) Only the reset row acts on Confirm and Start; the two value rows ignore both, so a player
// stepping a value cannot fall into the confirm.
TEST(SettingsScreen, OnlyTheResetRowOpensTheConfirm) {
    for (const Action act : {Action::Confirm, Action::Start}) {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();
        openFrom(game, wiring, GameState::TITLE_SCREEN);

        for (const SettingsRow row : {SettingsRow::FULLSCREEN, SettingsRow::WINDOW_SCALE}) {
            game.screens.settingsRow = row;
            press(game, {act});
            kirpich::systems::settingsScreen(game, wiring);
            EXPECT_EQ(game.flow.gameState, GameState::SETTINGS) << "value row acted on a button";
        }

        game.screens.settingsRow = SettingsRow::RESET_SCORES;
        game.audioCues           = kirpich::systems::AudioCues{};
        press(game, {act});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_RESET_CONFIRM);
        EXPECT_EQ(game.audioCues.square, kirpich::SquareSfxId::CHANGE_SCREEN);
    }
}

// (8) The cursor blinks on the frame timer, composed through the dispatcher so the timer is
// decremented the way a real frame decrements it: it holds for the interval, then toggles.
TEST(SettingsScreen, CursorBlinksOnTheFrameTimer) {
    GameContext         game;
    GameStateDispatcher dispatcher;
    Probe               probe;
    kirpich::systems::installSettingsHandlers(dispatcher, probe.wiring());

    game.flow.gameState = GameState::TITLE_SCREEN;
    kirpich::systems::openSettings(game);
    dispatcher.tick(game, retropp::ActionSet{});  // the init
    ASSERT_EQ(game.flow.gameState, GameState::SETTINGS);
    ASSERT_TRUE(game.screens.cursorVisible);

    // The interval holds the cursor as it is.
    for (int frame = 0; frame < kBlinkFrames - 1; ++frame) {
        dispatcher.tick(game, retropp::ActionSet{});
        EXPECT_TRUE(game.screens.cursorVisible) << "frame " << frame;
        EXPECT_EQ(game.display.map[kFullscreenRow][kCursorCol], kCursor);
    }

    // The frame the timer reaches zero, it toggles and the cell goes empty.
    dispatcher.tick(game, retropp::ActionSet{});
    EXPECT_FALSE(game.screens.cursorVisible);
    EXPECT_EQ(game.display.map[kFullscreenRow][kCursorCol], kSpace);
}

// ── The confirm ───────────────────────────────────────────────────────────────────────────────────

// (9) The confirm paints its two-line question, opens on "no", and moves between the two words.
TEST(SettingsScreen, ConfirmPaintsAndOpensOnNo) {
    GameContext game;
    Probe       probe;
    const auto  wiring = probe.wiring();
    openFrom(game, wiring, GameState::TITLE_SCREEN);

    game.screens.confirmYes = true;  // a previous visit left it on yes
    kirpich::systems::initResetConfirmScreen(game);

    EXPECT_FALSE(game.screens.confirmYes) << "the confirm must open on no every time";
    EXPECT_EQ(game.flow.gameState, GameState::RESET_CONFIRM);
    EXPECT_EQ(game.flow.timer1, kBlinkFrames);

    using C = CharTile;
    const BackgroundMap& map = game.display.map;
    expectGlyphs(map, kConfirmRow1, kConfirmCol1,
                 {C::LETTER_E, C::LETTER_R, C::LETTER_A, C::LETTER_S, C::LETTER_E, C::SPACE,
                  C::LETTER_A, C::LETTER_L, C::LETTER_L});
    expectGlyphs(map, kConfirmRow2, kConfirmCol2,
                 {C::LETTER_H, C::LETTER_I, C::LETTER_G, C::LETTER_H, C::SPACE, C::LETTER_S,
                  C::LETTER_C, C::LETTER_O, C::LETTER_R, C::LETTER_E, C::LETTER_S});
    expectGlyphs(map, kChoiceRow, kNoCol, {C::LETTER_N, C::LETTER_O});
    expectGlyphs(map, kChoiceRow, kYesCol, {C::LETTER_Y, C::LETTER_E, C::LETTER_S});
    EXPECT_EQ(map[kChoiceRow][kNoCol - kCursorGap], kCursor);
    EXPECT_EQ(map[kChoiceRow][kYesCol - kCursorGap], kSpace);

    // Right moves to yes and the cursor goes with it; left comes back.
    press(game, {Action::MenuRight});
    kirpich::systems::resetConfirmScreen(game, wiring);
    EXPECT_TRUE(game.screens.confirmYes);
    EXPECT_EQ(map[kChoiceRow][kYesCol - kCursorGap], kCursor);
    EXPECT_EQ(map[kChoiceRow][kNoCol - kCursorGap], kSpace);

    press(game, {Action::MenuLeft});
    kirpich::systems::resetConfirmScreen(game, wiring);
    EXPECT_FALSE(game.screens.confirmYes);
    EXPECT_EQ(map[kChoiceRow][kNoCol - kCursorGap], kCursor);
}

// (10) Yes clears both tables and writes them out; no and Back leave every score alone. Every path
// goes back to the settings screen with the settings screen painted again.
TEST(SettingsScreen, ConfirmActsOnYesAndOnlyOnYes) {
    const auto populated = [] {
        HighScoreState scores;
        scores.typeA[3][0].score = 12345;
        scores.typeA[3][0].name  = {CharTile::LETTER_A, CharTile::LETTER_B, CharTile::LETTER_C,
                                    CharTile::SPACE,    CharTile::SPACE,    CharTile::SPACE};
        scores.typeB[2][1][0].score = 6789;
        return scores;
    };

    // Yes: both tables go, and the cleared state is written out.
    {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();
        openFrom(game, wiring, GameState::TITLE_SCREEN);
        game.highScores = populated();
        kirpich::systems::initResetConfirmScreen(game);

        press(game, {Action::MenuRight});
        kirpich::systems::resetConfirmScreen(game, wiring);
        press(game, {Action::Confirm});
        kirpich::systems::resetConfirmScreen(game, wiring);

        EXPECT_EQ(game.highScores.typeA[3][0].score, 0u);
        EXPECT_EQ(game.highScores.typeA[3][0].name[0], CharTile::DIGIT_0);
        EXPECT_EQ(game.highScores.typeB[2][1][0].score, 0u);
        EXPECT_EQ(probe.savedScores, 1);
        EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
        // The settings screen is back, not the question.
        expectGlyphs(game.display.map, kScaleRow, kLabelCol,
                     {CharTile::LETTER_S, CharTile::LETTER_I, CharTile::LETTER_Z,
                      CharTile::LETTER_E});
    }

    // No, and Back: the scores survive and nothing is written.
    for (const Action act : {Action::Confirm, Action::Back}) {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();
        openFrom(game, wiring, GameState::TITLE_SCREEN);
        game.highScores = populated();
        kirpich::systems::initResetConfirmScreen(game);

        press(game, {act});
        kirpich::systems::resetConfirmScreen(game, wiring);

        EXPECT_EQ(game.highScores.typeA[3][0].score, 12345u);
        EXPECT_EQ(game.highScores.typeB[2][1][0].score, 6789u);
        EXPECT_EQ(probe.savedScores, 0);
        EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);
    }
}

// (10b) Exit is guarded by the same confirm, which asks about whichever row opened it. Yes ends the
// run; no and Back do not, and neither does merely landing on the row.
TEST(SettingsScreen, ExitIsGuardedByTheSameConfirm) {
    using C = CharTile;

    const auto openExitConfirm = [](GameContext& game, const SettingsWiring& wiring) {
        game.screens.settingsRow = SettingsRow::EXIT_GAME;
        press(game, {Action::Confirm});
        kirpich::systems::settingsScreen(game, wiring);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_RESET_CONFIRM)
            << "exit must not end the run on a single press";
        kirpich::systems::initResetConfirmScreen(game);
    };

    // The confirm asks about exiting, not about the scores.
    {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();
        int         exits  = 0;
        auto        wired  = wiring;
        wired.exit         = [&exits] { ++exits; };

        openFrom(game, wired, GameState::TITLE_SCREEN);
        openExitConfirm(game, wired);

        EXPECT_EQ(game.screens.pendingConfirm, kirpich::ConfirmAction::EXIT_GAME);
        expectGlyphs(game.display.map, kConfirmRow1, 8, {C::LETTER_E, C::LETTER_X, C::LETTER_I,
                                                          C::LETTER_T});
        expectGlyphs(game.display.map, kConfirmRow2, 6,
                     {C::LETTER_T, C::LETTER_H, C::LETTER_E, C::SPACE, C::LETTER_G, C::LETTER_A,
                      C::LETTER_M, C::LETTER_E});

        // No: the run continues and the screen comes back.
        press(game, {Action::Confirm});
        kirpich::systems::resetConfirmScreen(game, wired);
        EXPECT_EQ(exits, 0);
        EXPECT_EQ(game.flow.gameState, GameState::SETTINGS);

        // Yes: the run is asked to end, exactly once, and the scores are untouched by it.
        openExitConfirm(game, wired);
        press(game, {Action::MenuRight});
        kirpich::systems::resetConfirmScreen(game, wired);
        press(game, {Action::Confirm});
        kirpich::systems::resetConfirmScreen(game, wired);
        EXPECT_EQ(exits, 1);
        EXPECT_EQ(probe.savedScores, 0) << "exiting must not touch the score tables";
        // The confirm stays up until the run ends. Going back to the settings screen first would
        // show the player a screen they have just left, and then quit out of it.
        EXPECT_EQ(game.flow.gameState, GameState::RESET_CONFIRM);
        expectGlyphs(game.display.map, kConfirmRow1, 8,
                     {C::LETTER_E, C::LETTER_X, C::LETTER_I, C::LETTER_T});
    }

    // A build with no exit wired simply has a row that does nothing - it must not crash.
    {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();  // .exit left empty
        openFrom(game, wiring, GameState::TITLE_SCREEN);
        openExitConfirm(game, wiring);
        press(game, {Action::MenuRight});
        kirpich::systems::resetConfirmScreen(game, wiring);
        press(game, {Action::Confirm});
        kirpich::systems::resetConfirmScreen(game, wiring);
        EXPECT_EQ(game.flow.gameState, GameState::RESET_CONFIRM);
    }
}

// ── Leaving ───────────────────────────────────────────────────────────────────────────────────────

// (11) The headline: leaving puts the caller's screen back exactly. Asserted from both entries and
// as a whole-machine comparison, so a field the screen forgot to restore fails here rather than
// being noticed on a running build.
TEST(SettingsScreen, LeavingRestoresTheCallerExactly) {
    // From the title screen: the first map is the one shown, and it comes back cell for cell.
    {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();

        game.flow.gameState = GameState::TITLE_SCREEN;
        for (std::size_t row = 0; row < kScreenRows; ++row) {
            for (std::size_t col = 0; col < kScreenCols; ++col) {
                game.display.map[row][col] = static_cast<std::uint8_t>(row * 20 + col);
            }
        }
        game.engine.oam[0] = kirpich::OamEntry{.y = 0x80, .x = 0x10, .tile = 0x58};
        game.flow.timer1   = 61;
        const GameContext before = game;

        kirpich::systems::openSettings(game);
        kirpich::systems::initSettingsScreen(game, wiring);
        for (int frame = 0; frame < 3; ++frame) {
            press(game, {});
            kirpich::systems::settingsScreen(game, wiring);
        }
        press(game, {Action::Back});
        kirpich::systems::settingsScreen(game, wiring);

        EXPECT_EQ(game.flow.gameState, GameState::TITLE_SCREEN);
        EXPECT_TRUE(game.display.map == before.display.map);
        EXPECT_TRUE(game.engine.oam == before.engine.oam);
        EXPECT_EQ(game.flow.timer1, before.flow.timer1);
    }

    // From a paused round: the second map is the one shown, so that is the one saved and restored,
    // the display stays on it, and the first map - which the frame's other beats keep writing - is
    // never touched at all.
    {
        GameContext game;
        Probe       probe;
        const auto  wiring = probe.wiring();

        game.flow.gameState      = GameState::NORMAL_GAMEPLAY;
        game.flow.paused         = true;
        game.display.displayed   = DisplayedMap::SECOND;
        for (std::size_t row = 0; row < kScreenRows; ++row) {
            for (std::size_t col = 0; col < kScreenCols; ++col) {
                game.display.map[row][col]       = 0xA0;  // the live gameplay picture
                game.display.secondMap[row][col] = static_cast<std::uint8_t>(row + col);
            }
        }
        const BackgroundMap liveBefore   = game.display.map;
        const BackgroundMap pausedBefore = game.display.secondMap;

        kirpich::systems::openSettings(game);
        kirpich::systems::initSettingsScreen(game, wiring);

        // While the screen is up it is the paused screen that carries it, never the live one.
        EXPECT_TRUE(game.display.map == liveBefore) << "the live map must be left alone";
        EXPECT_FALSE(game.display.secondMap == pausedBefore);
        EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);

        press(game, {Action::Back});
        kirpich::systems::settingsScreen(game, wiring);

        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);
        EXPECT_TRUE(game.flow.paused) << "the round must still be paused";
        EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);
        EXPECT_TRUE(game.display.secondMap == pausedBefore);
        EXPECT_TRUE(game.display.map == liveBefore);
    }
}

// ── The two entries ───────────────────────────────────────────────────────────────────────────────

// (12) The title screen's third item: down reaches it, up leaves it, and while the cursor is on it
// the player-count laws are inert - so a player exploring the new item cannot silently change the
// player count behind the cursor.
TEST(SettingsScreen, TitleScreenReachesTheThirdItem) {
    GameContext game;
    kirpich::systems::initTitleScreen(game);
    ASSERT_FALSE(game.screens.titleSettingsSelected);

    const std::uint8_t playerRowY = game.engine.oam[0].y;

    press(game, {Action::MenuDown});
    kirpich::systems::titleScreen(game);
    EXPECT_TRUE(game.screens.titleSettingsSelected);
    EXPECT_NE(game.engine.oam[0].y, playerRowY) << "the cursor must move to the settings row";

    // The player-count buttons do nothing while the cursor is down here.
    const bool wasMultiplayer = game.multiplayer.isMultiplayer;
    for (const Action act : {Action::Select, Action::MenuRight, Action::MenuLeft}) {
        press(game, {act});
        kirpich::systems::titleScreen(game);
        EXPECT_EQ(game.multiplayer.isMultiplayer, wasMultiplayer) << "player count moved";
    }
    EXPECT_TRUE(game.screens.titleSettingsSelected);

    // Start opens the screen from here.
    press(game, {Action::Start});
    kirpich::systems::titleScreen(game);
    EXPECT_EQ(game.flow.gameState, GameState::INIT_SETTINGS);
    EXPECT_EQ(game.screens.settingsReturn, GameState::TITLE_SCREEN);

    // Up returns to the player row and leaves the player count where it was.
    GameContext back;
    kirpich::systems::initTitleScreen(back);
    press(back, {Action::MenuDown});
    kirpich::systems::titleScreen(back);
    press(back, {Action::MenuUp});
    kirpich::systems::titleScreen(back);
    EXPECT_FALSE(back.screens.titleSettingsSelected);
    EXPECT_EQ(back.engine.oam[0].y, playerRowY);
    EXPECT_EQ(back.flow.gameState, GameState::TITLE_SCREEN);
}

// (13) A paused round opens the screen with A, and an unpaused one does not - the same button
// rotates a piece there.
TEST(SettingsScreen, PausedRoundOpensWithConfirm) {
    {
        GameContext game;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.paused    = true;
        press(game, {Action::Confirm});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_EQ(game.flow.gameState, GameState::INIT_SETTINGS);
        EXPECT_EQ(game.screens.settingsReturn, GameState::NORMAL_GAMEPLAY);
    }
    {
        GameContext game;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.paused    = false;
        press(game, {Action::Confirm});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);
    }
    // A demo is running: recorded input cannot reach the screen any more than it can pause.
    {
        GameContext game;
        game.flow.gameState   = GameState::NORMAL_GAMEPLAY;
        game.flow.paused      = true;
        game.demo.activeDemo  = kirpich::ActiveDemo::TYPE_A;
        press(game, {Action::Confirm});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);
    }
}

}  // namespace
