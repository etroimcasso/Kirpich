#pragma once

// One achievement announced: its badge, its title beside it, and what it asked for under the title.
//
// The badge draws earned, because the banner is only ever shown for one that has just been earned -
// which is also why a hidden achievement shows its real title and description here. Being earned is
// what reveals it; the placeholder copy belongs to the screen that lists what has not been.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"  // AchievementId

namespace kirpich::render {

[[nodiscard]] Sprites AchievementBanner(AchievementId id, int x, int y, const TileAtlas& atlas,
                                        std::uint8_t ramp);

}  // namespace kirpich::render
