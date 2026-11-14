#pragma once

// Which palette the frame is drawn through, when a round's palette follows its level.
//
// With Settings::paletteRotation OFF, every frame draws through paletteIndexOf(settings), the palette
// the player chose. With a preset or CUSTOM, the round's own states draw through the level's palette
// instead: the atlas's rotation set, render::kRotationRamp, holding the mode's palette for the level
// modulo ten - a preset's, or the player's own working rotation for CUSTOM. Every other screen - the
// menus, the settings, the top-score entry - keeps the player's palette.
//
// The one place outside a round the rotation set is written for is the rotation screen's held
// preview: while Select is held there, the set holds the mode's palette at the level the preview
// shows, and the preview draws through it.
//
// The rotation set's colors are state the frame reads, and the host keeps them current with one call
// per simulation tick:
//
//     kirpich::systems::reconcileRotationPalette(
//         settings, rotations.working, game, tiles.rotationShades,
//         [&renderer, &tiles](const kirpich::render::ShadeRamp& shades) {
//             kirpich::render::updateRotationPalette(renderer, tiles, shades);
//         });
//
// and draws through framePaletteIndex(settings, rotations.working, game) in its render callback.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <kirpich/game_state.h>

#include "render/palettes.h"              // ShadeRamp
#include "render/rotation_presets.h"      // RotationPalettes
#include "state/custom_rotations.h"       // RotationProgression, kRotationLevelCount
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

// The mode's ten palettes, level 0 first: a preset's table, `progression.levels` for CUSTOM, and
// nothing for OFF. The pointer refers to the preset table or into `progression`, and is valid as long
// as the one it names.
[[nodiscard]] const render::RotationPalettes* rotationPalettes(
    const Settings& settings, const RotationProgression& progression) noexcept;

// The palette the frame should be drawn through, or nothing. Nothing while the mode is OFF. Otherwise
// the mode's palette (rotationPalettes) at:
//   - game.rotationScreen.previewLevel, while the game is on the rotation screen with its held preview
//     up (game.flow.gameState == PALETTE_ROTATION and game.rotationScreen.previewing);
//   - rotationLevelOf(game.flow.level), while the game is in a round's states (roundPaletteShown);
// and nothing in any other state. The host passes the player's own working rotation as `progression`.
// Reads game.flow.gameState, game.flow.level and game.rotationScreen and nothing else of the context.
[[nodiscard]] std::optional<render::ShadeRamp> rotationPaletteFor(
    const Settings& settings, const RotationProgression& progression,
    const GameContext& game) noexcept;

// The palette index the frame draws through: render::kRotationRamp while rotationPaletteFor yields a
// palette, paletteIndexOf(settings) otherwise.
[[nodiscard]] std::uint8_t framePaletteIndex(const Settings& settings,
                                             const RotationProgression& progression,
                                             const GameContext& game) noexcept;

// Keep the rotation set current. When rotationPaletteFor yields a palette that differs from
// `current` - what the set holds - `write` is called once with it; otherwise nothing happens.
// `current` keeps its colors while no palette is wanted, so returning to a round at the same level
// writes nothing. Call once per simulation tick, after the frame's last beat, so a level change - or
// a step of the held preview's level - is drawn on the tick it happens.
void reconcileRotationPalette(const Settings& settings, const RotationProgression& progression,
                              const GameContext& game, const render::ShadeRamp& current,
                              const std::function<void(const render::ShadeRamp&)>& write);

}  // namespace kirpich::systems
