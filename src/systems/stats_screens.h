#pragma once

// The statistics screens' own content: what the toggle's screen says, what the chooser offers, and
// the install that puts all of it onto the dispatcher.
//
// The carousel (systems/carousel_screen.h) and the list (systems/list_screen.h) are the machines and
// own no words; the host (src/main.cpp) is the wiring and owns no content. This unit is where the
// content lives, beside systems/enhancement_screens.h which does the same for the three screens a
// settings row opens.
//
// Three screens ship here:
//
//   - the toggle's own screen, a carousel instance with one option, opened from the settings screen's
//     stats row. It carries Settings::showStats, which governs only whether the statistics are
//     OFFERED - the tables fill either way (systems/stats.h).
//   - the statistics screen itself, a list instance whose five rows are the whole game's figures,
//     each game type's, and the achievements. It is opened from the title screen's stats item once
//     the toggle is on, and it is where statistics and achievements meet.
//   - the branch each row opens, a paged screen (systems/page_screen.h) whose pages are the content
//     in systems/stats_pages.h. One instance serves all five: which row was taken is recorded on
//     ScreenUiState, and the pages fork on it.
//
// The option count is published for the render layer, whose arrow rule needs to know how many options
// the shown carousel holds (render/settings_overlay.h, carouselArrows). It is static-asserted against
// the table in the .cpp, so the two cannot drift.

#include <cstddef>
#include <functional>

#include "state/settings.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// How many options the statistics toggle's screen offers. One by nature, as the ghost screen's is.
inline constexpr std::size_t kStatsOptionCount = 1;

// Enter the chooser from the title screen: take the title screen's picture so it can be put back
// untouched, and record it as the screen to come back to.
//
// The title screen's own init would rebuild the picture, but it also clears the round just played,
// re-cues the music and re-arms the attract countdown - so coming back through it would be a reset
// rather than a return.
void openStatsMenu(GameContext& game);

// Install all three statistics screens - the toggle's carousel on INIT_STATS_SCREEN/STATS_SCREEN, the
// statistics screen on INIT_STATS_MENU/STATS_MENU, and the branch on INIT_STATS_PAGE/STATS_PAGE -
// with the toggle bound to `settings`' flag. `changed` fires on every toggle, which is where the host
// applies and saves; the settings wiring is what leaving the toggle's screen repaints.
//
// `settings` is held by reference in the installed handlers, so it must outlive the dispatcher - the
// same lifetime the settings wiring's own pointer already demands.
void installStatsScreens(GameStateDispatcher& dispatcher, Settings& settings,
                         std::function<void()> changed, SettingsWiring settingsWiring);

}  // namespace kirpich::systems
