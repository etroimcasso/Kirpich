#include "systems/controls_screen.h"

#include <array>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "state/controls_screen_state.h"
#include "systems/controls.h"  // assignKey, assignPad
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// One step of the cursor across the rows. A move cues the menu move; an edge moves nothing and says
// nothing.
void moveRow(GameContext& game, int delta) {
    const int next = static_cast<int>(game.controlsScreen.row) + delta;
    if (next < 0 || next >= static_cast<int>(kGbButtonCount)) {
        return;
    }
    game.controlsScreen.row = static_cast<GbButton>(next);
    game.audioCues.square   = SquareSfxId::TINK;
}

// One step of the cursor across the columns, the same way.
void moveColumn(GameContext& game, int delta) {
    const int next = static_cast<int>(game.controlsScreen.column) + delta;
    if (next < 0 || next >= static_cast<int>(kControlsColumnCount)) {
        return;
    }
    game.controlsScreen.column = static_cast<ControlsColumn>(next);
    game.audioCues.square      = SquareSfxId::TINK;
}

// Wait for a press on the cell the cursor is on. The engine does not take a press already down when
// it is asked as its answer, so the A that started the wait is not bound to the cell.
void listen(GameContext& game, const ControlsWiring& wiring) {
    game.controlsScreen.listening = true;
    if (wiring.listen) {
        wiring.listen();
    }
}

// Stop waiting. Whatever ended the wait, input is ignored until it is let go.
void stopListening(GameContext& game) {
    game.controlsScreen.listening       = false;
    game.controlsScreen.awaitingRelease = true;
}

// Bind the captured press to the cell, if it is the cell's kind. Returns false for a press of the other
// kind, which the cell cannot take.
bool bind(const ControlsScreenState& ui, const retropp::CapturedSource& press, Controls& controls) {
    const retropp::Source& source = press.source;
    switch (ui.column) {
        case ControlsColumn::KEYBOARD:
            return source.kind == retropp::Source::Kind::Key &&
                   assignKey(controls, ui.row, source.key);
        case ControlsColumn::CONTROLLER:
            return source.kind == retropp::Source::Kind::Pad &&
                   assignPad(controls, ui.row, source.pad, press.device.family);
    }
    return false;
}

// One frame of waiting: read what the platform caught, if anything.
void readCapture(GameContext& game, const ControlsWiring& wiring) {
    if (!wiring.captured) {
        return;
    }
    const std::optional<retropp::CapturedSource> press = wiring.captured();
    if (!press) {
        return;
    }

    const bool cancel = press->source.kind == retropp::Source::Kind::Key &&
                        press->source.key == kCancelKey;
    if (cancel || wiring.controls == nullptr) {
        stopListening(game);
        return;
    }

    if (!bind(game.controlsScreen, *press, *wiring.controls)) {
        // A press the cell cannot take. The platform keeps its answer until it is asked again, so ask
        // again, or the screen would read the same press every frame.
        if (wiring.listen) {
            wiring.listen();
        }
        return;
    }

    if (wiring.apply) {
        wiring.apply(*wiring.controls);
    }
    if (wiring.save) {
        wiring.save(*wiring.controls);
    }
    game.audioCues.square = SquareSfxId::TINK;
    stopListening(game);
}

using Effect = void (*)(GameContext&, const SettingsWiring&, const ControlsWiring&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { moveRow(g, -1); }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { moveRow(g, +1); }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { moveColumn(g, -1); }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { moveColumn(g, +1); }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { listen(g, c); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { listen(g, c); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring& s, const ControlsWiring&) {
             returnToSettings(g, s);
         }},
};

void dispatch(GameContext& game, const SettingsWiring& settings, const ControlsWiring& controls,
              std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, settings, controls);
            return;
        }
    }
}

}  // namespace

void openControlsSettings(GameContext& game) {
    game.controlsScreen.reset();
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::CONTROLS_SETTINGS;
}

void controlsScreen(GameContext& game, const SettingsWiring& settings,
                    const ControlsWiring& controls) {
    if (game.controlsScreen.listening) {
        readCapture(game, controls);
        return;
    }
    if (game.controlsScreen.awaitingRelease) {
        if (game.joypad.held == retropp::ActionSet{}) {
            game.controlsScreen.awaitingRelease = false;
        }
        return;
    }
    blinkScreenCursor(game);
    dispatch(game, settings, controls, kBinds);
}

void installControlsScreen(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           ControlsWiring controls) {
    dispatcher.setHandler(GameState::CONTROLS_SETTINGS,
                          [settings = std::move(settings),
                           controls = std::move(controls)](GameContext& g) {
                              controlsScreen(g, settings, controls);
                          });
}

}  // namespace kirpich::systems
