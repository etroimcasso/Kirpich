#include "systems/enhancement_screens.h"

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include <kirpich/game_state.h>

#include "systems/carousel_screen.h"
#include "systems/game_state_dispatcher.h"
#include "systems/mode_screen.h"

namespace kirpich::systems {
namespace {

// ── What the screens say ──────────────────────────────────────────────────────────────────────────
//
// Each block is wrapped by hand to the screen's twenty columns, and written without commas or
// apostrophes — the font has neither. A blank line is a paragraph break.

// The new-modes screen, opened from the settings row of the same name. It explains what turning them
// on does and carries the switch itself, because a sentence does not fit in a settings row's three
// cells.
constexpr std::string_view kNewModesDescription[] = {
    "type c is a marathon",
    "over a rising floor.",
    "",
    "every ten drops it",
    "comes up a row and a",
    "new line of junk",
    "arrives below.",
    "",
    "clearing a line buys",
    "back one drop.",
};

// The fixes screen, opened from the settings row of the same name: the cartridge's own defects
// offered back, one option to a screen, every one off by default so fidelity is what a player has
// until they ask otherwise. The carousel behind it is general — a future option is another entry in
// the table below, nothing more.
constexpr std::string_view kAudioFixDescription[] = {
    "after a demo plays",
    "the title screen",
    "stays silent. this",
    "is the original",
    "behavior.",
    "",
    "turn this on and",
    "the music returns",
    "when a demo ends.",
};

// The ghost piece's own screen, opened from its row: the second carousel instance, carrying the
// switch beside the room to say what it does.
constexpr std::string_view kGhostDescription[] = {
    "the falling piece",
    "casts a shadow on",
    "the row where it",
    "will land.",
    "",
    "off is the",
    "original behavior.",
};

// ── The option tables, and what keeps them alive ───────────────────────────────────────────────────
//
// A carousel's wiring carries its options as a borrowed span, and installCarouselHandlers copies
// that wiring into both of the handlers it installs — so the table has to outlive the dispatcher.
// A table also cannot be static: every option holds a pointer into one particular Settings, and a
// second install against a second Settings would find the first one's flags.
//
// So each table is allocated on install and its owner rides in the `changed` seam that every copy of
// the wiring carries. The table then lives exactly as long as a handler that can read it, and goes
// when the dispatcher does.

using FixesTable = std::array<CarouselOption, kFixesOptionCount>;
using GhostTable = std::array<CarouselOption, kGhostOptionCount>;

std::shared_ptr<const FixesTable> makeFixesOptions(Settings& settings) {
    std::array options{
        CarouselOption{
            .title = "audio", .body = kAudioFixDescription, .enabled = &settings.fixAudio},
    };
    static_assert(std::tuple_size_v<decltype(options)> == kFixesOptionCount,
                  "the fixes screen's published option count and its table must say the same number");
    return std::make_shared<const FixesTable>(options);
}

std::shared_ptr<const GhostTable> makeGhostOptions(Settings& settings) {
    std::array options{
        CarouselOption{
            .title = "ghost", .body = kGhostDescription, .enabled = &settings.ghostPiece},
    };
    static_assert(std::tuple_size_v<decltype(options)> == kGhostOptionCount,
                  "the ghost screen's published option count and its table must say the same number");
    return std::make_shared<const GhostTable>(options);
}

// Install one carousel instance whose option table this unit owns — see the note above for what the
// captured owner is doing.
template <typename Table>
void installOwnedCarousel(GameStateDispatcher& dispatcher, GameState init, GameState loop,
                          std::shared_ptr<const Table> options, std::function<void()> changed,
                          SettingsWiring settings) {
    const std::span<const CarouselOption> view{*options};
    installCarouselHandlers(
        dispatcher, init, loop,
        CarouselWiring{.options = view,
                       .changed =
                           [owner = std::move(options), changed = std::move(changed)] {
                               // `owner` holds the option table alive for as long as any installed
                               // handler can read the span above. It is touched rather than left
                               // silent because a capture that reads as unused invites deletion,
                               // and deleting this one leaves that span dangling.
                               (void)owner;
                               if (changed) {
                                   changed();
                               }
                           }},
        std::move(settings));
}

}  // namespace

void installEnhancementScreens(GameStateDispatcher& dispatcher, Settings& settings,
                               std::function<void()> changed, SettingsWiring settingsWiring) {
    // No preview drawing on the mode screen. It draws from the font alone so that it reads the same
    // whichever tile art is loaded behind it, and a font of letters cannot draw a playing field.
    //
    // Its body span points at this file's own static array and installModeScreenHandlers copies the
    // wiring by value, so nothing here needs the option tables' keep-alive treatment.
    installModeScreenHandlers(dispatcher,
                              ModeScreenWiring{
                                  .content = {.title = "new modes", .body = kNewModesDescription},
                                  .enabled = &settings.newModes,
                                  .changed = changed,
                              },
                              settingsWiring);

    installOwnedCarousel(dispatcher, GameState::INIT_FIXES_SCREEN, GameState::FIXES_SCREEN,
                         makeFixesOptions(settings), changed, settingsWiring);

    installOwnedCarousel(dispatcher, GameState::INIT_GHOST_SCREEN, GameState::GHOST_SCREEN,
                         makeGhostOptions(settings), std::move(changed), std::move(settingsWiring));
}

}  // namespace kirpich::systems
