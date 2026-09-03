#include "systems/list_screen.h"

#include <algorithm>
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
#include "systems/screen_stack.h"  // popScreen

namespace kirpich::systems {

namespace {

// The visible screen, and the interval the game's own selection screens blink on.
constexpr std::size_t  kScreenRows  = 18;
constexpr std::size_t  kScreenCols  = 20;
constexpr std::uint8_t kBlinkFrames = 16;

constexpr auto kSpace       = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr auto kCursorGlyph = static_cast<std::uint8_t>(CharTile::HYPHEN);

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

// How many rows the instance is offering, capped at what the count field can carry. A list longer
// than that is one nothing in this game builds, and clamping keeps the recorded count honest about
// what was drawn.
std::size_t rowCount(const ListWiring& wiring) {
    if (!wiring.count) {
        return 0;
    }
    return std::min<std::size_t>(wiring.count(), 255);
}

// Bring the selection inside the list and the window inside the selection.
//
// The window moves by as little as it can - one row when the cursor steps off an edge - so a walk
// scrolls rather than jumping a page at a time, and the rows a player was reading stay where they
// were.
void settleWindow(ScreenUiState& ui, std::size_t rows) {
    if (rows == 0) {
        ui.listRow = 0;
        ui.listTop = 0;
        return;
    }
    if (ui.listRow >= rows) {
        ui.listRow = static_cast<std::uint8_t>(rows - 1);
    }
    if (ui.listRow < ui.listTop) {
        ui.listTop = ui.listRow;
    } else if (ui.listRow >= ui.listTop + kListRows) {
        ui.listTop = static_cast<std::uint8_t>(ui.listRow - (kListRows - 1));
    }

    // A window that starts past what is left of the list would show blank lines under a scroll
    // indicator that says there is more. This happens when a list shortens between frames.
    const std::size_t lastTop = rows > kListRows ? rows - kListRows : 0;
    if (ui.listTop > lastTop) {
        ui.listTop = static_cast<std::uint8_t>(lastTop);
    }
}

// The whole screen: the heading, the visible rows, and the cursor beside the selected one.
void paintListScreen(GameContext& game, const ListWiring& wiring) {
    ScreenUiState& ui   = game.screens;
    BackgroundMap& map  = game.display.displayedMap();
    const std::size_t rows = rowCount(wiring);

    settleWindow(ui, rows);
    ui.listCount = static_cast<std::uint8_t>(rows);

    clearVisibleRegion(map);

    if (wiring.title) {
        const std::string_view title = wiring.title();
        writeMapText(map, kScreenTitleRow, centred(title.size()), title);
    }

    const std::size_t shown = std::min(kListRows, rows - std::min(rows, std::size_t{ui.listTop}));
    for (std::size_t offset = 0; offset < shown; ++offset) {
        const std::size_t row = ui.listTop + offset;
        if (wiring.paintRow) {
            wiring.paintRow(map, row, listRowLine(offset));
        }
    }

    if (rows != 0 && ui.cursorVisible) {
        map[listRowLine(ui.listRow - ui.listTop)][kListCursorCol] = kCursorGlyph;
    }
}

// Move the cursor one row, if there is one that way. An end stop moves nothing and says nothing.
void moveCursor(GameContext& game, const ListWiring& wiring, int delta) {
    const std::size_t rows = rowCount(wiring);
    const int         next = static_cast<int>(game.screens.listRow) + delta;
    if (rows == 0 || next < 0 || next >= static_cast<int>(rows)) {
        return;
    }
    game.screens.listRow  = static_cast<std::uint8_t>(next);
    game.audioCues.square = SquareSfxId::TINK;
}

}  // namespace

void initListScreen(GameContext& game, const ListWiring& wiring, GameState loop) {
    ScreenUiState& ui = game.screens;

    ui.listRow       = 0;
    ui.listTop       = 0;
    ui.cursorVisible = true;

    // Whatever the screen this one was opened from left in the buffer belongs to that screen, and
    // none of it belongs here.
    clearOamObjects(game);

    paintListScreen(game, wiring);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = loop;
}

void listScreen(GameContext& game, const ListWiring& wiring) {
    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        if (wiring.back) {
            wiring.back(game);
        } else {
            popScreen(game);
        }
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        return;
    }

    if (pressed(game, Action::Confirm) || pressed(game, Action::Start)) {
        if (wiring.chose && rowCount(wiring) != 0) {
            wiring.chose(game, game.screens.listRow);
        }
        return;
    }

    if (pressed(game, Action::MenuUp)) {
        moveCursor(game, wiring, -1);
    } else if (pressed(game, Action::MenuDown)) {
        moveCursor(game, wiring, +1);
    }

    paintListScreen(game, wiring);
}

void installListHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                         ListWiring wiring) {
    dispatcher.setHandler(init, [wiring, loop](GameContext& g) {
        initListScreen(g, wiring, loop);
    });
    dispatcher.setHandler(
        loop, [wiring = std::move(wiring)](GameContext& g) { listScreen(g, wiring); });
}

}  // namespace kirpich::systems
