#include "render/screens/achievements/notice.h"

#include "render/components/backdrop.h"
#include "render/components/background_layer.h"
#include "render/components/sprite_layer.h"
#include "render/screens/achievements/banner.h"
#include "render/screens/achievements/layout.h"  // kAchBadgeZ
#include "render/screens/achievements/notice_layout.h"

namespace kirpich::render {

bool achievementNoticeShown(kirpich::GameState state) noexcept {
    return state == kirpich::GameState::ACHIEVEMENT_NOTICE;
}

Layers AchievementNotice(AchievementId id, const TileAtlas& atlas, std::uint8_t ramp) {
    return {
        BackgroundLayer("ach-notice-backdrop", 0, Backdrop(atlas, ramp)),
        SpriteLayer("ach-notice", kAchBadgeZ,
                    {
                        AchievementBanner(id, kNoticeBadgeX, kNoticeBadgeY, atlas, ramp),
                    }),
    };
}

}  // namespace kirpich::render
