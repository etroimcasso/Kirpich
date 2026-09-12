#include "render/achievements/badge_grid.h"

#include "render/achievements/badge.h"
#include "render/achievements/layout.h"    // achGridX / achGridY
#include "systems/achievements_screen.h"   // the section's badges

namespace kirpich::render {

Sprites BadgeGrid(systems::AchievementSection section, const AchievementState& earned,
                  const TileAtlas& atlas, std::uint8_t ramp) {
    Sprites           out;
    const std::size_t count = systems::achievementSectionBadgeCount(section);

    for (std::size_t slot = 0; slot < count; ++slot) {
        const systems::AchievementDef& def = systems::achievementSectionBadge(section, slot);
        const Sprites badge = AchievementBadge(def.id, achGridX(slot), achGridY(slot),
                                               systems::achievementUnlocked(earned, def.id), atlas,
                                               ramp);
        out.insert(out.end(), badge.begin(), badge.end());
    }
    return out;
}

}  // namespace kirpich::render
