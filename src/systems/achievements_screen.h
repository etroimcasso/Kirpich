#pragma once

// The achievements screen's logic: which section is on the page, where the cursor sits, which badge
// is open, and what a press does to those. It draws nothing and names no drawing type - the picture
// is a function of this state, built by the components under src/render/achievements/.
//
// One section is on the page at a time. The cursor walks that section's badges; walking off the
// bottom turns to the next section and off the top to the previous, keeping its column and landing in
// the row it stepped into, so the whole set is one continuous grid whose only end stops are the first
// section's top and the last section's bottom. A opens the badge under the cursor, B closes an open
// badge or leaves the screen.
//
// Input is a dispatch table - one table per mode, mapping an action to what it does to the state -
// so what a button does is data a reader sees in one place.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <kirpich/game_state.h>

#include "systems/achievements.h"  // AchievementDef, AchievementSection, achievementSet
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// How many themed sections there are - one page each. Tied to the last section so the page count and
// the section enum cannot drift.
inline constexpr std::size_t kAchievementSectionCount =
    static_cast<std::size_t>(AchievementSection::THE_LONG_GAME) + 1;

// The grid is this many badges across; the row count follows from how many a section holds. Three
// columns keeps the widest section (six badges) to two rows. The cursor law owns it, because how many
// columns there are is what turns a position into a row and a column.
inline constexpr std::size_t kAchievementGridCols = 3;

// The section at a stored index, clamped so a stale value names a real section.
[[nodiscard]] AchievementSection achievementSectionAt(std::uint8_t index) noexcept;

// A section's heading. The set groups by section but does not name the groups, so the names live with
// the screen that shows them.
[[nodiscard]] std::string_view achievementSectionTitle(AchievementSection section) noexcept;

// How many badges a section holds, and the one at a position in it. The set is grouped by section in
// id order, so a section is a run of it; a position past the run clamps to the last.
[[nodiscard]] std::size_t           achievementSectionBadgeCount(AchievementSection section) noexcept;
[[nodiscard]] const AchievementDef& achievementSectionBadge(AchievementSection section,
                                                            std::size_t        position) noexcept;

// Whether the player has earned a given achievement.
[[nodiscard]] bool achievementUnlocked(const AchievementState& earned, AchievementId id) noexcept;

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// Open the screen at the first section, first badge, nothing open; empty the object buffer, which
// belongs to whichever screen was up before; arm the shared cursor blink. It writes no background map
// and saves no caller: the picture is the components', and the chooser it was opened from draws
// itself again when the player comes back.
void initAchievementsScreen(GameContext& game);

// One frame: blink the cursor, then run the active input table. Mutates only state.
void achievementsScreen(GameContext& game);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

void installAchievementsScreen(GameStateDispatcher& dispatcher);

}  // namespace kirpich::systems
