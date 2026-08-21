#include "systems/settings_screen.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"        // SquareSfxId
#include "retropp/input.h"   // actionId
#include "state/display_state.h"
#include "render/palettes.h"  // kShadeRampCount, clampShadeRamp
#include "state/screen_ui_state.h"
#include "systems/game_state_dispatcher.h"
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/screen.h"        // writeMapText

namespace kirpich::systems {

namespace {

// The cursor holds for this many frames between toggles - the interval the selection screens blink
// their own cursors on (systems/menu_screens.cpp).
constexpr std::uint8_t kBlinkFrames = 16;

// The visible screen: the top-left corner of the background map, and the region these screens paint.
constexpr std::size_t kScreenRows = 18;
constexpr std::size_t kScreenCols = 20;

// The settings screen. The option rows are evenly spaced so the cursor's walk reads as a column; the
// value field is three cells wide because "off" is the widest thing that goes in it.
constexpr std::size_t kTitleRow  = 2;
constexpr std::size_t kLabelCol  = 3;
constexpr std::size_t kValueCol       = 15;
constexpr std::size_t kValueWidth     = 3;
constexpr std::size_t kCursorCol      = 1;


// The confirm. Its question is two lines because the font has no question mark and "erase all high
// scores" is one cell wider than the screen.
constexpr std::size_t kConfirmRow1      = 5;
constexpr std::size_t kConfirmRow2      = 7;
constexpr std::size_t kChoiceRow        = 11;
constexpr std::size_t kNoCol            = 6;
constexpr std::size_t kYesCol           = 12;
constexpr std::size_t kChoiceCursorGap  = 2;  // cells between the cursor and the word it points at

constexpr auto kSpace       = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kCursorGlyph = static_cast<std::uint8_t>(CharTile::HYPHEN);

// The game's own selector arrow — the one the title screen points at the player count with. It points
// right, and the left arrow is the same tile flipped, so the scroller is drawn in the game's own hand
// rather than in shapes invented for it. It is a tile of the copyright-and-title art, which is why
// this screen selects that art while it is up.
constexpr std::uint8_t kSelectorTile = 0x58;

// The object entries the two arrows occupy. The screen empties the buffer on the way in, so nothing
// else is using them.
constexpr std::size_t kLeftArrowObject  = 0;
constexpr std::size_t kRightArrowObject = 1;

// Object coordinates are offset from the screen's by (8, 16).
constexpr std::uint8_t kObjectOriginX = 8;
constexpr std::uint8_t kObjectOriginY = 16;

// Place the scroller's arrows, or take them away where there is nowhere to scroll to. Off the second
// page there is no scroller at all.
void drawRampArrows(GameContext& game, std::uint8_t ramp, bool onPalettePage) {
    game.engine.oam[kLeftArrowObject]  = OamEntry{};
    game.engine.oam[kRightArrowObject] = OamEntry{};
    if (!onPalettePage) {
        return;
    }

    const auto y = static_cast<std::uint8_t>(
        settingsRowLine(SettingsRow::SHADE_RAMP) * 8 + kObjectOriginY);
    if (ramp > 0) {
        game.engine.oam[kLeftArrowObject] =
            OamEntry{.y     = y,
                     .x     = static_cast<std::uint8_t>(kPaletteLeftArrowCol * 8 + kObjectOriginX),
                     .tile  = kSelectorTile,
                     .xflip = true};
    }
    if (ramp + 1 < render::kShadeRampCount) {
        game.engine.oam[kRightArrowObject] =
            OamEntry{.y    = y,
                     .x    = static_cast<std::uint8_t>(kPaletteRightArrowCol * 8 + kObjectOriginX),
                     .tile = kSelectorTile};
    }
}

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// The map the display is reading, which is the one these screens paint. Nothing else writes it while
// they are up: at the title screen the second map is idle, and in a paused round the first map is the
// one the frame's remaining beats keep writing.
BackgroundMap& targetMap(DisplayState& display) {
    return display.displayed == DisplayedMap::SECOND ? display.secondMap : display.map;
}

// Centre a run of `length` cells across the visible width.
std::size_t centred(std::size_t length) { return (kScreenCols - length) / 2; }

// Empty the visible region. The rest of the map is left as the caller had it - it is off screen, and
// the whole map goes back on the way out anyway.
void clearVisibleRegion(BackgroundMap& map) {
    for (std::size_t row = 0; row < kScreenRows; ++row) {
        for (std::size_t col = 0; col < kScreenCols; ++col) {
            map[row][col] = kSpace;
        }
    }
}

Settings settingsOf(const SettingsWiring& wiring) {
    return wiring.settings != nullptr ? *wiring.settings : Settings{};
}

// The two value fields, blanked and rewritten. Called on the way in and after every change.
void paintSettingsValues(BackgroundMap& map, const Settings& settings) {
    for (std::size_t i = 0; i < kValueWidth; ++i) {
        map[settingsRowLine(SettingsRow::FULLSCREEN)][kValueCol + i]   = kSpace;
        map[settingsRowLine(SettingsRow::WINDOW_SCALE)][kValueCol + i] = kSpace;
    }
    writeMapText(map, settingsRowLine(SettingsRow::FULLSCREEN), kValueCol,
                 settings.fullscreen ? "on" : "off");

    // One digit and an x - the scales this build offers are all single digits.
    const char scale[2] = {static_cast<char>('0' + settings.windowScale), 'x'};
    writeMapText(map, settingsRowLine(SettingsRow::WINDOW_SCALE), kValueCol,
                 std::string_view{scale, sizeof scale});
}

// The palette row's number: the ramp in effect, counted from one. It is the only text in the
// scroller - the arrows either side of it and the preview strip below are shapes the render bridge
// draws (src/render/settings_overlay.h), which is why the cells they sit in are cleared here.
static_assert(render::kShadeRampCount <= 99,
              "a ramp past the ninety-ninth has no room in the two cells the number is drawn in");

void paintRampValue(BackgroundMap& map, std::uint8_t ramp) {
    const std::size_t row = settingsRowLine(SettingsRow::SHADE_RAMP);
    map[row][kPaletteLeftArrowCol]  = kSpace;
    map[row][kPaletteRightArrowCol] = kSpace;

    // Right-aligned across its two cells, with a leading zero drawn as a space rather than as a
    // zero - the same law the game's own number readouts print under.
    const int number = render::clampShadeRamp(ramp) + 1;
    const auto digit = [](int value) {
        return static_cast<std::uint8_t>(static_cast<std::uint8_t>(CharTile::DIGIT_0) + value);
    };
    map[row][kPaletteValueCol]     = number >= 10 ? digit(number / 10) : kSpace;
    map[row][kPaletteValueCol + 1] = digit(number % 10);
}

// Which rows the given page draws, in order.
std::vector<SettingsRow> rowsOnPage(std::uint8_t page) {
    std::vector<SettingsRow> rows;
    for (std::uint8_t i = 0; i < kSettingsRowCount; ++i) {
        const auto row = static_cast<SettingsRow>(i);
        if (settingsPageOf(row) == page) rows.push_back(row);
    }
    return rows;
}

std::string_view labelFor(SettingsRow row) {
    switch (row) {
        case SettingsRow::FULLSCREEN:   return "fullscreen";
        case SettingsRow::WINDOW_SCALE: return "size";
        case SettingsRow::SHADE_RAMP:   return "palette";
        case SettingsRow::RESET_SCORES: return "reset scores";
        // "exit game", not "exit": on its own the word reads as leaving this screen, which is what
        // Back does, and a player reaching for it would be asked to quit instead.
        case SettingsRow::EXIT_GAME:    return "exit game";
    }
    return {};
}

void paintSettings(BackgroundMap& map, const ScreenUiState& ui, const Settings& settings) {
    const std::uint8_t page = settingsPageOf(ui.settingsRow);

    clearVisibleRegion(map);

    // The header names the page as well as the screen. The font has no slash, so the two sit a cell
    // apart rather than reading "settings/1".
    const char header[10] = {'s', 'e', 't', 't', 'i', 'n', 'g', 's', ' ',
                             static_cast<char>('1' + page)};
    const std::string_view title{header, sizeof header};
    writeMapText(map, kTitleRow, centred(title.size()), title);

    for (const SettingsRow row : rowsOnPage(page)) {
        writeMapText(map, settingsRowLine(row), kLabelCol, labelFor(row));
    }

    // Only the first page carries the two value fields and the palette scroller.
    if (page == 0) {
        paintSettingsValues(map, settings);
        paintRampValue(map, settings.shadeRamp);
    }
}

void drawSettingsCursor(BackgroundMap& map, const ScreenUiState& ui) {
    const std::uint8_t page = settingsPageOf(ui.settingsRow);
    for (const SettingsRow row : rowsOnPage(page)) {
        map[settingsRowLine(row)][kCursorCol] = kSpace;
    }
    if (ui.cursorVisible) {
        map[settingsRowLine(ui.settingsRow)][kCursorCol] = kCursorGlyph;
    }
}

// What the confirm asks, for each of the two actions it guards. Two lines apiece, each centred, so
// both read the same way - and neither needs a question mark, which the font does not carry.
struct ConfirmQuestion {
    std::string_view first;
    std::string_view second;
};

ConfirmQuestion questionFor(ConfirmAction action) {
    switch (action) {
        case ConfirmAction::ERASE_SCORES: return {"erase all", "high scores"};
        case ConfirmAction::EXIT_GAME:    return {"exit", "the game"};
    }
    return {};
}

void drawConfirmCursor(BackgroundMap& map, const ScreenUiState& ui) {
    map[kChoiceRow][kNoCol - kChoiceCursorGap]  = kSpace;
    map[kChoiceRow][kYesCol - kChoiceCursorGap] = kSpace;
    if (ui.cursorVisible) {
        const std::size_t col = ui.confirmYes ? kYesCol : kNoCol;
        map[kChoiceRow][col - kChoiceCursorGap] = kCursorGlyph;
    }
}

// The blink half of the selection screens' cursor law: hold while the frame timer counts, then
// toggle and reload. The dispatcher decrements the timer after the handler runs.
void blinkTick(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.timer1 = kBlinkFrames;
    game.screens.cursorVisible = !game.screens.cursorVisible;
}

// Put the caller's screen back and hand control to whichever state opened this one.
void leaveSettings(GameContext& game) {
    ScreenUiState& ui = game.screens;

    targetMap(game.display) = ui.savedMap;
    game.display.sheet      = ui.savedSheet;
    game.engine.oam         = ui.savedOam;
    // The objects are back where they were, but nothing on screen has been theirs for however long
    // the screen was up, so none of them has a past for the renderer to ease them from.
    game.oamSources.reset();

    game.flow.timer1    = ui.savedTimer1;
    game.flow.gameState = ui.settingsReturn;
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
}

// Back to the settings screen from the confirm. It repaints rather than re-entering the init, which
// would save the confirm's own picture as the caller's screen and lose the real one.
void returnToSettings(GameContext& game, const SettingsWiring& wiring) {
    BackgroundMap& map = targetMap(game.display);
    paintSettings(map, game.screens, settingsOf(wiring));
    game.screens.cursorVisible = true;
    drawRampArrows(game, settingsOf(wiring).shadeRamp,
                   settingsPageOf(game.screens.settingsRow) == 0);
    drawSettingsCursor(map, game.screens);

    game.flow.timer1      = kBlinkFrames;
    game.flow.gameState   = GameState::SETTINGS;
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
}

// Move the cursor one row. Returns whether that crossed onto the other page, which is the caller's
// cue to repaint: a page is a different set of labels, not just a different cursor position.
bool moveCursor(GameContext& game, int delta) {
    const int next = static_cast<int>(game.screens.settingsRow) + delta;
    if (next < 0 || next >= static_cast<int>(kSettingsRowCount)) {
        return false;  // an end stop moves nothing and says nothing
    }
    const std::uint8_t before = settingsPageOf(game.screens.settingsRow);
    game.screens.settingsRow  = static_cast<SettingsRow>(next);
    game.audioCues.square     = SquareSfxId::TINK;
    return settingsPageOf(game.screens.settingsRow) != before;
}

// Change the value on the row the cursor is on. Right turns fullscreen on and steps the size up;
// left does the opposite. A change that lands on the value already held is an end stop: nothing is
// written, nothing is said, and neither seam fires.
void changeValue(GameContext& game, const SettingsWiring& wiring, int delta) {
    if (wiring.settings == nullptr) {
        return;
    }
    Settings next = *wiring.settings;
    switch (game.screens.settingsRow) {
        case SettingsRow::FULLSCREEN:
            next.fullscreen = delta > 0;
            break;
        case SettingsRow::WINDOW_SCALE:
            next.windowScale = clampWindowScale(static_cast<int>(next.windowScale) + delta);
            break;
        case SettingsRow::SHADE_RAMP:
            next.shadeRamp = render::clampShadeRamp(static_cast<int>(next.shadeRamp) + delta);
            break;
        case SettingsRow::RESET_SCORES:
        case SettingsRow::EXIT_GAME:
            return;  // an action, not a value
    }
    if (next == *wiring.settings) {
        return;
    }

    *wiring.settings      = next;
    game.audioCues.square = SquareSfxId::TINK;
    paintSettingsValues(targetMap(game.display), next);
    paintRampValue(targetMap(game.display), next.shadeRamp);
    if (wiring.apply) {
        wiring.apply(next);
    }
    if (wiring.save) {
        wiring.save(next);
    }
}

}  // namespace

void openSettings(GameContext& game) {
    game.screens.settingsReturn = game.flow.gameState;
    game.flow.gameState         = GameState::INIT_SETTINGS;
    game.audioCues.square       = SquareSfxId::CHANGE_SCREEN;
}

void initSettingsScreen(GameContext& game, const SettingsWiring& wiring) {
    ScreenUiState& ui  = game.screens;
    BackgroundMap& map = targetMap(game.display);

    ui.savedMap    = map;
    ui.savedOam    = game.engine.oam;
    ui.savedTimer1 = game.flow.timer1;
    ui.savedSheet  = game.display.sheet;
    clearOamObjects(game);

    // The set the game's own selector arrow is a tile of. Selecting it is an assignment - every set is
    // already uploaded - and the screen's text reads the same either way.
    game.display.sheet = TileSheet::COPYRIGHT_TITLE;

    ui.settingsRow   = SettingsRow::FULLSCREEN;
    ui.cursorVisible = true;

    paintSettings(map, game.screens, settingsOf(wiring));
    drawSettingsCursor(map, ui);
    drawRampArrows(game, settingsOf(wiring).shadeRamp, true);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = GameState::SETTINGS;
}

void settingsScreen(GameContext& game, const SettingsWiring& wiring) {
    blinkTick(game);

    if (pressed(game, Action::Back)) {
        leaveSettings(game);
        return;
    }

    // The two rows that end something both go through the same confirm, which asks about whichever
    // one opened it. Neither happens on a single press.
    if (pressed(game, Action::Confirm) || pressed(game, Action::Start)) {
        const SettingsRow row = game.screens.settingsRow;
        if (row == SettingsRow::RESET_SCORES || row == SettingsRow::EXIT_GAME) {
            game.screens.pendingConfirm = row == SettingsRow::RESET_SCORES
                                              ? ConfirmAction::ERASE_SCORES
                                              : ConfirmAction::EXIT_GAME;
            game.audioCues.square       = SquareSfxId::CHANGE_SCREEN;
            game.flow.gameState         = GameState::INIT_RESET_CONFIRM;
            return;
        }
    }

    if (pressed(game, Action::MenuRight)) {
        changeValue(game, wiring, 1);
    } else if (pressed(game, Action::MenuLeft)) {
        changeValue(game, wiring, -1);
    }

    bool turnedPage = false;
    if (pressed(game, Action::MenuDown)) {
        turnedPage = moveCursor(game, 1);
    } else if (pressed(game, Action::MenuUp)) {
        turnedPage = moveCursor(game, -1);
    }

    BackgroundMap& map = targetMap(game.display);

    // A page turn changes which labels are on screen, so the whole screen is laid out again rather
    // than only the cursor being moved.
    if (turnedPage) {
        paintSettings(map, game.screens, settingsOf(wiring));
    } else if (settingsPageOf(game.screens.settingsRow) == 0) {
        // The values are redrawn every frame, not only when this screen changes one: the fullscreen
        // chord sets the same setting from outside, and a row showing what the chord just turned off
        // is the whole point of having the row. Only the first page carries them.
        paintSettingsValues(map, settingsOf(wiring));
        paintRampValue(map, settingsOf(wiring).shadeRamp);
    }
    drawRampArrows(game, settingsOf(wiring).shadeRamp,
                   settingsPageOf(game.screens.settingsRow) == 0);
    drawSettingsCursor(map, game.screens);
}

void initResetConfirmScreen(GameContext& game) {
    ScreenUiState& ui  = game.screens;
    BackgroundMap& map = targetMap(game.display);

    // It opens on "no" every time, so a player who arrives here by accident leaves with their scores
    // by pressing whichever button brought them.
    ui.confirmYes    = false;
    ui.cursorVisible = true;

    const ConfirmQuestion question = questionFor(ui.pendingConfirm);

    drawRampArrows(game, 0, false);
    clearVisibleRegion(map);
    writeMapText(map, kConfirmRow1, centred(question.first.size()), question.first);
    writeMapText(map, kConfirmRow2, centred(question.second.size()), question.second);
    writeMapText(map, kChoiceRow, kNoCol, "no");
    writeMapText(map, kChoiceRow, kYesCol, "yes");
    drawConfirmCursor(map, ui);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = GameState::RESET_CONFIRM;
}

void resetConfirmScreen(GameContext& game, const SettingsWiring& wiring) {
    ScreenUiState& ui = game.screens;

    blinkTick(game);

    if (pressed(game, Action::Back)) {
        returnToSettings(game, wiring);
        return;
    }

    if (pressed(game, Action::Confirm) || pressed(game, Action::Start)) {
        if (ui.confirmYes && ui.pendingConfirm == ConfirmAction::EXIT_GAME) {
            // The run ends here. The confirm stays on screen for the frames it takes the engine to
            // resolve the request: going back to the settings screen first would show the player a
            // screen they have just left, and then quit out of it.
            if (wiring.exit) {
                wiring.exit();
            }
            return;
        }

        if (ui.confirmYes) {
            // Both tables, back to the state a machine that has never been played holds. A cleared
            // name is six zero bytes, which is what the top-score printer reads as no name at all.
            game.highScores.typeA = {};
            game.highScores.typeB = {};
            if (wiring.saveScores) {
                wiring.saveScores(game.highScores);
            }
        }
        returnToSettings(game, wiring);
        return;
    }

    if (pressed(game, Action::MenuRight) && !ui.confirmYes) {
        ui.confirmYes         = true;
        game.audioCues.square = SquareSfxId::TINK;
    } else if (pressed(game, Action::MenuLeft) && ui.confirmYes) {
        ui.confirmYes         = false;
        game.audioCues.square = SquareSfxId::TINK;
    }

    drawConfirmCursor(targetMap(game.display), ui);
}

void installSettingsHandlers(GameStateDispatcher& dispatcher, SettingsWiring wiring) {
    dispatcher.setHandler(GameState::INIT_SETTINGS,
                          [wiring](GameContext& g) { initSettingsScreen(g, wiring); });
    dispatcher.setHandler(GameState::SETTINGS,
                          [wiring](GameContext& g) { settingsScreen(g, wiring); });
    dispatcher.setHandler(GameState::INIT_RESET_CONFIRM, initResetConfirmScreen);
    dispatcher.setHandler(GameState::RESET_CONFIRM,
                          [wiring = std::move(wiring)](GameContext& g) {
                              resetConfirmScreen(g, wiring);
                          });
}

}  // namespace kirpich::systems
