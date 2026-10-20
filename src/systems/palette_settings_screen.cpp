#include "systems/palette_settings_screen.h"

#include <array>
#include <optional>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Where one step lands: a built-in ramp, or a present custom slot (numbered from one).
struct Stop {
    bool         custom = false;
    std::uint8_t value  = 0;
};

// The selector's walk is one sequence: the built-in ramps in order, then the present slots in order.
// The current position is the saved slot while the custom palette is in use - or nowhere on the walk,
// when the working palette is saved in no slot - and the ramp otherwise. From off the walk, Left goes
// to the last ramp and Right to the first present slot, which is where the slots begin.
std::optional<Stop> stepTarget(const Settings& settings, const CustomPalettes* palettes,
                               int delta) noexcept {
    constexpr std::uint8_t kLastRamp = static_cast<std::uint8_t>(render::kShadeRampCount - 1);

    if (!settings.customPalette) {
        const std::uint8_t ramp = render::clampShadeRamp(settings.shadeRamp);
        if (delta < 0) {
            if (ramp == 0) return std::nullopt;
            return Stop{.custom = false, .value = static_cast<std::uint8_t>(ramp - 1)};
        }
        if (ramp < kLastRamp) {
            return Stop{.custom = false, .value = static_cast<std::uint8_t>(ramp + 1)};
        }
        if (palettes != nullptr) {
            if (const auto first = nextPresentSlot(*palettes, 0)) {
                return Stop{.custom = true, .value = *first};
            }
        }
        return std::nullopt;
    }

    const std::optional<std::uint8_t> saved =
        palettes != nullptr ? savedSlotOf(*palettes) : std::nullopt;

    if (delta > 0) {
        if (palettes == nullptr) return std::nullopt;
        if (const auto next = nextPresentSlot(*palettes, saved.value_or(0))) {
            return Stop{.custom = true, .value = *next};
        }
        return std::nullopt;
    }

    if (palettes != nullptr && saved) {
        if (const auto previous = previousPresentSlot(*palettes, *saved)) {
            return Stop{.custom = true, .value = *previous};
        }
    }
    return Stop{.custom = false, .value = kLastRamp};
}

void stepPalette(GameContext& game, const SettingsWiring& wiring, const CustomPaletteWiring& custom,
                 int delta) {
    const std::optional<Stop> target = stepTarget(wiring.current(), custom.palettes, delta);
    if (!target) {
        return;  // an end stop moves nothing and says nothing
    }

    Settings next = wiring.current();
    if (target->custom) {
        // Only reachable with palettes behind the wiring: a slot is the walk's own answer.
        CustomPalettes& palettes = *custom.palettes;
        palettes.working         = palettes.slots[target->value - 1].shades;
        if (custom.apply) {
            custom.apply(palettes.working);
        }
        if (custom.save) {
            custom.save(palettes);
        }
        next.customPalette = true;
    } else {
        next.shadeRamp     = target->value;
        next.customPalette = false;
    }

    // A move between two slots changes no setting, so the cue is the step's own rather than
    // changeSettings', which cues only what it stores.
    game.audioCues.square = SquareSfxId::TINK;
    changeSettings(game, wiring, next);
}

using Effect = void (*)(GameContext&, const SettingsWiring&, const CustomPaletteWiring&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kBinds{
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring& w, const CustomPaletteWiring& c) {
             stepPalette(g, w, c, -1);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring& w, const CustomPaletteWiring& c) {
             stepPalette(g, w, c, +1);
         }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring& w, const CustomPaletteWiring&) {
             returnToSettings(g, w);
         }},
};

void dispatch(GameContext& game, const SettingsWiring& wiring, const CustomPaletteWiring& custom,
              std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, wiring, custom);
            return;
        }
    }
}

}  // namespace

void openPaletteSettings(GameContext& game) {
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::PALETTE_SETTINGS;
}

bool paletteCanStep(const Settings& settings, const CustomPalettes* palettes, int delta) noexcept {
    return stepTarget(settings, palettes, delta).has_value();
}

void paletteSettingsScreen(GameContext& game, const SettingsWiring& wiring,
                           const CustomPaletteWiring& palettes) {
    blinkScreenCursor(game);
    dispatch(game, wiring, palettes, kBinds);
}

void installPaletteSettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring,
                                  CustomPaletteWiring palettes) {
    dispatcher.setHandler(GameState::PALETTE_SETTINGS,
                          [wiring = std::move(wiring), palettes = std::move(palettes)](
                              GameContext& g) { paletteSettingsScreen(g, wiring, palettes); });
}

}  // namespace kirpich::systems
