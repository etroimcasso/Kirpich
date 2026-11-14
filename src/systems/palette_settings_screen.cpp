#include "systems/palette_settings_screen.h"

#include <array>
#include <optional>
#include <span>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"                                      // SquareSfxId
#include "render/screens/palette_editor/palette_editor.h"      // openPaletteEditor
#include "render/screens/palette_rotation/rotation_screen.h"  // openRotationScreen
#include "retropp/input.h"                                 // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"         // keyRepeatFire
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

// Left and Right are not here: the selector reads them as held, on the key repeat, ahead of this
// table (paletteSettingsScreen).
constexpr std::array kBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) {
             g.audioCues.square = SquareSfxId::CHANGE_SCREEN;
             openRotationScreen(g);
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

// The selector's two directions on the key repeat, Left read ahead of Right: a press steps at once, a
// hold steps again 23 frames later and every 9 after that. Returns whether either was down, which
// makes a held direction the whole frame's input.
bool stepHeldDirection(GameContext& game, const SettingsWiring& wiring,
                       const CustomPaletteWiring& custom) {
    for (const auto& [action, delta] : {std::pair{Action::MenuLeft, -1}, std::pair{Action::MenuRight, +1}}) {
        const bool isPressed = pressed(game, action);
        const bool isHeld    = game.joypad.held.test(retropp::actionId(action));
        if (isPressed || isHeld) {
            if (keyRepeatFire(game.flow.keyRepeatTimer, isPressed, isHeld)) {
                stepPalette(game, wiring, custom, delta);
            }
            return true;
        }
    }
    return false;
}

}  // namespace

void openPaletteSettings(GameContext& game) {
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    // A direction still held from the screen that opened this one waits out the whole repeat before
    // it steps, rather than whatever was left on the timer.
    game.flow.keyRepeatTimer = 0;
    game.flow.gameState      = GameState::PALETTE_SETTINGS;
}

bool paletteCanStep(const Settings& settings, const CustomPalettes* palettes, int delta) noexcept {
    return nextPaletteChoice(paletteChoiceOf(settings, palettes), palettes, delta).has_value();
}

void paletteSettingsScreen(GameContext& game, const SettingsWiring& wiring,
                           const CustomPaletteWiring& palettes) {
    blinkScreenCursor(game);
    if (stepHeldDirection(game, wiring, palettes)) {
        return;
    }
    dispatch(game, wiring, palettes, kBinds);
}

void installPaletteSettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring,
                                  CustomPaletteWiring palettes) {
    dispatcher.setHandler(GameState::PALETTE_SETTINGS,
                          [wiring = std::move(wiring), palettes = std::move(palettes)](
                              GameContext& g) { paletteSettingsScreen(g, wiring, palettes); });
}

}  // namespace kirpich::systems
