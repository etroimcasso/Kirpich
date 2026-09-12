#pragma once

// The achievements screen: its layers.
//
// A backdrop of tiles, and over it the section's heading with either the badge grid or an open
// badge's panel. Which of the two is decided where the layer is declared, from the screen's state, so
// nothing is ever added by one path and cleared by another - a gate going false simply stops the
// component being asked for, and the engine's own reconciliation takes it off the screen.

#include <cstdint>

#include <kirpich/game_state.h>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/achievement_state.h"
#include "state/achievements_screen_state.h"

namespace kirpich::render {

// Whether the achievements screen is the one on display.
[[nodiscard]] bool achievementScreenShown(kirpich::GameState state) noexcept;

[[nodiscard]] Layers AchievementsScreen(const AchievementScreenState& ui,
                                        const AchievementState& earned, bool blinkOn,
                                        const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
