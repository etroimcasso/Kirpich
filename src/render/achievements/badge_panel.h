#pragma once

// One badge, opened: the badge itself, and what there is to say about it.
//
// A hidden achievement that is still locked says nothing - the page opens on the badge alone, and the
// name and the criterion appear only once it has been earned. One that is not hidden, or has been
// earned, shows its name, what it asks for, and - if it is earned - the date and the play time it was
// earned at.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"  // AchievementId, AchievementState

namespace kirpich::render {

[[nodiscard]] Sprites BadgePanel(AchievementId id, const AchievementState& earned,
                                 const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
