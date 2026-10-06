#include "systems/settings_screen.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"        // SquareSfxId
#include "retropp/input.h"   // actionId
#include "state/display_state.h"
#include "state/screen_ui_state.h"
#include "systems/boot.h"  // softReset
#include "systems/controls_screen.h"          // openControlsSettings
#include "systems/display_settings_screen.h"  // openDisplaySettings
#include "systems/game_state_dispatcher.h"
#include "systems/palette_settings_screen.h"  // openPaletteSettings
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/screen.h"         // writeMapText
#include "systems/title_screens.h"  // refreshTitleScreenObjects

namespace kirpich::systems {

namespace {

// The visible screen: the top-left corner of the background map, and the region these screens paint.
constexpr std::size_t kScreenRows = 18;
constexpr std::size_t kScreenCols = 20;

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

// Which arrows a row carries. No row on this screen holds a value of its own, so the only arrow is
// the right one on a row that opens a screen: it says there is another screen through this row, and
// pressing that way opens it.
struct Reach {
    bool left  = false;
    bool right = false;
};

Reach reachOf(SettingsRow row) {
    switch (row) {
        case SettingsRow::DISPLAY:
        case SettingsRow::PALETTE:
        case SettingsRow::CONTROLS:
        case SettingsRow::GHOST_PIECE:
        case SettingsRow::NEW_MODES:
        case SettingsRow::FIXES:
        case SettingsRow::STATS:
            return {.left = false, .right = true};
        case SettingsRow::EXIT_GAME:
        case SettingsRow::RESET_SCORES:
        case SettingsRow::RESET_STATS:
        case SettingsRow::RESET_ACHIEVEMENTS:
        case SettingsRow::RESET_ALL:
            return {};  // an action has nothing to scroll through
    }
    return {};
}

// Place every row's arrows, or take them away. Rows off the current page have none, and neither does
// a row that opens nothing.
void drawValueArrows(GameContext& game, std::uint8_t page) {
    for (std::uint8_t i = 0; i < kSettingsRowCount; ++i) {
        const auto row = static_cast<SettingsRow>(i);
        const Reach reach = settingsPageOf(row) == page ? reachOf(row) : Reach{};
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
        case SettingsRow::DISPLAY:      return "display";
        case SettingsRow::PALETTE:      return "palette";
        case SettingsRow::CONTROLS:     return "controls";
        // "ghost", not "ghost piece": a label runs from column 3 to the left arrow at column 13, so
        // ten cells is all there is, and the two-word form is eleven. The siblings are terse for the
        // same reason - the Display screen's size row is "size", not "window scale".
        case SettingsRow::GHOST_PIECE:  return "ghost";
        case SettingsRow::NEW_MODES:    return "new modes";
        case SettingsRow::FIXES:        return "fixes";
        case SettingsRow::STATS:        return "stats";
        case SettingsRow::RESET_SCORES: return "reset scores";
        // A label has ten cells (see above), so the achievements row is abbreviated to fit. The three
        // siblings read as one group because they all start with the verb.
        case SettingsRow::RESET_STATS:        return "reset stats";
        case SettingsRow::RESET_ACHIEVEMENTS: return "reset achvmnts";
        case SettingsRow::RESET_ALL:          return "reset all";
        // "exit game", not "exit": on its own the word reads as leaving this screen, which is what
        // Back does, and a player reaching for it would be asked to quit instead.
        case SettingsRow::EXIT_GAME:    return "exit game";
    }
    return {};
}

// What each page is called. The name says what the page holds: the screens for the window, the
// palette and the controls are settings, and the pages after them - the screens and switches the cartridge never had - are
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

void paintSettings(BackgroundMap& map, const ScreenUiState& ui) {
    const std::uint8_t page = settingsPageOf(ui.settingsRow);

    clearVisibleRegion(map);

    const std::string_view title = pageTitle(page);
    writeMapText(map, kScreenTitleRow, centred(title.size()), title);

    for (const SettingsRow row : rowsOnPage(page)) {
        writeMapText(map, settingsRowLine(row), kLabelCol, labelFor(row));
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

// What each reset costs. Three records, three documents, three rows - so each of these clears one
// kind and writes one document, and the all-in row is the three of them run together rather than a
// fourth thing that knows how to clear everything.
//
// Each leaves the round in progress alone. The settings screen opens from a paused round, so a reset
// taken mid-round would otherwise drop the latch the round records through, and the round would end
// up counted nowhere. Clearing what has been recorded and letting the current round go on to be
// recorded is the answer that does what the row says.

void eraseScores(GameContext& game, const SettingsWiring& wiring) {
    // Every table, back to the state a machine that has never been played holds - the heart tables
    // with the cartridge ones, since "erase all high scores" means all of them. A cleared name is six
    // zero bytes, which is what the top-score printer reads as no name at all. saveScores writes both
    // documents, so the cleared heart tables are persisted too.
    game.highScores.typeA      = {};
    game.highScores.typeB      = {};
    game.highScores.typeC      = {};
    game.highScores.typeAHeart = {};
    game.highScores.typeBHeart = {};
    game.highScores.typeCHeart = {};
    if (wiring.saveScores) {
        wiring.saveScores(game.highScores);
    }
}

void eraseStats(GameContext& game, const SettingsWiring& wiring) {
    // The six slice tables and the two figures that are not folds over them. The application total
    // goes back to zero with the rest: it is the count of time spent, and a player erasing what they
    // have played is erasing that too.
    //
    // The stamp is NOT cleared with it. It is the point the application clock was last banked from,
    // and a zero stamp makes the next bank read the whole monotonic clock as time just played and add
    // it to the total that was meant to be empty. Keeping it means the next bank adds only the
    // fraction of a second since the last one, which is the time the player has genuinely spent since
    // answering yes.
    const RoundInProgress round = game.stats.round;
    const std::uint64_t   stamp = game.stats.applicationStampNanos;

    game.stats                       = StatsState{};
    game.stats.round                 = round;
    game.stats.applicationStampNanos = stamp;

    if (wiring.saveStats) {
        wiring.saveStats(game.stats);
    }
}

void eraseAchievements(GameContext& game, const SettingsWiring& wiring) {
    // The unlock records only. The round's own observations are this round's bookkeeping, not a
    // record of anything earned, and the round is still being played.
    game.achievements.unlocked = {};
    if (wiring.saveAchievements) {
        wiring.saveAchievements(game.achievements);
    }
}

// Which confirm a row opens, or nothing when the row is not an action. One table rather than a chain
// of comparisons, so adding an action row is a row here and a question below.
std::optional<ConfirmAction> confirmFor(SettingsRow row) noexcept {
    switch (row) {
        case SettingsRow::RESET_SCORES:       return ConfirmAction::ERASE_SCORES;
        case SettingsRow::RESET_STATS:        return ConfirmAction::ERASE_STATS;
        case SettingsRow::RESET_ACHIEVEMENTS: return ConfirmAction::ERASE_ACHIEVEMENTS;
        case SettingsRow::RESET_ALL:          return ConfirmAction::ERASE_EVERYTHING;
        case SettingsRow::EXIT_GAME:          return ConfirmAction::EXIT_GAME;
        case SettingsRow::DISPLAY:
        case SettingsRow::PALETTE:
        case SettingsRow::CONTROLS:
        case SettingsRow::GHOST_PIECE:
        case SettingsRow::NEW_MODES:
        case SettingsRow::FIXES:
        case SettingsRow::STATS:
            break;
    }
    return std::nullopt;
}

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
        case ConfirmAction::ERASE_STATS:
            return {.title       = "reset stats",
                    .first       = "erase everything",
                    .second      = "played so far",
                    .leftChoice  = "no",
                    .rightChoice = "yes"};
        case ConfirmAction::ERASE_ACHIEVEMENTS:
            // The question says "achievements" where the row says "achvmnts". A row label starts at
            // column 3 and the word does not fit in what is left; these lines are centred across the
            // whole width, so here it does, and the screen that asks should use the real word.
            return {.title       = "reset achievements",
                    .first       = "erase every",
                    .second      = "achievement earned",
                    .leftChoice  = "no",
                    .rightChoice = "yes"};
        case ConfirmAction::ERASE_EVERYTHING:
            // Named for what it costs rather than for the row, because this is the one answer a
            // player cannot take back a piece at a time.
            return {.title       = "reset all",
                    .first       = "erase scores stats",
                    .second      = "and achievements",
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

// Where the two answers sit (settings_screen.h).
ConfirmChoiceColumns choiceColumns(const ConfirmContent& content) {
    return confirmChoiceColumns(content.leftChoice.size(), content.rightChoice.size());
}

void drawConfirmCursor(BackgroundMap& map, const ScreenUiState& ui) {
    const ConfirmChoiceColumns cols =
        choiceColumns(confirmContentFor(ui.pendingConfirm, offersReturnToTitle(ui)));
    map[kConfirmChoiceRow][cols.left - kConfirmCursorGap]  = kSpace;
    map[kConfirmChoiceRow][cols.right - kConfirmCursorGap] = kSpace;
    if (ui.cursorVisible) {
        const std::size_t col = ui.confirmRight ? cols.right : cols.left;
        map[kConfirmChoiceRow][col - kConfirmCursorGap] = kCursorGlyph;
    }
}

// Put the caller's screen back and hand control to whichever state opened this one.
void leaveSettings(GameContext& game, const SettingsWiring& wiring) {
    restoreCallerScreen(game);
    game.flow.gameState   = game.screens.settingsReturn;
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;

    // The title screen's objects are derived from the settings rather than remembered, and the
    // snapshot just put back was taken before the player changed them. Lay them down again for the
    // settings as they now stand.
    //
    // The title screen redraws them itself every frame - but not until its next tick, and frames are
    // submitted in between. Those would carry the row the player left, with the stats item still
    // standing or still missing, and a directly-written object is named for the entry it sits in, so
    // the renderer matches the two rows and glides one word into the other's place.
    if (game.flow.gameState == GameState::TITLE_SCREEN ||
        game.flow.gameState == GameState::INIT_TITLE_SCREEN) {
        refreshTitleScreenObjects(game, wiring.current().showStats);
    }
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
    game.flow.timer1 = kScreenBlinkFrames;
    game.screens.cursorVisible = !game.screens.cursorVisible;
}

bool changeSettings(GameContext& game, const SettingsWiring& wiring, const Settings& next) {
    if (wiring.settings == nullptr || next == *wiring.settings) {
        return false;
    }
    *wiring.settings      = next;
    game.audioCues.square = SquareSfxId::TINK;
    if (wiring.apply) {
        wiring.apply(next);
    }
    if (wiring.save) {
        wiring.save(next);
    }
    return true;
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

void returnToSettings(GameContext& game, const SettingsWiring& /*wiring*/) {
    BackgroundMap& map = game.display.displayedMap();
    paintSettings(map, game.screens);
    game.screens.cursorVisible = true;
    drawValueArrows(game, settingsPageOf(game.screens.settingsRow));
    drawSettingsCursor(map, game.screens);

    game.flow.timer1      = kScreenBlinkFrames;
    game.flow.gameState   = GameState::SETTINGS;
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
}

void openSettings(GameContext& game) {
    game.screens.settingsReturn = game.flow.gameState;
    game.flow.gameState         = GameState::INIT_SETTINGS;
    game.audioCues.square       = SquareSfxId::CHANGE_SCREEN;
}

void initSettingsScreen(GameContext& game, const SettingsWiring& /*wiring*/) {
    ScreenUiState& ui = game.screens;

    saveCallerScreen(game);
    BackgroundMap& map = game.display.displayedMap();

    ui.settingsRow   = SettingsRow::DISPLAY;
    ui.cursorVisible = true;

    paintSettings(map, game.screens);
    drawSettingsCursor(map, ui);
    drawValueArrows(game, settingsPageOf(ui.settingsRow));

    game.flow.timer1    = kScreenBlinkFrames;
    game.flow.gameState = GameState::SETTINGS;
}

void settingsScreen(GameContext& game, const SettingsWiring& wiring) {
    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        leaveSettings(game, wiring);
        return;
    }

    // The action rows. Each goes through the same confirm, which asks about whichever one opened it —
    // none of them happens on a single press. A row that is not in the table is not an action; the
    // new-modes row opens a screen of its own instead, because what it turns on needs more explaining
    // than a value in a field.
    if (pressed(game, Action::Confirm) || pressed(game, Action::Start)) {
        if (const std::optional<ConfirmAction> action = confirmFor(game.screens.settingsRow)) {
            game.screens.pendingConfirm = *action;
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
            case SettingsRow::DISPLAY:
                game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
                openDisplaySettings(game);
                return;
            case SettingsRow::PALETTE:
                game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
                openPaletteSettings(game);
                return;
            case SettingsRow::CONTROLS:
                game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
                openControlsSettings(game);
                return;
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
        paintSettings(map, game.screens);
    }
    drawValueArrows(game, settingsPageOf(game.screens.settingsRow));
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
    const ConfirmChoiceColumns cols = choiceColumns(content);

    drawValueArrows(game, kSettingsPageCount);  // the confirm has no scrollers
    clearVisibleRegion(map);
    writeMapText(map, kScreenTitleRow, centred(content.title.size()), content.title);
    // A question can be one line or two; an empty line is a row left blank rather than a row of
    // nothing written at column ten.
    writeMapText(map, kConfirmQuestionFirstRow, centred(content.first.size()), content.first);
    if (!content.second.empty()) {
        writeMapText(map, kConfirmQuestionSecondRow, centred(content.second.size()),
                     content.second);
    }
    writeMapText(map, kConfirmChoiceRow, cols.left, content.leftChoice);
    writeMapText(map, kConfirmChoiceRow, cols.right, content.rightChoice);
    drawConfirmCursor(map, ui);

    game.flow.timer1    = kScreenBlinkFrames;
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
            const ConfirmAction action = ui.pendingConfirm;
            const bool everything      = action == ConfirmAction::ERASE_EVERYTHING;

            if (everything || action == ConfirmAction::ERASE_SCORES) {
                eraseScores(game, wiring);
            }
            if (everything || action == ConfirmAction::ERASE_STATS) {
                eraseStats(game, wiring);
            }
            if (everything || action == ConfirmAction::ERASE_ACHIEVEMENTS) {
                eraseAchievements(game, wiring);
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
