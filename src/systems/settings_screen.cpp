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

// The object entries the arrows occupy: two per scroller row, in row order. The screen empties the
// buffer on the way in, so nothing else is using them.
constexpr std::size_t kFirstArrowObject = 0;

// Object coordinates are offset from the screen's by (8, 16).
constexpr std::uint8_t kObjectOriginX = 8;
constexpr std::uint8_t kObjectOriginY = 16;

// Which way each scroller row can still go. A row at the end of its range loses that arrow, so the
// ends of a range are visible rather than something a player finds by pressing.
struct Reach {
    bool left  = false;
    bool right = false;
};

Reach reachOf(SettingsRow row, const Settings& settings) {
    switch (row) {
        case SettingsRow::FULLSCREEN:
            return {.left = settings.fullscreen, .right = !settings.fullscreen};
        case SettingsRow::WINDOW_SCALE:
            return {.left  = settings.windowScale > kMinWindowScale,
                    .right = settings.windowScale < kMaxWindowScale};
        case SettingsRow::SHADE_RAMP:
            return {.left  = settings.shadeRamp > 0,
                    .right = settings.shadeRamp + 1 < render::kShadeRampCount};
        case SettingsRow::GHOST_PIECE:
            return {.left = settings.ghostPiece, .right = !settings.ghostPiece};
        case SettingsRow::NEW_MODES:
            // Not a value, but it does go somewhere: the right arrow says there is another screen
            // through this row, and pressing that way opens it.
            return {.left = false, .right = true};
        case SettingsRow::EXIT_GAME:
        case SettingsRow::RESET_SCORES:
            return {};  // an action has nothing to scroll through
    }
    return {};
}

// Place every scroller row's arrows, or take them away. Rows off the current page have none, and
// neither does a row that is an action rather than a choice.
void drawValueArrows(GameContext& game, const Settings& settings, std::uint8_t page) {
    for (std::uint8_t i = 0; i < kSettingsRowCount; ++i) {
        const auto row = static_cast<SettingsRow>(i);
        const Reach reach =
            settingsPageOf(row) == page ? reachOf(row, settings) : Reach{};
        placeScrollerArrows(game, kFirstArrowObject + std::size_t{2} * i, settingsRowLine(row),
                            reach.left, reach.right);
    }
}

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
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

// Write one row's value into the field, from its first cell, blanking whatever was there.
void paintValue(BackgroundMap& map, SettingsRow row, std::string_view text) {
    const std::size_t line = settingsRowLine(row);
    for (std::size_t i = 0; i < kOptionValueWidth; ++i) {
        map[line][kOptionValueCol + i] = kSpace;
    }
    if (text.size() > kOptionValueWidth) {
        text = text.substr(0, kOptionValueWidth);
    }
    writeMapText(map, line, kOptionValueCol, text);
}

// The palette row's number: the ramp in effect, counted from one. It is the only text in the
// scroller - the arrows either side of it and the preview strip below are shapes the render bridge
// draws (src/render/settings_overlay.h), which is why the cells they sit in are cleared here.
static_assert(render::kShadeRampCount <= 99,
              "a ramp past the ninety-ninth has no room in the two cells the number is drawn in");

void paintRampValue(BackgroundMap& map, std::uint8_t ramp) {
    const int  number = render::clampShadeRamp(ramp) + 1;
    const char digits[2] = {static_cast<char>('0' + number / 10),
                            static_cast<char>('0' + number % 10)};
    paintValue(map, SettingsRow::SHADE_RAMP,
               number >= 10 ? std::string_view{digits, 2} : std::string_view{digits + 1, 1});
}

// Every value on the given page. Called on the way in and after every change - and a page paints only
// its own rows, because the other page's lines hold whatever this one wrote there.
void paintSettingsValues(BackgroundMap& map, const Settings& settings, std::uint8_t page) {
    if (page == 0) {
        paintValue(map, SettingsRow::FULLSCREEN, settings.fullscreen ? "on" : "off");

        // One digit and an x - the scales this build offers are all single digits.
        const char scale[2] = {static_cast<char>('0' + settings.windowScale), 'x'};
        paintValue(map, SettingsRow::WINDOW_SCALE, std::string_view{scale, sizeof scale});

        paintRampValue(map, settings.shadeRamp);
        return;
    }
    paintValue(map, SettingsRow::GHOST_PIECE, settings.ghostPiece ? "on" : "off");
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
        // "ghost", not "ghost piece": a label runs from column 3 to the left arrow at column 13, so
        // ten cells is all there is, and the two-word form is eleven. The siblings are terse for the
        // same reason - the size row is "size", not "window scale".
        case SettingsRow::GHOST_PIECE:  return "ghost";
        case SettingsRow::NEW_MODES:    return "new modes";
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

    paintSettingsValues(map, settings, page);
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

// Put the caller's screen back and hand control to whichever state opened this one.
void leaveSettings(GameContext& game) {
    ScreenUiState& ui = game.screens;

    game.display.displayedMap() = ui.savedMap;
    game.display.sheet      = ui.savedSheet;
    game.engine.oam         = ui.savedOam;
    // The objects are back where they were, but nothing on screen has been theirs for however long
    // the screen was up, so none of them has a past for the renderer to ease them from.
    game.oamSources.reset();

    game.flow.timer1    = ui.savedTimer1;
    game.flow.gameState = ui.settingsReturn;
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
        case SettingsRow::GHOST_PIECE:
            next.ghostPiece = delta > 0;
            break;
        case SettingsRow::NEW_MODES:
        case SettingsRow::RESET_SCORES:
        case SettingsRow::EXIT_GAME:
            return;  // an action, not a value
    }
    if (next == *wiring.settings) {
        return;
    }

    *wiring.settings      = next;
    game.audioCues.square = SquareSfxId::TINK;
    paintSettingsValues(game.display.displayedMap(), next,
                        settingsPageOf(game.screens.settingsRow));
    if (wiring.apply) {
        wiring.apply(next);
    }
    if (wiring.save) {
        wiring.save(next);
    }
}

}  // namespace

void placeScrollerArrows(GameContext& game, std::size_t entry, std::size_t line, bool left,
                         bool right) {
    const auto arrow = [line](std::size_t col, bool flip) {
        return OamEntry{.y     = static_cast<std::uint8_t>(line * 8 + kObjectOriginY),
                        .x     = static_cast<std::uint8_t>(col * 8 + kObjectOriginX),
                        .tile  = kSelectorTile,
                        .xflip = flip};
    };
    game.engine.oam[entry] = left ? arrow(kOptionLeftArrowCol, /*flip=*/true) : OamEntry{};
    game.engine.oam[entry + 1] = right ? arrow(kOptionRightArrowCol, /*flip=*/false) : OamEntry{};
}

void blinkScreenCursor(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.timer1 = kBlinkFrames;
    game.screens.cursorVisible = !game.screens.cursorVisible;
}

void returnToSettings(GameContext& game, const SettingsWiring& wiring) {
    BackgroundMap& map = game.display.displayedMap();
    paintSettings(map, game.screens, wiring.current());
    game.screens.cursorVisible = true;
    drawValueArrows(game, wiring.current(), settingsPageOf(game.screens.settingsRow));
    drawSettingsCursor(map, game.screens);

    game.flow.timer1      = kBlinkFrames;
    game.flow.gameState   = GameState::SETTINGS;
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
}

void openSettings(GameContext& game) {
    game.screens.settingsReturn = game.flow.gameState;
    game.flow.gameState         = GameState::INIT_SETTINGS;
    game.audioCues.square       = SquareSfxId::CHANGE_SCREEN;
}

void initSettingsScreen(GameContext& game, const SettingsWiring& wiring) {
    ScreenUiState& ui  = game.screens;
    BackgroundMap& map = game.display.displayedMap();

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

    paintSettings(map, game.screens, wiring.current());
    drawSettingsCursor(map, ui);
    drawValueArrows(game, wiring.current(), settingsPageOf(ui.settingsRow));

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = GameState::SETTINGS;
}

void settingsScreen(GameContext& game, const SettingsWiring& wiring) {
    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        leaveSettings(game);
        return;
    }

    // The action rows. The two that end something go through the same confirm, which asks about
    // whichever one opened it — neither happens on a single press. The new-modes row opens a screen of
    // its own instead, because what it turns on needs more explaining than a value in a field.
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

    // The new-modes row carries a right arrow rather than a value, so pressing that way opens the
    // screen it points into - the same thing Confirm and Start do from this row.
    if (game.screens.settingsRow == SettingsRow::NEW_MODES &&
        (pressed(game, Action::Confirm) || pressed(game, Action::Start) ||
         pressed(game, Action::MenuRight))) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        game.flow.gameState   = GameState::INIT_MODE_SCREEN;
        return;
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

    BackgroundMap& map = game.display.displayedMap();

    // A page turn changes which labels are on screen, so the whole screen is laid out again rather
    // than only the cursor being moved.
    if (turnedPage) {
        paintSettings(map, game.screens, wiring.current());
    } else {
        // The values are redrawn every frame, not only when this screen changes one: the fullscreen
        // chord sets the same setting from outside, and a row showing what the chord just turned off
        // is the whole point of having the row.
        paintSettingsValues(map, wiring.current(), settingsPageOf(game.screens.settingsRow));
    }
    drawValueArrows(game, wiring.current(), settingsPageOf(game.screens.settingsRow));
    drawSettingsCursor(map, game.screens);
}

void initResetConfirmScreen(GameContext& game) {
    ScreenUiState& ui  = game.screens;
    BackgroundMap& map = game.display.displayedMap();

    // It opens on "no" every time, so a player who arrives here by accident leaves with their scores
    // by pressing whichever button brought them.
    ui.confirmYes    = false;
    ui.cursorVisible = true;

    const ConfirmQuestion question = questionFor(ui.pendingConfirm);

    drawValueArrows(game, Settings{}, kSettingsPageCount);  // the confirm has no scrollers
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

    blinkScreenCursor(game);

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
            game.highScores.typeC = {};
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

    drawConfirmCursor(game.display.displayedMap(), ui);
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
