#include "render/achievements/banner.h"

#include <string_view>

#include "render/achievements/badge.h"
#include "render/achievements/notice_layout.h"
#include "render/glyphs.h"
#include "systems/achievements.h"  // achievementDef

namespace kirpich::render {

Sprites AchievementBanner(AchievementId id, int x, int y, const TileAtlas& atlas,
                          std::uint8_t ramp) {
    const systems::AchievementDef& def = systems::achievementDef(id);

    Sprites out = AchievementBadge(id, x, y, /*unlocked=*/true, atlas, ramp);

    const auto add = [&out](const Sprites& run) {
        out.insert(out.end(), run.begin(), run.end());
    };

    // The text clears the badge's own art, however wide this one is.
    const int textX = noticeTextX(badgeExtent(id).width);

    add(Glyphs(def.title, textX, y, kNoticeGlyphPitch, atlas, ramp));

    int line = y + kNoticeTitleGap;
    for (const std::string_view run : wrapText(def.description, noticeTextCells(textX))) {
        add(Glyphs(run, textX, line, kNoticeGlyphPitch, atlas, ramp));
        line += kNoticeLineStep;
    }
    return out;
}

}  // namespace kirpich::render
