#pragma once

// One achievement's badge: the sprites that are its icon.
//
// Earned and locked are the same art through a different palette. A locked badge draws faded - the
// player's own ramp inked in its light shade, the way the config screen greys a choice that is not
// the selected one - so an earned one is the solid one. It fades within the ramp rather than falling
// back to another ramp, because a ramp whose darkest shade is black is indistinguishable from any
// other ramp's black: a difference carried by hue alone disappears on those.
//
// A badge does not know whether it is selected: the cursor is its own object drawn around it
// (render/selection_corners.h), so a grid reads the same however it is being walked.
//
// The art each achievement wears is its section's emblem, taken from art the game already has. That
// mapping is data: changing it changes no logic and no saved byte.

#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"  // AchievementId

namespace kirpich::render {

[[nodiscard]] Sprites AchievementBadge(AchievementId id, int x, int y, bool unlocked,
                                       const TileAtlas& atlas, std::uint8_t ramp);

// How much room a badge's art actually takes, in pixels. The set's emblems are different shapes - a
// square of four, a bar of four across, a figure three tall - so anything drawn around a badge has to
// ask how big this one is rather than assume a size.
struct BadgeExtent {
    int width  = 0;
    int height = 0;

    friend constexpr bool operator==(const BadgeExtent&, const BadgeExtent&) = default;
};

[[nodiscard]] BadgeExtent badgeExtent(AchievementId id);

}  // namespace kirpich::render
