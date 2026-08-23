#include "systems/mode_screen.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/screen.h"        // writeMapText

namespace kirpich::systems {

namespace {

// The visible screen, and the interval the game's own selection screens blink on.
constexpr std::size_t  kScreenRows  = 18;
constexpr std::size_t  kScreenCols  = 20;
constexpr std::uint8_t kBlinkFrames = 16;

constexpr auto kSpace       = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kCursorGlyph = static_cast<std::uint8_t>(CharTile::HYPHEN);

// The screen borrows the settings screen's own geometry for its one option row — the label column,
// the value field and the two arrow columns — so the row lines up with the rows the player has just
// walked past.
constexpr std::size_t kEnableRow = kSettingsFirstRow;
constexpr std::size_t kLabelCol  = 3;
constexpr std::size_t kCursorCol = 1;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Centre a run of `length` cells across the visible width.
std::size_t centred(std::size_t length) {
    return length >= kScreenCols ? 0 : (kScreenCols - length) / 2;
}

void clearVisibleRegion(BackgroundMap& map) {
    for (std::size_t row = 0; row < kScreenRows; ++row) {
        for (std::size_t col = 0; col < kScreenCols; ++col) {
            map[row][col] = kSpace;
        }
    }
}

bool enabledOf(const ModeScreenWiring& wiring) {
    return wiring.enabled != nullptr && *wiring.enabled;
}

void paintEnableValue(BackgroundMap& map, bool enabled) {
    for (std::size_t i = 0; i < kOptionValueWidth; ++i) {
        map[kEnableRow][kOptionValueCol + i] = kSpace;
    }
    writeMapText(map, kEnableRow, kOptionValueCol, enabled ? "on" : "off");
}

// The one scroller row's arrows, in the first two object entries. A row already at one end of its
// range loses that arrow, the same law every settings row follows.
void drawEnableArrows(GameContext& game, bool enabled) {
    placeScrollerArrows(game, 0, kEnableRow, /*left=*/enabled, /*right=*/!enabled);
}

void drawCursor(BackgroundMap& map, bool visible) {
    map[kEnableRow][kCursorCol] = visible ? kCursorGlyph : kSpace;
}

void paintModeScreen(BackgroundMap& map, const ModeScreenWiring& wiring) {
    clearVisibleRegion(map);

    const ModeScreenContent& content = wiring.content;
    writeMapText(map, kModeScreenTitleRow, centred(content.title.size()), content.title);

    writeMapText(map, kEnableRow, kLabelCol, "enable");
    paintEnableValue(map, enabledOf(wiring));

    writeMapText(map, kModeScreenFirstLine, centred(content.firstLine.size()), content.firstLine);
    writeMapText(map, kModeScreenSecondLine, centred(content.secondLine.size()),
                 content.secondLine);

    // Whatever the mode wants to show for itself. The rows below the description are cleared and
    // otherwise untouched, so a caller draws into them without having to blank them first.
    if (wiring.preview) {
        wiring.preview(map);
    }
}

// Turn the mode on or off. A press toward the value already held is an end stop: nothing changes,
// nothing is said, and the seam does not fire — the same law the settings screen's own rows follow.
void setEnabled(GameContext& game, const ModeScreenWiring& wiring, bool on) {
    if (wiring.enabled == nullptr || *wiring.enabled == on) {
        return;
    }
    *wiring.enabled = on;

    game.audioCues.square = SquareSfxId::TINK;
    if (wiring.changed) {
        wiring.changed();
    }
}

}  // namespace

void initModeScreen(GameContext& game, const ModeScreenWiring& wiring) {
    game.screens.cursorVisible = true;

    // The settings screen's own arrows are still in the buffer, and none of them belongs here.
    clearOamObjects(game);

    BackgroundMap& map = game.display.displayedMap();
    paintModeScreen(map, wiring);
    drawEnableArrows(game, enabledOf(wiring));
    drawCursor(map, game.screens.cursorVisible);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = GameState::MODE_SCREEN;
}

void modeScreen(GameContext& game, const ModeScreenWiring& wiring, const SettingsWiring& settings) {
    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        returnToSettings(game, settings);
        return;
    }

    if (pressed(game, Action::MenuRight)) {
        setEnabled(game, wiring, true);
    } else if (pressed(game, Action::MenuLeft)) {
        setEnabled(game, wiring, false);
    }

    BackgroundMap& map = game.display.displayedMap();
    paintEnableValue(map, enabledOf(wiring));
    drawEnableArrows(game, enabledOf(wiring));
    drawCursor(map, game.screens.cursorVisible);
}

void installModeScreenHandlers(GameStateDispatcher& dispatcher, ModeScreenWiring wiring,
                               SettingsWiring settings) {
    dispatcher.setHandler(GameState::INIT_MODE_SCREEN,
                          [wiring](GameContext& g) { initModeScreen(g, wiring); });
    dispatcher.setHandler(GameState::MODE_SCREEN,
                          [wiring = std::move(wiring), settings = std::move(settings)](
                              GameContext& g) { modeScreen(g, wiring, settings); });
}

}  // namespace kirpich::systems
