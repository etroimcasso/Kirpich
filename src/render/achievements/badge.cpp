#include "render/achievements/badge.h"

#include <algorithm>
#include <string>

#include <kirpich/sprite_id.h>

#include "data/sprites.h"                // getSprite
#include "render/achievements/layout.h"  // kAchBadgeZ
#include "systems/achievements.h"        // achievementDef, for a badge's section

namespace kirpich::render {

namespace {

// One part of a composed sprite is one tile.
constexpr int kBadgeTilePx = 8;

// The art a badge wears: its section's emblem, from art the game already has. Data - changing it
// changes no logic and no saved byte.
//
// Which sheet matters as much as which sprite. A tile index means different art under each regime, so
// the rocket - which the game only ever draws while the launch scenes have their own sheet loaded -
// has to name that sheet here, or its indices resolve to whatever the gameplay art keeps at them.
struct BadgeArt {
    SpriteId  id    = SpriteId::T_0;
    TileSheet sheet = TileSheet::GAMEPLAY;
};

[[nodiscard]] BadgeArt artFor(AchievementId id) noexcept {
    switch (systems::achievementDef(id).section) {
        case systems::AchievementSection::ASCENT:        return {SpriteId::T_0};
        case systems::AchievementSection::ENDURANCE:     return {SpriteId::I_0};
        case systems::AchievementSection::THE_TETRIS:    return {SpriteId::O_0};
        case systems::AchievementSection::DIG_OUT:       return {SpriteId::JUMPING_COSSACK_1};
        case systems::AchievementSection::RISING_FLOOR:  return {SpriteId::S_0};
        case systems::AchievementSection::REDLINE:       return {SpriteId::Z_0};
        case systems::AchievementSection::HAVE_A_HEART:  return {SpriteId::L_0};
        case systems::AchievementSection::LIFTOFF:
            return {SpriteId::ROCKET_S, TileSheet::MULTIPLAYER_BURAN};
        case systems::AchievementSection::THE_LONG_GAME: return {SpriteId::J_0};
    }
    return {};
}

// A composed sprite's own smallest offsets, so every badge hangs from the same corner however its art
// is laid out.
struct PartOrigin {
    std::uint8_t y = 0;
    std::uint8_t x = 0;
};

[[nodiscard]] PartOrigin smallestOffsets(const kirpich::Sprite& art) noexcept {
    if (art.parts.empty()) return {};
    PartOrigin least{.y = 255, .x = 255};
    for (const kirpich::SpritePart& part : art.parts) {
        least.y = std::min(least.y, part.y);
        least.x = std::min(least.x, part.x);
    }
    return least;
}

}  // namespace

Sprites AchievementBadge(AchievementId id, int x, int y, bool unlocked, const TileAtlas& atlas,
                         std::uint8_t ramp) {
    const BadgeArt         emblem = artFor(id);
    const kirpich::Sprite& art    = getSprite(emblem.id);
    const PartOrigin       least  = smallestOffsets(art);

    Sprites     out;
    std::size_t part = 0;
    out.reserve(art.parts.size());
    for (const kirpich::SpritePart& p : art.parts) {
        const ResolvedTile tile =
            unlocked ? resolveSpriteTile(p.tile, emblem.sheet, /*palette1=*/false, atlas, ramp)
                     : resolveDimSpriteTile(p.tile, emblem.sheet, atlas, ramp);
        // A badge is named for which achievement it is, which part of its art this is, AND where it
        // is drawn. The same achievement appears in the grid and again, larger, on its own panel;
        // those are two different objects, and keying them alike makes the engine reconcile them as
        // one and slide the grid badge into the panel when a badge is opened.
        out.push_back(retropp::Sprite{
            .key = retropp::ObjectKey{"ach-badge-" + std::to_string(achievementIndex(id)) + "-" +
                                      std::to_string(part) + "-" + std::to_string(x) + "-" +
                                      std::to_string(y)},
            .x       = x + static_cast<int>(p.x) - static_cast<int>(least.x),
            .y       = y + static_cast<int>(p.y) - static_cast<int>(least.y),
            .z       = kAchBadgeZ,
            .atlas   = tile.atlas,
            .tile    = tile.cell,
            .palette = tile.palette,
            .flipX   = p.xflip,
        });
        ++part;
    }
    return out;
}

BadgeExtent badgeExtent(AchievementId id) {
    const kirpich::Sprite& art = getSprite(artFor(id).id);
    if (art.parts.empty()) return {};

    const PartOrigin least = smallestOffsets(art);
    std::uint8_t     right = 0;
    std::uint8_t     below = 0;
    for (const kirpich::SpritePart& p : art.parts) {
        right = std::max(right, static_cast<std::uint8_t>(p.x - least.x));
        below = std::max(below, static_cast<std::uint8_t>(p.y - least.y));
    }

    // Each part is one tile, so the art runs a tile past its furthest part's own corner.
    return {.width  = static_cast<int>(right) + kBadgeTilePx,
            .height = static_cast<int>(below) + kBadgeTilePx};
}

}  // namespace kirpich::render
