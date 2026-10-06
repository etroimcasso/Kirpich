#include "systems/palette_settings_screen.h"

#include <array>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "render/palettes.h"  // clampShadeRamp
#include "retropp/input.h"    // actionId
#include "state/settings.h"
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

void stepPalette(GameContext& game, const SettingsWiring& wiring, int delta) {
    Settings next  = wiring.current();
    next.shadeRamp = render::clampShadeRamp(static_cast<int>(next.shadeRamp) + delta);
    changeSettings(game, wiring, next);
}

using Effect = void (*)(GameContext&, const SettingsWiring&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kBinds{
    Bind{Action::MenuLeft, [](GameContext& g, const SettingsWiring& w) { stepPalette(g, w, -1); }},
    Bind{Action::MenuRight, [](GameContext& g, const SettingsWiring& w) { stepPalette(g, w, +1); }},
    Bind{Action::Back, [](GameContext& g, const SettingsWiring& w) { returnToSettings(g, w); }},
};

void dispatch(GameContext& game, const SettingsWiring& wiring, std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, wiring);
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

void paletteSettingsScreen(GameContext& game, const SettingsWiring& wiring) {
    blinkScreenCursor(game);
    dispatch(game, wiring, kBinds);
}

void installPaletteSettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring) {
    dispatcher.setHandler(GameState::PALETTE_SETTINGS,
                          [wiring = std::move(wiring)](GameContext& g) {
                              paletteSettingsScreen(g, wiring);
                          });
}

}  // namespace kirpich::systems
