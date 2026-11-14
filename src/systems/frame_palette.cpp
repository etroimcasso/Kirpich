#include "systems/frame_palette.h"

#include <algorithm>
#include <array>

#include "render/rotation_presets.h"  // presetRotation

namespace kirpich::systems {

namespace {

// The round's own states, by name. A list rather than a range, because the round's states are
// scattered through the dispatch table between the menus' and the two-player screens'.
constexpr std::array kRoundStates{
    GameState::NORMAL_GAMEPLAY,         GameState::INIT_GAME_OVER,
    GameState::BURAN_LIFTOFF,           GameState::BURAN_RISING,
    GameState::GAME_OVER_SCREEN,        GameState::TYPE_B_VICTORY_JINGLE,
    GameState::INIT_GAME,               GameState::INIT_TYPE_B_SCOREBOARD,
    GameState::STATE_0C_UNKNOWN,        GameState::GAME_OVER_CURTAIN,
    GameState::INIT_TYPE_B_BONUS,       GameState::DANCERS,
    GameState::INIT_BURAN,              GameState::PREPARE_BURAN_LAUNCH,
    GameState::BURAN_IGNITION,          GameState::BURAN_IGNITION_2,
    GameState::PRINT_CONGRATULATIONS,   GameState::CONGRATULATIONS,
    GameState::INIT_ROCKET_LAUNCH,      GameState::ROCKET,
    GameState::ROCKET_IGNITION,         GameState::ROCKET_LIFTOFF,
    GameState::ROCKET_MAIN_ENGINE_FIRE, GameState::END_OF_BONUS_SCENE,
    GameState::GAME_OVER_TO_BONUS,
};

}  // namespace

bool roundPaletteShown(GameState state) noexcept {
    return std::find(kRoundStates.begin(), kRoundStates.end(), state) != kRoundStates.end();
}

const render::RotationPalettes* rotationPalettes(const Settings&            settings,
                                                const RotationProgression& progression) noexcept {
    switch (settings.paletteRotation) {
        case PaletteRotation::OFF:
            return nullptr;
        case PaletteRotation::CUSTOM:
            return &progression.levels;
        case PaletteRotation::HOTTER:
        case PaletteRotation::COLDER:
        case PaletteRotation::RAINBOW:
            break;
    }
    return render::presetRotation(settings.paletteRotation);
}

std::optional<render::ShadeRamp> rotationPaletteFor(const Settings&            settings,
                                                    const RotationProgression& progression,
                                                    const GameContext&         game) noexcept {
    const render::RotationPalettes* palettes = rotationPalettes(settings, progression);
    if (palettes == nullptr) return std::nullopt;

    if (game.flow.gameState == GameState::PALETTE_ROTATION && game.rotationScreen.previewing) {
        return (*palettes)[game.rotationScreen.previewLevel % kRotationLevelCount];
    }
    if (roundPaletteShown(game.flow.gameState)) {
        return (*palettes)[rotationLevelOf(game.flow.level)];
    }
    return std::nullopt;
}

std::uint8_t framePaletteIndex(const Settings& settings, const RotationProgression& progression,
                               const GameContext& game) noexcept {
    return rotationPaletteFor(settings, progression, game) ? render::kRotationRamp
                                                           : paletteIndexOf(settings);
}

void reconcileRotationPalette(const Settings& settings, const RotationProgression& progression,
                              const GameContext& game, const render::ShadeRamp& current,
                              const std::function<void(const render::ShadeRamp&)>& write) {
    const std::optional<render::ShadeRamp> wanted = rotationPaletteFor(settings, progression, game);
    if (wanted && *wanted != current && write) {
        write(*wanted);
    }
}

}  // namespace kirpich::systems
