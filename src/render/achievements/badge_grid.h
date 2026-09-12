#pragma once

// One section's badges, laid out in the grid.
//
// The badges are the section's own, in the order the set holds them, and where each one sits comes
// from the layout. It knows nothing about the cursor: what is selected is drawn around the badge by
// the selector (render/selection_corners.h), not into it.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"  // AchievementState
#include "systems/achievements.h"     // AchievementSection

namespace kirpich::render {

[[nodiscard]] Sprites BadgeGrid(systems::AchievementSection section, const AchievementState& earned,
                                const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
