#include "systems/stats_screens.h"

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include <kirpich/game_state.h>

#include "data/sfx.h"  // SquareSfxId
#include "systems/carousel_screen.h"
#include "systems/game_state_dispatcher.h"
#include "systems/list_screen.h"
#include "systems/screen.h"        // writeMapText
#include "systems/screen_stack.h"  // pushScreen, popScreen

namespace kirpich::systems {
namespace {

// ── What the screens say ──────────────────────────────────────────────────────────────────────────
//
// Wrapped by hand to the screen's twenty columns, and written without commas or apostrophes - the
// font has neither. A blank line is a paragraph break.

// The toggle's own screen. What it has to make clear is the thing a player would otherwise assume
// wrongly: switching this on does not start the record, it shows a record that was already being
// kept.
constexpr std::string_view kStatsDescription[] = {
    "the game records",
    "every round you",
    "play. this is kept",
    "whether or not you",
    "turn this on.",
    "",
    "on adds a stats",
    "item to the title",
    "screen.",
};

// ── The stats screen and the branch it opens ──────────────────────────────────────────────────────
//
// This screen IS the statistics, not a menu on the way to them. Its rows are the five things there
// are to look at - the whole game's totals, each game type's, and the achievements - so a player
// reaches figures one press from the title screen rather than three.
//
// Each row opens a branch whose own screen is paged, with related figures grouped a page at a time
// (a page of piece counts, a page of times, and so on). That paging is the branch's business, not
// this screen's: here a row is a destination, and nothing more.

// Which of the rows the player took. The branch screen titles itself from it, so one instance serves
// every row rather than each needing a pair of dispatch slots of its own.
//
// It cannot be static: it belongs to one install, as the carousel option tables do. It is allocated
// on install and held by every wiring copy that reads it.
struct StatsBranch {
    std::size_t row = 0;
};

constexpr std::string_view kChooserTitle = "stats";
constexpr std::string_view kChooserRows[] = {
    "all time", "mode a", "mode b", "mode c", "achievements",
};

// What the branch says until the trees behind it are built. The rows exist and lead somewhere; what
// they lead to is honest about being unfinished.
constexpr std::string_view kNotBuiltYet = "not built yet";

using StatsTable = std::array<CarouselOption, kStatsOptionCount>;

std::shared_ptr<const StatsTable> makeStatsOptions(Settings& settings) {
    std::array options{
        CarouselOption{
            .title = "stats", .body = kStatsDescription, .enabled = &settings.showStats},
    };
    static_assert(std::tuple_size_v<decltype(options)> == kStatsOptionCount,
                  "the stats screen's published option count and its table must say the same number");
    return std::make_shared<const StatsTable>(options);
}

ListWiring chooserWiring(std::shared_ptr<StatsBranch> branch) {
    return ListWiring{
        .title = [] { return kChooserTitle; },
        .count = [] { return std::size(kChooserRows); },
        .paintRow =
            [](BackgroundMap& map, std::size_t row, std::size_t line) {
                writeMapText(map, line, kListTextCol, kChooserRows[row]);
            },
        .chose =
            [branch](GameContext& game, std::size_t row) {
                branch->row           = row;
                game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
                pushScreen(game, GameState::INIT_STATS_LIST);
            },
        // B leaves the tree altogether, which means putting the title screen's own picture back -
        // the chooser is the one screen here that borders something it did not paint over itself.
        .back =
            [](GameContext& game) {
                restoreCallerScreen(game);
                popScreen(game);
            },
    };
}

ListWiring branchWiring(std::shared_ptr<const StatsBranch> branch) {
    return ListWiring{
        .title = [branch] { return kChooserRows[branch->row]; },
        .count = [] { return std::size_t{1}; },
        .paintRow =
            [](BackgroundMap& map, std::size_t /*row*/, std::size_t line) {
                writeMapText(map, line, kListTextCol, kNotBuiltYet);
            },
        // Nothing to act on yet. B pops back to the chooser, which is the default.
    };
}

}  // namespace

void openStatsMenu(GameContext& game) {
    saveCallerScreen(game);
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
    pushScreen(game, GameState::INIT_STATS_MENU);
}

void installStatsScreens(GameStateDispatcher& dispatcher, Settings& settings,
                         std::function<void()> changed, SettingsWiring settingsWiring) {
    // The toggle's own screen. Its option table is a borrowed span that installCarouselHandlers
    // copies into both handlers, and it holds a pointer into one particular Settings - so it cannot
    // be static and has to outlive the dispatcher. It is allocated here and carried in the `changed`
    // seam every copy of the wiring holds, exactly as the enhancement screens' tables are.
    auto             options = makeStatsOptions(settings);
    const std::span<const CarouselOption> view{*options};
    installCarouselHandlers(
        dispatcher, GameState::INIT_STATS_SCREEN, GameState::STATS_SCREEN,
        CarouselWiring{.options = view,
                       .changed =
                           [owner = std::move(options), changed = std::move(changed)] {
                               // `owner` holds the table alive for as long as any installed handler
                               // can read the span above. It is touched rather than left silent
                               // because a capture that reads as unused invites deletion, and
                               // deleting this one leaves that span dangling.
                               (void)owner;
                               if (changed) {
                                   changed();
                               }
                           }},
        std::move(settingsWiring));

    // The chooser and the branch share one record of which row was taken, so the branch can name
    // itself. It rides in the wirings the same way the option table rides in the seam above.
    auto branch = std::make_shared<StatsBranch>();
    installListHandlers(dispatcher, GameState::INIT_STATS_MENU, GameState::STATS_MENU,
                        chooserWiring(branch));
    installListHandlers(dispatcher, GameState::INIT_STATS_LIST, GameState::STATS_LIST,
                        branchWiring(std::move(branch)));
}

}  // namespace kirpich::systems
