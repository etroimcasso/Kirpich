#include "render/achievements/badge_panel.h"

#include <cstdio>
#include <string>

#include "render/achievements/badge.h"
#include "render/achievements/layout.h"
#include "render/glyphs.h"
#include "systems/achievements.h"  // achievementDef

namespace kirpich::render {

namespace {

// What a hidden one says before it has been earned: that it is hidden, and what makes it stop being
// hidden. Nothing about itself, and nothing beyond the rule.
constexpr std::string_view kHiddenTitle       = "hidden";
constexpr std::string_view kHiddenDescription = "revealed when earned";

// When it was earned: the calendar date, and how long the player had been playing by then.
[[nodiscard]] std::string stamp(const AchievementUnlock& record) {
    const AchievementDate date     = unpackAchievementDate(record.datePacked);
    char                  text[32] = {};
    std::snprintf(text, sizeof text, "%u-%u-%u %uh %um", static_cast<unsigned>(date.year),
                  static_cast<unsigned>(date.month), static_cast<unsigned>(date.day),
                  record.playSecondsAtUnlock / 3600u, (record.playSecondsAtUnlock / 60u) % 60u);
    return text;
}

}  // namespace

Sprites BadgePanel(AchievementId id, const AchievementState& earned, const TileAtlas& atlas,
                   std::uint8_t ramp) {
    const AchievementUnlock&       record = earned.unlocked[achievementIndex(id)];
    const systems::AchievementDef& def    = systems::achievementDef(id);
    const bool                     told   = record.unlocked || !def.hidden;

    // A hidden one that has not been earned keeps what it is to itself, but it still says so. Left
    // with only its badge the page reads as though it had failed to draw rather than as a secret.
    const std::string_view title = told ? def.title : kHiddenTitle;
    const std::string_view body  = told ? def.description : kHiddenDescription;

    Sprites out =
        AchievementBadge(id, kAchPanelBadgeX, kAchPanelBadgeY, record.unlocked, atlas, ramp);

    const auto add = [&out](const Sprites& run) {
        out.insert(out.end(), run.begin(), run.end());
    };

    add(Glyphs(title, kAchPanelTextX, kAchPanelTitleY, kAchGlyphPitch, atlas, ramp));

    int line = kAchPanelDescY;
    for (const std::string_view run : wrapText(body, kAchPanelTextCells)) {
        add(Glyphs(run, kAchPanelTextX, line, kAchGlyphPitch, atlas, ramp));
        line += kAchPanelLineStep;
    }

    if (record.unlocked) {
        add(Glyphs(stamp(record), kAchPanelTextX, kAchPanelStampY, kAchGlyphPitch, atlas, ramp));
    }
    return out;
}

}  // namespace kirpich::render
