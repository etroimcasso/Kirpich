#pragma once

// A paged readout: a heading that names the page, a body the caller fills, and up and down to turn
// between pages. B goes back to whatever opened the screen.
//
// It is the third general screen machine, beside the carousel (systems/carousel_screen.h) and the
// list (systems/list_screen.h), and it exists because neither of those turns pages of computed
// figures. The carousel turns whole screens, but every one of its options carries a flag to toggle
// and a body of static prose held as a borrowed span - a figure is worked out when it is drawn, so it
// would need storage outliving the call. The list has the seam for that, and its up and down walk a
// cursor within one screen rather than moving between screens. This is the carousel's motion with the
// list's seam.
//
// **The screen never holds a line.** It clears the map, writes the heading, and asks the caller to
// fill the page; nothing it draws has to outlive the call. It draws no cursor of its own either,
// because a readout has no selectable row - a page that does carry one (a picker) draws it as part of
// its own body, which is where every other mark on the page comes from. The blink still runs, so the
// shared frame timer keeps its cadence for whatever the player returns to.
//
// A page that owns rows of its own takes the vertical walk first, through `walk`: it moves its cursor
// and says it consumed the step, and only a step it did not consume turns the page. That is what
// makes a picker and the page turn one continuous motion rather than two modes to switch between.
//
// Which page is up and how many there are live on ScreenUiState, as the list's window does and for
// the same reason: only one of these screens is ever on display, and the render bridge needs the
// count to know whether to draw an arrow.
//
// Every glyph is the caller's to choose, but the font is the same one every port screen draws from
// (tile indices $00-$26), which means the same picture under either tile regime.

#include <cstddef>
#include <functional>
#include <string_view>

#include <kirpich/game_state.h>

#include "systems/game_context.h"
#include "systems/settings_screen.h"  // the shared heading and page-arrow geometry

namespace kirpich::systems {

class GameStateDispatcher;

// Everything one instance needs from outside the game state.
//
// `count` and `title` are asked each frame and are handed the context, so an instance can name itself
// from what the player chose on the screen before it and can offer a different number of pages per
// branch. `paintPage` fills the body: it is called after the screen has cleared the displayed map and
// written the heading, and writes into game.display.displayedMap() - the map that was just cleared.
//
// `back` is what B does; a wiring that leaves it unset pops the navigation stack, which is what every
// screen in the statistics tree wants. `walk` is offered each vertical step before the page turns and
// returns true when it took it. `adjust` is left and right, which the machine has no meaning for at
// all - a page with nothing to change simply leaves it unset.
struct PageWiring {
    std::function<std::size_t(const GameContext&)>                        count;
    std::function<std::string_view(const GameContext&, std::size_t page)> title;
    std::function<void(GameContext&, std::size_t page)>                   paintPage;
    std::function<void(GameContext&)>                                     back;
    std::function<bool(GameContext&, std::size_t page, int delta)>        walk;
    std::function<void(GameContext&, std::size_t page, int delta)>        adjust;
};

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// The instance's init slot — start at the first page, empty the object buffer, paint, and enter
// `loop`.
//
// It does NOT save the caller's screen. A page opened over another screen is covering a picture that
// belongs to whoever saved it, and saving again here would store this screen as what the player
// returns to (see saveCallerScreen, systems/settings_screen.h).
void initPageScreen(GameContext& game, const PageWiring& wiring, GameState loop);

// The instance's loop slot — one frame: blink the cursor, offer the step to the page, turn the page
// if the page did not take it, change a value, or go back.
//
// The screen is laid out again every frame rather than patched, because a figure can change under it
// and because a page turn is a whole new screen.
void pageScreen(GameContext& game, const PageWiring& wiring);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install one instance into its two dispatch slots.
void installPageHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                         PageWiring wiring);

}  // namespace kirpich::systems
