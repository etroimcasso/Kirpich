#include "render/achievements/notice.h"

#include "render/achievements/banner.h"
#include "render/achievements/layout.h"  // kAchBadgeZ
#include "render/achievements/notice_layout.h"
#include "render/backdrop.h"
#include "render/background_layer.h"
#include "render/sprite_layer.h"

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
