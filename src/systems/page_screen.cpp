#include "systems/page_screen.h"

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

constexpr auto kSpace = static_cast<std::uint8_t>(CharTile::SPACE);

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

// How many pages the instance is offering, capped at what the count field can carry.
std::size_t pageCount(const GameContext& game, const PageWiring& wiring) {
    if (!wiring.count) {
        return 0;
    }
    return std::min<std::size_t>(wiring.count(game), 255);
}

// The whole screen: the heading, and whatever the page puts under it.
void paintPageScreen(GameContext& game, const PageWiring& wiring) {
    ScreenUiState&    ui    = game.screens;
    const std::size_t pages = pageCount(game, wiring);

    // A branch can offer fewer pages than the one visited before it.
    if (pages == 0) {
        ui.statsPage = 0;
    } else if (ui.statsPage >= pages) {
        ui.statsPage = static_cast<std::uint8_t>(pages - 1);
    }
    ui.statsPageCount = static_cast<std::uint8_t>(pages);

    BackgroundMap& map = game.display.displayedMap();
    clearVisibleRegion(map);

    if (wiring.title) {
        const std::string_view title = wiring.title(game, ui.statsPage);
        writeMapText(map, kScreenTitleRow, centred(title.size()), title);
    }

    if (wiring.paintPage && pages != 0) {
        wiring.paintPage(game, ui.statsPage);
    }
}

// Turn one page, if there is one that way. An end stop moves nothing and says nothing.
void turnPage(GameContext& game, const PageWiring& wiring, int delta) {
    const std::size_t pages = pageCount(game, wiring);
    const int         next  = static_cast<int>(game.screens.statsPage) + delta;
    if (pages == 0 || next < 0 || next >= static_cast<int>(pages)) {
        return;
    }
    game.screens.statsPage = static_cast<std::uint8_t>(next);
    game.audioCues.square  = SquareSfxId::TINK;
}

// One vertical step: the page takes it if it has rows of its own, and it turns the page otherwise.
void stepVertically(GameContext& game, const PageWiring& wiring, int delta) {
    if (wiring.walk && wiring.walk(game, game.screens.statsPage, delta)) {
        return;
    }
    turnPage(game, wiring, delta);
}

}  // namespace

void initPageScreen(GameContext& game, const PageWiring& wiring, GameState loop) {
    ScreenUiState& ui = game.screens;

    ui.statsPage     = 0;
    ui.cursorVisible = true;

    // Whatever the screen this one was opened from left in the buffer belongs to that screen, and
    // none of it belongs here. A page that places objects of its own does so as it paints.
    clearOamObjects(game);

    paintPageScreen(game, wiring);

    game.flow.timer1    = kBlinkFrames;
    game.flow.gameState = loop;
}

void pageScreen(GameContext& game, const PageWiring& wiring) {
    // A page that owns rows can leave the screen from its own first row, so what the frame started on
    // is what says whether this screen is still the one on display when the frame ends.
    const GameState entered = game.flow.gameState;

    blinkScreenCursor(game);

    if (pressed(game, Action::Back)) {
        if (wiring.back) {
            wiring.back(game);
        } else {
            popScreen(game);
        }
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
    } else if (pressed(game, Action::MenuUp)) {
        stepVertically(game, wiring, -1);
    } else if (pressed(game, Action::MenuDown)) {
        stepVertically(game, wiring, +1);
    } else if (pressed(game, Action::MenuLeft)) {
        if (wiring.adjust) wiring.adjust(game, game.screens.statsPage, -1);
    } else if (pressed(game, Action::MenuRight)) {
        if (wiring.adjust) wiring.adjust(game, game.screens.statsPage, +1);
    }

    // A step out of this screen has already chosen its own state; painting over the map on the way
    // out would put this screen's picture on whatever the player is going back to.
    if (game.flow.gameState != entered) {
        // The objects a page placed are this screen's, and the screen underneath is returned to at
        // its loop slot rather than through its init - so nothing else would empty them, and a
        // picker's arrows would still be standing on a screen that has no picker.
        clearOamObjects(game);
        return;
    }

    paintPageScreen(game, wiring);
}

void installPageHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                         PageWiring wiring) {
    dispatcher.setHandler(init, [wiring, loop](GameContext& g) {
        initPageScreen(g, wiring, loop);
    });
    dispatcher.setHandler(
        loop, [wiring = std::move(wiring)](GameContext& g) { pageScreen(g, wiring); });
}

}  // namespace kirpich::systems
