#pragma once

// The enhancement screens' own content: what the ghost, fixes and new-modes screens say, which flag
// each option binds, and the install that puts all three onto the dispatcher.
//
// The carousel (systems/carousel_screen.h) and the mode screen (systems/mode_screen.h) are the
// machines and own no words; the host (src/main.cpp) is the wiring and owns no content. This unit is
// where the content lives - the option tables and the prose under each enable row - so a new fix, a
// new screen or a reworded description is an edit here, never in the host. The host contributes the
// two things that are genuinely its own: the settings the flags reach into, and the seam a change
// fires.
//
// The option counts are published for the render layer, whose arrow rule needs to know how many
// options the shown carousel holds (render/settings_overlay.h, carouselArrows). Each count is
// static-asserted against its table in the .cpp, so the two cannot drift.

#include <cstddef>
#include <functional>

#include "state/settings.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// How many options each carousel offers. The fixes screen grows as quirks worth offering back are
// found; the ghost screen is one option by nature.
inline constexpr std::size_t kFixesOptionCount = 1;
inline constexpr std::size_t kGhostOptionCount = 1;

// Install all three enhancement screens - the fixes carousel on INIT_FIXES_SCREEN/FIXES_SCREEN, the
// ghost carousel on INIT_GHOST_SCREEN/GHOST_SCREEN, and the new-modes screen on
// INIT_MODE_SCREEN/MODE_SCREEN - with their options bound to `settings`' flags. `changed` fires on
// every toggle, which is where the host applies and saves; the settings wiring is what leaving any
// of them repaints.
//
// `settings` is held by reference in the installed handlers, so it must outlive the dispatcher -
// the same lifetime the settings wiring's own pointer already demands.
void installEnhancementScreens(GameStateDispatcher& dispatcher, Settings& settings,
                               std::function<void()> changed, SettingsWiring settingsWiring);

}  // namespace kirpich::systems
