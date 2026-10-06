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
    if (next < 0 || next >= static_cast<int>(kControlsRowCount)) {
        return;
    }
    game.controlsScreen.row = static_cast<ControlsRow>(next);
    game.audioCues.square   = SquareSfxId::TINK;
}

// One step of the cursor across the columns, the same way. The restore row is one item, so on it
// both sides are edges.
void moveColumn(GameContext& game, int delta) {
    if (game.controlsScreen.row == ControlsRow::RESTORE_DEFAULTS) {
        return;
    }
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

// Put a change to the bindings into effect and write it out, in that order, with the menu-move cue.
// Input is then ignored until it is let go, because a key still held may now mean something else.
void commit(GameContext& game, const ControlsWiring& wiring) {
    if (wiring.apply) {
        wiring.apply(*wiring.controls);
    }
    if (wiring.save) {
        wiring.save(*wiring.controls);
    }
    game.audioCues.square               = SquareSfxId::TINK;
    game.controlsScreen.awaitingRelease = true;
}

// Bind the captured press to the cell, if it is the cell's kind. Returns false for a press of the other
// kind, which the cell cannot take, and on the restore row, which has no cell.
bool bind(const ControlsScreenState& ui, const retropp::CapturedSource& press, Controls& controls) {
    const std::optional<GbButton> button = buttonOf(ui.row);
    if (!button) {
        return false;
    }
    const retropp::Source& source = press.source;
    switch (ui.column) {
        case ControlsColumn::KEYBOARD:
            return source.kind == retropp::Source::Kind::Key &&
                   assignKey(controls, *button, source.key);
        case ControlsColumn::CONTROLLER:
            return source.kind == retropp::Source::Kind::Pad &&
                   assignPad(controls, *button, source.pad, press.device.family);
    }
    return false;
}

// A or Start: on a button's row, wait for a press for the cell; on the restore row, put every binding
// back to the defaults - after asking. Bindings that already are the defaults have nothing to restore,
// so there the press is an end stop: no question, nothing cued.
void choose(GameContext& game, const ControlsWiring& wiring) {
    if (game.controlsScreen.row != ControlsRow::RESTORE_DEFAULTS) {
        listen(game, wiring);
        return;
    }
    if (wiring.controls == nullptr || *wiring.controls == kDefaultControls) {
        return;
    }
    game.controlsScreen.confirmingRestore = true;
    game.controlsScreen.confirmYes        = false;
    game.screens.cursorVisible            = true;
    game.flow.timer1                      = kScreenBlinkFrames;
    game.audioCues.square                 = SquareSfxId::CHANGE_SCREEN;
}

// Leave the question, back to the bindings with the cursor still on the restore row.
void closeConfirm(GameContext& game) {
    game.controlsScreen.confirmingRestore = false;
    game.controlsScreen.confirmYes        = false;
    game.screens.cursorVisible            = true;
    game.flow.timer1                      = kScreenBlinkFrames;
    game.audioCues.square                 = SquareSfxId::CHANGE_SCREEN;
}

// Move the question's cursor to one answer. A press toward the answer already chosen is an edge.
void pickAnswer(GameContext& game, bool yes) {
    if (game.controlsScreen.confirmYes == yes) {
        return;
    }
    game.controlsScreen.confirmYes = yes;
    game.audioCues.square          = SquareSfxId::TINK;
}

// A or Start on the question: "yes" restores the defaults, put into effect and saved; "no" leaves the
// bindings as they are. Either way the question closes.
void answer(GameContext& game, const ControlsWiring& wiring) {
    if (game.controlsScreen.confirmYes && wiring.controls != nullptr) {
        *wiring.controls = kDefaultControls;
        commit(game, wiring);
    }
    closeConfirm(game);
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

    game.controlsScreen.listening = false;
    commit(game, wiring);
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
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { choose(g, c); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { choose(g, c); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring& s, const ControlsWiring&) {
             returnToSettings(g, s);
         }},
};

// The restore question's own table. B and "no" both leave it with the bindings untouched.
constexpr std::array kConfirmBinds{
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { pickAnswer(g, false); }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { pickAnswer(g, true); }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { answer(g, c); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring& c) { answer(g, c); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const ControlsWiring&) { closeConfirm(g); }},
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
    if (game.controlsScreen.confirmingRestore) {
        dispatch(game, settings, controls, kConfirmBinds);
    } else {
        dispatch(game, settings, controls, kBinds);
    }
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
