#include "systems/display_settings_screen.h"

#include <array>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "state/display_settings_state.h"
#include "state/settings.h"
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// One step of the cursor. A move cues the menu move; an end stop moves nothing and says nothing.
void moveRow(GameContext& game, int delta) {
    const int next = static_cast<int>(game.displaySettings.row) + delta;
    if (next < 0 || next >= static_cast<int>(kDisplaySettingsRowCount)) {
        return;
    }
    game.displaySettings.row = static_cast<DisplaySettingsRow>(next);
    game.audioCues.square    = SquareSfxId::TINK;
}

// Change the value on the row the cursor is on. Right turns fullscreen on and steps the size up;
// left does the opposite.
void changeValue(GameContext& game, const SettingsWiring& wiring, int delta) {
    Settings next = wiring.current();
    switch (game.displaySettings.row) {
        case DisplaySettingsRow::FULLSCREEN:
            next.fullscreen = delta > 0;
            break;
        case DisplaySettingsRow::WINDOW_SCALE:
            next.windowScale = clampWindowScale(static_cast<int>(next.windowScale) + delta);
            break;
    }
    changeSettings(game, wiring, next);
}

using Effect = void (*)(GameContext&, const SettingsWiring&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kBinds{
    Bind{Action::MenuUp, [](GameContext& g, const SettingsWiring&) { moveRow(g, -1); }},
    Bind{Action::MenuDown, [](GameContext& g, const SettingsWiring&) { moveRow(g, +1); }},
    Bind{Action::MenuLeft, [](GameContext& g, const SettingsWiring& w) { changeValue(g, w, -1); }},
    Bind{Action::MenuRight, [](GameContext& g, const SettingsWiring& w) { changeValue(g, w, +1); }},
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

void openDisplaySettings(GameContext& game) {
    game.displaySettings.reset();
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::DISPLAY_SETTINGS;
}

void displaySettingsScreen(GameContext& game, const SettingsWiring& wiring) {
    blinkScreenCursor(game);
    dispatch(game, wiring, kBinds);
}

void installDisplaySettingsScreen(GameStateDispatcher& dispatcher, SettingsWiring wiring) {
    dispatcher.setHandler(GameState::DISPLAY_SETTINGS,
                          [wiring = std::move(wiring)](GameContext& g) {
                              displaySettingsScreen(g, wiring);
                          });
}

}  // namespace kirpich::systems
