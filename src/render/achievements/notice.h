#pragma once

// The end-of-round notice: what a finished round just earned, one badge to a screen.
//
// The whole screen is a backdrop and a banner, so there is no state in it beyond which achievement is
// being announced - which badge that is, and when the player has seen them all, belongs to
// systems/achievement_notice.h. Nothing here is gated, because a screen with one thing on it has one
// state to be in.

#include <cstdint>

#include <kirpich/game_state.h>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"  // AchievementId

namespace kirpich::render {

// Whether the notice is the screen that is up.
[[nodiscard]] bool achievementNoticeShown(kirpich::GameState state) noexcept;

[[nodiscard]] Layers AchievementNotice(AchievementId id, const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
