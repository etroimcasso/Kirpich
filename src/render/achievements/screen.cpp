#include "render/achievements/screen.h"

#include <utility>

#include "render/achievements/badge.h"  // badgeExtent
#include "render/achievements/badge_grid.h"
#include "render/achievements/badge_panel.h"
#include "render/achievements/layout.h"
#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/glyphs.h"
#include "render/palettes.h"  // kShadeRamps
#include "render/selection_corners.h"
#include "render/sprite_layer.h"
#include "systems/achievements_screen.h"  // the section on the page, and its title

namespace kirpich::render {

bool achievementScreenShown(kirpich::GameState state) noexcept {
    return state == kirpich::GameState::ACHIEVEMENTS;
}

Layers AchievementsScreen(const AchievementScreenState& ui, const AchievementState& earned,
                          bool blinkOn, const TileAtlas& atlas, std::uint8_t ramp) {
    const systems::AchievementSection section = systems::achievementSectionAt(ui.section);

    // The cursor is a bracket drawn around the badge it is on, not a change to the badge. It shows
    // while the grid is up and blinks on the timer every cursor in the game blinks on, and it is
    // sized to the badge under it, whose art may be a square, a bar or a standing figure.
    const bool selecting = !ui.open && blinkOn &&
                           systems::achievementSectionBadgeCount(section) != 0;

    Regions cursor;
    if (selecting) {
        const BadgeExtent art =
            badgeExtent(systems::achievementSectionBadge(section, ui.cursor).id);
        cursor = SelectionCorners("ach-cursor", achSelectX(ui.cursor), achSelectY(ui.cursor),
                                  achSelectSpan(art.width), achSelectSpan(art.height),
                                  rampColours(ramp)[0]);
    }

    return {
        BackgroundLayer("ach-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("ach-content", kAchBadgeZ,
                    {
                        Glyphs(systems::achievementSectionTitle(section), kAchHeadingX,
                               kAchHeadingY, kAchGlyphPitch, atlas, ramp),
                        ui.open ? BadgePanel(ui.openId, earned, atlas, ramp)
                                : BadgeGrid(section, earned, atlas, ramp),
                    },
                    LayerOptions{.regions = std::move(cursor)}),
    };
}

}  // namespace kirpich::render
