#pragma once

// A scrollable list: a heading, a window of rows, a cursor that walks them, and a row the player can
// act on. Up and down move the cursor; the window follows it when it would leave; A or Start reports
// the row; B goes back to whatever opened the screen.
//
// It is the second general screen machine, beside the carousel (systems/carousel_screen.h), and it
// exists because the carousel cannot do this: a carousel shows one option per screen, and a list of
// sixty combinations or a readout of ten figures needs a window over rows that do not fit.
//
// The machine owns nothing per-instance. How many rows there are, what each one looks like, and what
// acting on one does all arrive through the wiring, so a second list is a second install rather than
// a second screen. The heading and the rows arrive the same way, which is what lets one instance
// title itself from what the player chose on the one before it.
//
// **The screen never holds a row.** It asks the caller to paint row n at map line l and forgets it,
// so a row whose text has to be computed - a duration, a total, a name assembled from two numbers -
// is formatted into a local and written, and nothing has to outlive the call. That is deliberately
// unlike the carousel, whose options are a borrowed span that each instance has to keep alive.
//
// The two end indicators are the shared page arrows, drawn only where there is more list in that
// direction - the range-end law every scroller in the game follows. They are sprites rather than
// objects because an arrow standing on end needs a quarter turn, and an object carries only the two
// flips the hardware has; the render bridge draws them (render/settings_overlay.h, listArrows).
//
// Which row is selected and which one the window starts at live on ScreenUiState, as the carousel's
// shown option does and for the same reason: only one list is ever on screen, and each instance's
// init starts both at zero.
//
// Every glyph is the caller's to choose, but the font is the same one every port screen draws from
// (tile indices $00-$26), which means the same picture under either tile regime.

#include <cstddef>
#include <functional>
#include <string_view>

#include <kirpich/game_state.h>

#include "state/display_state.h"  // BackgroundMap
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // the shared heading and page-arrow geometry

namespace kirpich::systems {

class GameStateDispatcher;

// ── Where the drawn parts sit ─────────────────────────────────────────────────────────────────────
//
// The heading is the row every screen in this family uses, which is also what places the up arrow
// above it. The rows run one to a line so a long list shows as much of itself as it can.

inline constexpr std::size_t kListFirstRow = 5;
inline constexpr std::size_t kListLastRow  = 15;
inline constexpr std::size_t kListRows     = kListLastRow - kListFirstRow + 1;

// Where the cursor sits, and the first column a row's own text may use. Both match the settings
// screen's, so a list reads as a sibling of the screens the player walked to get here.
inline constexpr std::size_t kListCursorCol = 1;
inline constexpr std::size_t kListTextCol   = 3;

static_assert(kListLastRow < kPageDownArrowRow,
              "the rows must stop short of the down arrow's row, or the arrow would sit on a row");

// Which map line a visible row is drawn on. `offset` counts from the top of the window, not from the
// top of the list.
[[nodiscard]] constexpr std::size_t listRowLine(std::size_t offset) noexcept {
    return kListFirstRow + offset;
}

// Everything one instance needs from outside the game state.
//
// `title` and `count` are asked each frame, so an instance can name itself from what the player chose
// on the screen before it and can show a list whose length is not fixed. `paintRow` is handed the map
// and the line to write, and owns everything from kListTextCol rightwards. `chose` is the row the
// player acted on. `back` is what B does; a wiring that leaves it unset pops the navigation stack,
// which is what every list in the statistics tree wants.
struct ListWiring {
    std::function<std::string_view()>                                      title;
    std::function<std::size_t()>                                           count;
    std::function<void(BackgroundMap&, std::size_t row, std::size_t line)> paintRow;
    std::function<void(GameContext&, std::size_t row)>                     chose;
    std::function<void(GameContext&)>                                      back;
};

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// The instance's init slot — start at the first row with the window at the top, paint the screen, and
// enter `loop`.
//
// It does NOT save the caller's screen. A list opened over another screen is covering a picture that
// belongs to whoever saved it, and saving again here would store this screen as what the player
// returns to (see saveCallerScreen, systems/settings_screen.h).
void initListScreen(GameContext& game, const ListWiring& wiring, GameState loop);

// The instance's loop slot — one frame: blink the cursor, walk the rows, act on one, or go back.
//
// The screen is laid out again every frame rather than patched. A list's rows are asked for on every
// pass anyway, since a row's text can be a figure that has just changed, and repainting is also what
// makes returning to a list from a screen it opened show the list rather than what covered it.
void listScreen(GameContext& game, const ListWiring& wiring);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install one instance into its two dispatch slots.
void installListHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                         ListWiring wiring);

}  // namespace kirpich::systems
