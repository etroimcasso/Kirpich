#pragma once

// Sprite scene lists: the sprite objects each scripted scene places on screen.
//
// A scene - a victory or defeat screen, the ending dance, a launch sequence, or a menu selection -
// is drawn from a short list of sprite objects. Each object gives a screen position, the sprite to
// draw there (a SpriteId, whose composed layout lives in src/data/sprites.h), and two OAM attribute
// toggles. This surface holds those lists; placing them into the object buffer and drawing them is
// the renderer's job, not this file's.
//
// Two kinds of list appear. Most are ordinary lists of objects, returned as a span. Two - the
// active and preview falling-piece templates - are single objects whose sprite is a placeholder the
// piece logic overwrites each frame with the current piece's rotation; they are returned by
// reference.
//
// The objects are generated from the disassembly by tools/asm_parser/parse_scene_sprites.py; change
// a value there and regenerate rather than editing scene_sprites_data.inc. The object byte layout
// and the per-scene consumer sites are specified in docs/contracts/sprite-scenes.md.

#include <array>
#include <cstdint>
#include <span>

#include <kirpich/sprite_id.h>

namespace kirpich {

// One placed sprite object. `y` and `x` are the OAM base coordinates the renderer draws at; `sprite`
// selects which composed sprite to draw; `hidden` starts the object invisible (the renderer keeps it
// off-screen until game logic reveals it); `behindBg` sets the OAM background-priority bit; `xflip`
// mirrors the object horizontally.
struct SceneSprite {
    bool         hidden;    // starts invisible until game logic reveals it
    std::uint8_t y;         // OAM Y base coordinate
    std::uint8_t x;         // OAM X base coordinate
    SpriteId     sprite;    // which composed sprite to draw (see src/data/sprites.h)
    bool         behindBg;  // OAM background-over-object priority
    bool         xflip;     // OAM horizontal flip

    friend constexpr bool operator==(const SceneSprite&, const SceneSprite&) = default;
};

// kConfigScreenSprites, kMarioVictorySprites, ..., kActivePieceSprite, kPreviewPieceSprite:
// generated at namespace scope, one array per scene list plus the two single-object templates.
#include "generated/scene_sprites_data.inc"

// The two A-Type/B-Type markers on the game-type config screen.
[[nodiscard]] constexpr std::span<const SceneSprite> configScreenSprites() noexcept {
    return kConfigScreenSprites;
}
// The digit marker on the Type-A difficulty screen.
[[nodiscard]] constexpr std::span<const SceneSprite> typeADifficultySprites() noexcept {
    return kTypeADifficultySprites;
}
// The two digit markers on the Type-B difficulty screen.
[[nodiscard]] constexpr std::span<const SceneSprite> typeBDifficultySprites() noexcept {
    return kTypeBDifficultySprites;
}
// The two digit markers on the two-player start-height screen.
[[nodiscard]] constexpr std::span<const SceneSprite> twoPlayerHeightSprites() noexcept {
    return kTwoPlayerHeightSprites;
}
// The Mario / Luigi two-player victory characters.
[[nodiscard]] constexpr std::span<const SceneSprite> marioVictorySprites() noexcept {
    return kMarioVictorySprites;
}
[[nodiscard]] constexpr std::span<const SceneSprite> luigiVictorySprites() noexcept {
    return kLuigiVictorySprites;
}
// The Mario / Luigi two-player defeat characters.
[[nodiscard]] constexpr std::span<const SceneSprite> marioDefeatSprites() noexcept {
    return kMarioDefeatSprites;
}
[[nodiscard]] constexpr std::span<const SceneSprite> luigiDefeatSprites() noexcept {
    return kLuigiDefeatSprites;
}
// The ten musicians and dancers of the ending dance. All start hidden.
[[nodiscard]] constexpr std::span<const SceneSprite> dancerSprites() noexcept {
    return kDancerSprites;
}
// The Buran shuttle and its two smoke plumes.
[[nodiscard]] constexpr std::span<const SceneSprite> buranLaunchSprites() noexcept {
    return kBuranLaunchSprites;
}
// The rocket and its two smoke plumes.
[[nodiscard]] constexpr std::span<const SceneSprite> rocketLaunchSprites() noexcept {
    return kRocketLaunchSprites;
}

// The falling-piece object templates. Their `sprite` is a placeholder (L_0); the piece logic sets it
// to the current piece's rotation before drawing.
[[nodiscard]] constexpr const SceneSprite& activePieceSprite() noexcept {
    return kActivePieceSprite;
}
[[nodiscard]] constexpr const SceneSprite& previewPieceSprite() noexcept {
    return kPreviewPieceSprite;
}

}  // namespace kirpich
