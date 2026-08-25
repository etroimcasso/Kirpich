#include "systems/carousel_screen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "state/display_state.h"
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

// The enable row borrows the settings screen's own geometry - the label column, the value field and
// the two arrow columns - so the row lines up with the rows the player has just walked past. The
// mode screen sits on the same row for the same reason.
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

// The option on show. The index is clamped rather than trusted because the wiring and the state
// arrive separately: a stale index against a shorter option list reads the last option rather than
// past the span.
const CarouselOption* shownOption(const GameContext& game, const CarouselWiring& wiring) {
    if (wiring.options.empty()) {
        return nullptr;
    }
    const std::size_t index =
        std::min<std::size_t>(game.screens.carouselOption, wiring.options.size() - 1);
    return &wiring.options[index];
}

bool enabledOf(const CarouselOption* option) {
    return option != nullptr && option->enabled != nullptr && *option->enabled;
}

void paintEnableValue(BackgroundMap& map, bool enabled) {
    for (std::size_t i = 0; i < kOptionValueWidth; ++i) {
        map[kEnableRow][kOptionValueCol + i] = kSpace;
    }
    writeMapText(map, kEnableRow, kOptionValueCol, enabled ? "on" : "off");
}

// The enable row's arrows, in the first two object entries. A row already at one end of its range
// loses that arrow, the same law every settings row follows.
void drawEnableArrows(GameContext& game, bool enabled) {
    placeScrollerArrows(game, 0, kEnableRow, /*left=*/enabled, /*right=*/!enabled);
}

void drawCursor(BackgroundMap& map, bool visible) {
    map[kEnableRow][kCursorCol] = visible ? kCursorGlyph : kSpace;
}

// One option's whole screen. A different option is a different screen, so moving repaints through
// here rather than patching the parts that changed.
void paintCarouselScreen(BackgroundMap& map, const GameContext& game,
                         const CarouselWiring& wiring) {
    clearVisibleRegion(map);

    const CarouselOption* option = shownOption(game, wiring);
    if (option == nullptr) {
        return;  // an instance with no options is an empty screen, and B still leaves it
    }

    writeMapText(map, kCarouselTitleRow, centred(option->title.size()), option->title);

    writeMapText(map, kEnableRow, kLabelCol, "enable");
    paintEnableValue(map, enabledOf(option));

    // The body, one line per row from kCarouselBodyFirstRow down. An empty line is simply a row
    // left blank, which is how a caller separates paragraphs; anything past the body's last row is
    // not drawn, because the down arrow's row is below it.
    const std::size_t lines = std::min(option->body.size(), kCarouselBodyRows);
    for (std::size_t i = 0; i < lines; ++i) {
        const std::string_view line = option->body[i];
        if (!line.empty()) {
            writeMapText(map, kCarouselBodyFirstRow + i, centred(line.size()), line);
        }
    }
}

// Turn the shown option on or off. A press toward the value already held is an end stop: nothing
// changes, nothing is said, and the seam does not fire - the same law the settings screen's own
// rows follow.
void setEnabled(GameContext& game, const CarouselWiring& wiring, bool on) {
    const CarouselOption* option = shownOption(game, wiring);
    if (option == nullptr || option->enabled == nullptr || *option->enabled == on) {
        return;
    }
    *option->enabled = on;

    game.audioCues.square = SquareSfxId::TINK;
    if (wiring.changed) {
        wiring.changed();
    }
}

// Move to the option above or below, if there is one. The move is a repaint - a different option is
// a different screen - and an end stop moves nothing and says nothing.
void moveOption(GameContext& game, const CarouselWiring& wiring, int delta) {
    const int next = static_cast<int>(game.screens.carouselOption) + delta;
    if (next < 0 || next >= static_cast<int>(wiring.options.size())) {
        return;
    }
    game.screens.carouselOption = static_cast<std::uint8_t>(next);
    game.audioCues.square       = SquareSfxId::TINK;

    BackgroundMap& map = game.display.displayedMap();
    paintCarouselScreen(map, game, wiring);
    drawEnableArrows(game, enabledOf(shownOption(game, wiring)));
    drawCursor(map, game.screens.cursorVisible);
}

}  // namespace

void initCarouselScreen(GameContext& game, const CarouselWiring& wiring, GameState loop) {
    game.screens.carouselOption = 0;
    game.screens.cursorVisible  = true;

    // The settings screen's own arrows are still in the buffer, and none of them belongs here.
    clearOamObjects(game);

    BackgroundMap& map = game.display.displayedMap();
    paintCarouselScreen(map, game, wiring);
    drawEnableArrows(game, enabledOf(shownOption(game, wiring)));
    drawCursor(map, game.screens.cursorVisible);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = loop;
}

void carouselScreen(GameContext& game, const CarouselWiring& wiring,
                    const SettingsWiring& settings) {
    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        returnToSettings(game, settings);
        return;
    }

    if (pressed(game, Action::MenuUp)) {
        moveOption(game, wiring, -1);
    } else if (pressed(game, Action::MenuDown)) {
        moveOption(game, wiring, +1);
    } else if (pressed(game, Action::MenuRight)) {
        setEnabled(game, wiring, true);
    } else if (pressed(game, Action::MenuLeft)) {
        setEnabled(game, wiring, false);
    }

    BackgroundMap& map = game.display.displayedMap();
    paintEnableValue(map, enabledOf(shownOption(game, wiring)));
    drawEnableArrows(game, enabledOf(shownOption(game, wiring)));
    drawCursor(map, game.screens.cursorVisible);
}

void installCarouselHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                             CarouselWiring wiring, SettingsWiring settings) {
    dispatcher.setHandler(init, [wiring, loop](GameContext& g) {
        initCarouselScreen(g, wiring, loop);
    });
    dispatcher.setHandler(loop, [wiring = std::move(wiring), settings = std::move(settings)](
                                    GameContext& g) { carouselScreen(g, wiring, settings); });
}

}  // namespace kirpich::systems
