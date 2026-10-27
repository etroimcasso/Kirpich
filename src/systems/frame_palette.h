#pragma once

// Which palette the frame is drawn through, when a round's palette follows its level.
//
// With Settings::paletteRotation OFF, every frame draws through paletteIndexOf(settings), the palette
// the player chose. With a preset or CUSTOM, the round's own states draw through the level's palette
// instead: the atlas's rotation set, render::kRotationRamp, holding the mode's palette for the level
// modulo ten. Every other screen - the menus, the settings, the top-score entry - keeps the player's
// palette.
//
// The rotation set's colors are state the frame reads, and the host keeps them current with one call
// per simulation tick:
//
//     kirpich::systems::reconcileRotationPalette(
//         settings, rotation, game, tiles.rotationShades,
//         [&renderer, &tiles](const kirpich::render::ShadeRamp& shades) {
//             kirpich::render::updateRotationPalette(renderer, tiles, shades);
//         });
//
// and draws through framePaletteIndex(settings, rotation, game) in its render callback.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <kirpich/game_state.h>

#include "render/palettes.h"              // ShadeRamp
#include "state/rotation_progression.h"   // RotationProgression, kRotationLevelCount
#include "state/settings.h"               // Settings
#include "systems/game_context.h"         // GameContext

namespace kirpich::systems {

// Whether a state is one of the round's own, from its init until the game returns to a menu: the
// round's play (paused included), its game-over curtain and screen, the Type B scoreboard and dance,
// and both launch scenes. The level's palette holds through all of them and through none of the
// screens outside them. The attract demo runs these same states, so it is drawn the same way.
[[nodiscard]] bool roundPaletteShown(GameState state) noexcept;

// Which of the ten palettes a level wears.
[[nodiscard]] constexpr std::size_t rotationLevelOf(std::uint8_t level) noexcept {
    return level % kRotationLevelCount;
}

// The palette the round should be drawn through this frame, or nothing: nothing while the mode is
// OFF or the game is not in a round's states; otherwise the mode's palette at
// rotationLevelOf(game.flow.level) - a preset's, or the player's own for CUSTOM. Reads
// game.flow.gameState and game.flow.level and nothing else of the context.
[[nodiscard]] std::optional<render::ShadeRamp> rotationPaletteFor(
    const Settings& settings, const RotationProgression& progression,
    const GameContext& game) noexcept;

// The palette index the frame draws through: render::kRotationRamp while rotationPaletteFor yields a
// palette, paletteIndexOf(settings) otherwise.
[[nodiscard]] std::uint8_t framePaletteIndex(const Settings& settings,
                                             const RotationProgression& progression,
                                             const GameContext& game) noexcept;

// Keep the rotation set current. When rotationPaletteFor yields a palette that differs from
// `current` - what the set holds now - `write` is called once with it; otherwise nothing happens.
// `current` keeps its colors while no palette is wanted, so returning to a round at the same level
// writes nothing. Call once per simulation tick, after the frame's last beat, so a level change is
// drawn on the tick it happens.
void reconcileRotationPalette(const Settings& settings, const RotationProgression& progression,
                              const GameContext& game, const render::ShadeRamp& current,
                              const std::function<void(const render::ShadeRamp&)>& write);

}  // namespace kirpich::systems
