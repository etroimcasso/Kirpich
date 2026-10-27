#include "systems/palette_settings_screen.h"

#include <array>
#include <optional>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"                                      // SquareSfxId
#include "render/screens/palette_editor/palette_editor.h"  // openPaletteEditor
#include "retropp/input.h"                                 // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/palette_walk.h"  // PaletteChoice, paletteChoiceOf, nextPaletteChoice

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// The selector steps the settings along the palette walk (src/systems/palette_walk.h), from where the
// settings stand on it.
void stepPalette(GameContext& game, const SettingsWiring& wiring, const CustomPaletteWiring& custom,
                 int delta) {
    const std::optional<PaletteChoice> target = nextPaletteChoice(
        paletteChoiceOf(wiring.current(), custom.palettes), custom.palettes, delta);
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
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& c) {
             g.audioCues.square = SquareSfxId::CHANGE_SCREEN;
             openPaletteEditor(g, c.palettes);
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
    return nextPaletteChoice(paletteChoiceOf(settings, palettes), palettes, delta).has_value();
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
