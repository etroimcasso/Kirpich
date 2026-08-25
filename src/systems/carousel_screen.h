#pragma once

// A carousel of option screens: one option at a time, each with a screen of its own - a title, an
// enable row in the settings screen's scroller geometry, and the room to say what the option means
// before a player decides. Up and down move between options; left and right turn the shown one on
// and off; B goes back to the settings screen.
//
// The machine is general and owns nothing per-instance: the options, the flag each enable row
// toggles, and the two dispatch slots an instance answers to all arrive through the installer, so a
// second carousel is a second install rather than a second screen. The fixes screen is the first
// instance - the cartridge's own defects offered back, off by default, fidelity until a player asks
// otherwise. A mode screen with more than one mode to offer would be another.
//
// The up arrow sits ABOVE the title: the title belongs to the option, so the arrow saying "there is
// another option" has to sit outside what the option owns, or it would read as the description
// scrolling. The down arrow sits below the description for the same reason. An arrow is drawn only
// where there is an option in that direction, the law every scroller on the settings screen
// follows; with one option neither is drawn.
//
// It owns no state of its own. Which option is shown lives on ScreenUiState - one field serves
// every instance, because only one carousel is ever on screen - the flags belong to the caller
// (they are saved settings, which outlive a launch), and both reach the handlers through the
// wiring. The blink is the one every port screen shares.
//
// Every glyph it draws comes from the font (tile indices $00-$26), which means the same picture
// under either tile regime - so the screen reads correctly whether it was opened from the title
// screen or from a paused round, as the settings screen behind it does.

#include <functional>
#include <span>
#include <string_view>

#include <kirpich/game_state.h>

#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring, the shared scroller geometry

namespace kirpich::systems {

class GameStateDispatcher;

// One option: what its screen calls it, the prose under the enable row, and the flag the row
// toggles. The flag is a pointer because the caller owns it - it is a saved setting.
//
// The caller wraps its own text: a line wider than the twenty-cell screen is clipped, and there is
// no word wrapping here. An empty line is a blank row, which is how paragraphs are separated. Lines
// past the body's last row are not drawn. The font has letters, digits, a period and a hyphen; it
// has no comma and no apostrophe, so prose written for this screen has to do without them.
struct CarouselOption {
    std::string_view                  title;
    std::span<const std::string_view> body;
    bool*                             enabled = nullptr;
};

// Everything one instance needs from outside the game state: its options, in the order up and down
// walk them, and a seam that fires on every change - which is where the caller applies and writes
// the settings out, the way every settings row does.
struct CarouselWiring {
    std::span<const CarouselOption> options{};
    std::function<void()>           changed;
};

// Where the screen's parts sit. The title, enable row and body share the mode screen's rows, so the
// screens a settings row can open read as siblings; the enable row is the settings screen's own
// first scroller row (settingsRowLine geometry), as the mode screen's is. The up arrow is the
// carousel's own, above the title; the down arrow sits below the body, and the body stops a row
// short of the mode screen's so the arrow has a row of its own.
inline constexpr std::size_t kCarouselTitleRow     = 2;
inline constexpr std::size_t kCarouselBodyFirstRow = 7;
inline constexpr std::size_t kCarouselBodyLastRow  = 15;
inline constexpr std::size_t kCarouselBodyRows = kCarouselBodyLastRow - kCarouselBodyFirstRow + 1;
inline constexpr std::size_t kCarouselUpArrowRow   = 1;
inline constexpr std::size_t kCarouselDownArrowRow = 16;

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// The instance's init slot — start at the first option, paint its screen over the map the settings
// screen is on, place the enable row's arrows, and enter `loop`.
//
// It does NOT save the caller's screen. The settings screen already holds the picture this one is
// covering, and saving again here would store this screen as what a player returns to.
void initCarouselScreen(GameContext& game, const CarouselWiring& wiring, GameState loop);

// The instance's loop slot — one frame: blink the cursor, move to another option (a repaint - a
// different option is a different screen), turn the shown one on or off, or go back to the settings
// screen. A change fires `changed` as it is made.
void carouselScreen(GameContext& game, const CarouselWiring& wiring, const SettingsWiring& settings);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install one instance into its two dispatch slots. The settings wiring is needed because leaving
// the carousel repaints the screen that opened it.
void installCarouselHandlers(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                             CarouselWiring wiring, SettingsWiring settings);

}  // namespace kirpich::systems
