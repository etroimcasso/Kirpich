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
#include "systems/boot.h"  // softReset
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
// value field is three cells wide because "off" is the widest thing that goes in it. The heading sits
// on the row every screen in this family uses (kScreenTitleRow, settings_screen.h), which is also
// what places the page-up arrow above it.
constexpr std::size_t kLabelCol  = 3;
constexpr std::size_t kCursorCol      = 1;


// The confirm. Its question is two lines because the font has no question mark and "erase all high
// scores" is one cell wider than the screen.
constexpr std::size_t kConfirmRow1      = 5;
constexpr std::size_t kConfirmRow2      = 7;
constexpr std::size_t kChoiceRow        = 11;
constexpr std::size_t kChoiceCursorGap  = 2;  // cells between the cursor and the word it points at
constexpr std::size_t kChoiceGap        = 2;  // cells between one answer and the next one's cursor

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
        case SettingsRow::NEW_MODES:
        case SettingsRow::FIXES:
        case SettingsRow::STATS:
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
    // The second page's rows all open screens or act; none carries an inline value. The ghost
    // switch lives on the ghost row's own screen, where there is room to say what it does.
    (void)settings;
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
        case SettingsRow::FIXES:        return "fixes";
        case SettingsRow::STATS:        return "stats";
        case SettingsRow::RESET_SCORES: return "reset scores";
        // "exit game", not "exit": on its own the word reads as leaving this screen, which is what
        // Back does, and a player reaching for it would be asked to quit instead.
        case SettingsRow::EXIT_GAME:    return "exit game";
    }
    return {};
}

// What each page is called. The name says what the page holds: the window's own choices are
// settings, and the pages after them - the screens and switches the cartridge never had - are
// enhancements. Each family counts from one, so a family's pages are numbered within it rather than
// across the whole screen. The font has no slash, so the name and the number sit a cell apart rather
// than reading "settings/1".
std::string_view pageTitle(std::uint8_t page) {
    switch (page) {
        case 0:  return "settings 1";
        case 1:  return "enhancements 1";
        case 2:  return "enhancements 2";
        default: return {};
    }
}

void paintSettings(BackgroundMap& map, const ScreenUiState& ui, const Settings& settings) {
    const std::uint8_t page = settingsPageOf(ui.settingsRow);

    clearVisibleRegion(map);

    const std::string_view title = pageTitle(page);
    writeMapText(map, kScreenTitleRow, centred(title.size()), title);

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
// What a confirm screen says: a heading, two lines of question, and the two answers it offers.
//
// The screen itself is general — it draws this, moves a cursor between the two answers, and reports
// which one the player left it on. What an answer MEANS belongs to the caller: on one confirm the
// left answer is a cancel, on another both answers act and B is the only way out. Anything that needs
// confirming adds an enumerator, an entry here, and a branch on the answer; it needs no screen.
struct ConfirmContent {
    std::string_view title;
    std::string_view first;
    std::string_view second;
    std::string_view leftChoice;
    std::string_view rightChoice;
};

// Whether leaving for the title screen is a thing this confirm can offer. It is not when the settings
// screen was opened from the title screen itself: there is no round to leave and nowhere to go, so the
// exit confirm asks the plain question it asks when the game is not being played.
bool offersReturnToTitle(const ScreenUiState& ui) {
    return ui.settingsReturn != GameState::TITLE_SCREEN;
}

ConfirmContent confirmContentFor(ConfirmAction action, bool canReturnToTitle) {
    switch (action) {
        case ConfirmAction::ERASE_SCORES:
            return {.title       = "reset scores",
                    .first       = "erase all",
                    .second      = "high scores",
                    .leftChoice  = "no",
                    .rightChoice = "yes"};
        case ConfirmAction::EXIT_GAME:
            // Mid-round there are two places to go, and both answers act - leaving without going
            // anywhere is what B is for, which is why neither answer is a "no". A screen offering
            // "no" beside two destinations would be asking two questions at once.
            if (canReturnToTitle) {
                return {.title       = "exit game",
                        .first       = "leave the game",
                        .second      = "and go to",
                        .leftChoice  = "title",
                        .rightChoice = "desktop"};
            }
            // Opened from the title screen, there is only one place to go. The question names it and
            // "no" is an answer again; the second line goes unused, which the paint allows for.
            return {.title       = "exit game",
                    .first       = "return to desktop",
                    .second      = {},
                    .leftChoice  = "no",
                    .rightChoice = "yes"};
    }
    return {};
}

// Where the two answers sit, and where each one's cursor goes.
//
// The pair is centred as a block — cursor, word, gap, cursor, word — rather than nailed to fixed
// columns, so a pair of long answers still fits the screen. For "no" and "yes" the arithmetic lands on
// columns 6 and 12, which is where that pair has always been drawn.
struct ChoiceColumns {
    std::size_t left;
    std::size_t right;
};

ChoiceColumns choiceColumns(const ConfirmContent& content) {
    const std::size_t block = kChoiceCursorGap + content.leftChoice.size() + kChoiceGap +
                              kChoiceCursorGap + content.rightChoice.size();
    const std::size_t start = block >= kScreenCols ? 0 : (kScreenCols - block) / 2;
    const std::size_t left  = start + kChoiceCursorGap;
    return {left, left + content.leftChoice.size() + kChoiceGap + kChoiceCursorGap};
}

void drawConfirmCursor(BackgroundMap& map, const ScreenUiState& ui) {
    const ChoiceColumns cols =
        choiceColumns(confirmContentFor(ui.pendingConfirm, offersReturnToTitle(ui)));
    map[kChoiceRow][cols.left - kChoiceCursorGap]  = kSpace;
    map[kChoiceRow][cols.right - kChoiceCursorGap] = kSpace;
    if (ui.cursorVisible) {
        const std::size_t col = ui.confirmRight ? cols.right : cols.left;
        map[kChoiceRow][col - kChoiceCursorGap] = kCursorGlyph;
    }
}

// Put the caller's screen back and hand control to whichever state opened this one.
void leaveSettings(GameContext& game) {
    restoreCallerScreen(game);
    game.flow.gameState   = game.screens.settingsReturn;
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
        case SettingsRow::NEW_MODES:
        case SettingsRow::FIXES:
        case SettingsRow::STATS:
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

void saveCallerScreen(GameContext& game) {
    ScreenUiState& ui = game.screens;

    ui.savedMap    = game.display.displayedMap();
    ui.savedOam    = game.engine.oam;
    ui.savedTimer1 = game.flow.timer1;
    ui.savedSheet  = game.display.sheet;
    clearOamObjects(game);

    // Lift the driver's demo gate for as long as a port screen is up. It is still set after an
    // attract demo has played - nothing clears it until a round starts - and while it is set the
    // driver blanks every cue before playing, so the screen would be silent for the rest of the
    // session. See the note on ScreenUiState::savedActiveDemo.
    ui.savedActiveDemo   = game.demo.activeDemo;
    game.demo.activeDemo = ActiveDemo::NONE;

    // The set the game's own selector arrow is a tile of. Selecting it is an assignment - every set
    // is already uploaded - and a screen drawn from the font reads the same either way.
    game.display.sheet = TileSheet::COPYRIGHT_TITLE;
}

void restoreCallerScreen(GameContext& game) {
    ScreenUiState& ui = game.screens;

    game.display.displayedMap() = ui.savedMap;
    game.display.sheet          = ui.savedSheet;
    game.engine.oam             = ui.savedOam;
    // The objects are back where they were, but nothing on screen has been theirs for however long
    // the screen was up, so none of them has a past for the renderer to ease them from.
    game.oamSources.reset();

    game.flow.timer1 = ui.savedTimer1;

    // The caller's demo gate, back as it was.
    game.demo.activeDemo = ui.savedActiveDemo;
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
    ScreenUiState& ui = game.screens;

    saveCallerScreen(game);
    BackgroundMap& map = game.display.displayedMap();

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

    // The screen-opening rows carry a right arrow rather than a value, so pressing that way opens
    // the screen the row points into - the same thing Confirm and Start do from these rows.
    if (pressed(game, Action::Confirm) || pressed(game, Action::Start) ||
        pressed(game, Action::MenuRight)) {
        const auto openScreen = [&game](GameState init) {
            game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
            game.flow.gameState   = init;
        };
        switch (game.screens.settingsRow) {
            case SettingsRow::GHOST_PIECE:
                openScreen(GameState::INIT_GHOST_SCREEN);
                return;
            case SettingsRow::NEW_MODES:
                openScreen(GameState::INIT_MODE_SCREEN);
                return;
            case SettingsRow::FIXES:
                openScreen(GameState::INIT_FIXES_SCREEN);
                return;
            case SettingsRow::STATS:
                openScreen(GameState::INIT_STATS_SCREEN);
                return;
            default:
                break;
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

    // It opens on the left answer every time. For the erase that is "no", so a player who arrives here
    // by accident leaves with their scores by pressing whichever button brought them; for the exit it
    // is the title, the one of the two that does not end the run.
    ui.confirmRight  = false;
    ui.cursorVisible = true;

    const ConfirmContent content =
        confirmContentFor(ui.pendingConfirm, offersReturnToTitle(ui));
    const ChoiceColumns cols = choiceColumns(content);

    drawValueArrows(game, Settings{}, kSettingsPageCount);  // the confirm has no scrollers
    clearVisibleRegion(map);
    writeMapText(map, kScreenTitleRow, centred(content.title.size()), content.title);
    // A question can be one line or two; an empty line is a row left blank rather than a row of
    // nothing written at column ten.
    writeMapText(map, kConfirmRow1, centred(content.first.size()), content.first);
    if (!content.second.empty()) {
        writeMapText(map, kConfirmRow2, centred(content.second.size()), content.second);
    }
    writeMapText(map, kChoiceRow, cols.left, content.leftChoice);
    writeMapText(map, kChoiceRow, cols.right, content.rightChoice);
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
        if (ui.pendingConfirm == ConfirmAction::EXIT_GAME) {
            // Opened from the title screen the answers are "no" and "yes", so the left one refuses
            // rather than going anywhere.
            if (!offersReturnToTitle(ui) && !ui.confirmRight) {
                returnToSettings(game, wiring);
                return;
            }

            if (ui.confirmRight) {
                // Out of the program. The confirm stays on screen for the frames it takes the engine
                // to resolve the request: going back to the settings screen first would show the
                // player a screen they have just left, and then quit out of it.
                if (wiring.exit) {
                    wiring.exit();
                }
                return;
            }

            // Out of the round instead, which is a soft reset that skips the copyright screen. The
            // machine goes back where the reset chord leaves it - the score tables kept, everything
            // else at its boot value - and then straight to the title rather than through the
            // copyright screens a reset shows first.
            //
            // Going through the reset rather than taking the screen down by hand is what makes this
            // correct rather than nearly correct. It selects the first map, which the title screen's
            // own init does not do for itself; it clears the pause flag, so a round left paused
            // cannot hand a paused frame to whatever starts next; and it asks for the sound driver's
            // whole startup rather than the plain initialisation, which is the only thing that clears
            // the driver's latched pause-tune timer. A driver left with that byte set never reaches
            // its sound routines again - the music and every effect stop for the rest of the session.
            softReset(game);
            game.flow.gameState = GameState::INIT_TITLE_SCREEN;
            return;
        }

        if (ui.confirmRight) {
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

    if (pressed(game, Action::MenuRight) && !ui.confirmRight) {
        ui.confirmRight       = true;
        game.audioCues.square = SquareSfxId::TINK;
    } else if (pressed(game, Action::MenuLeft) && ui.confirmRight) {
        ui.confirmRight       = false;
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
