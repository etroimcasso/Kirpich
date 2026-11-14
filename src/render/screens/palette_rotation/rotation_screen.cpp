#include "render/screens/palette_rotation/rotation_screen.h"

#include <array>
#include <optional>
#include <span>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/palette_settings_screen.h"  // openPaletteSettings
#include "systems/rotation_walk.h"            // RotationChoice, rotationChoiceOf, nextRotationChoice

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

bool held(const GameContext& game, Action action) {
    return game.joypad.held.test(retropp::actionId(action));
}

void cue(GameContext& game, SquareSfxId sound) {
    game.audioCues.square = sound;
}

// ── The menu ──────────────────────────────────────────────────────────────────────────────────────

void leave(GameContext& game) {
    cue(game, SquareSfxId::CHANGE_SCREEN);
    openPaletteSettings(game);
}

// Up on the first row is an end stop; Down past the last row leaves.
void moveRow(GameContext& game, int delta) {
    const int next = static_cast<int>(game.rotationScreen.row) + delta;
    if (next < 0) {
        return;
    }
    if (next >= static_cast<int>(kRotationScreenRowCount)) {
        leave(game);
        return;
    }
    game.rotationScreen.row = static_cast<RotationScreenRow>(next);
    cue(game, SquareSfxId::TINK);
}

// The selector steps the settings along the rotation walk (src/systems/rotation_walk.h), from where
// the settings stand on it.
void stepRotation(GameContext& game, const SettingsWiring& wiring, const RotationWiring& custom,
                  int delta) {
    const std::optional<RotationChoice> target = nextRotationChoice(
        rotationChoiceOf(wiring.current(), custom.rotations), custom.rotations, delta);
    if (!target) {
        return;  // an end stop moves nothing and says nothing
    }

    Settings next = wiring.current();
    if (target->mode == PaletteRotation::CUSTOM) {
        // Only reachable with rotations behind the wiring: a slot is the walk's own answer.
        CustomRotations& rotations = *custom.rotations;
        rotations.working          = rotations.slots[target->slot - 1].progression;
        if (custom.save) {
            custom.save(rotations);
        }
    }
    next.paletteRotation = target->mode;

    // A move between two slots changes no setting, so the cue is the step's own rather than
    // changeSettings', which cues only what it stores.
    cue(game, SquareSfxId::TINK);
    changeSettings(game, wiring, next);
}

// Left and Right: the rotation on the selector, nothing on the editor item.
void sideways(GameContext& game, const SettingsWiring& wiring, const RotationWiring& custom,
              int delta) {
    if (game.rotationScreen.row == RotationScreenRow::SELECTOR) {
        stepRotation(game, wiring, custom, delta);
    }
}

// ── The held preview ──────────────────────────────────────────────────────────────────────────────

void stepLevel(GameContext& game, int delta) {
    const int next = static_cast<int>(game.rotationScreen.previewLevel) + delta;
    if (next < 0 || next >= static_cast<int>(kRotationLevelCount)) {
        return;
    }
    game.rotationScreen.previewLevel = static_cast<std::uint8_t>(next);
    cue(game, SquareSfxId::TINK);
}

// ── The tables ────────────────────────────────────────────────────────────────────────────────────

using Effect = void (*)(GameContext&, const SettingsWiring&, const RotationWiring&);

struct Bind {
    Action action;
    Effect effect;
};

// Confirm and Start are not here: neither row has anything to choose. The editor item does nothing.
constexpr std::array kWalkBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { moveRow(g, -1); }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { moveRow(g, +1); }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring& s, const RotationWiring& r) {
             sideways(g, s, r, -1);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring& s, const RotationWiring& r) {
             sideways(g, s, r, +1);
         }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { leave(g); }},
};

// Up and Right are the next level, Down and Left the previous one, so the four arrows the level item
// shows are the four presses that move it.
constexpr std::array kPreviewBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { stepLevel(g, +1); }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { stepLevel(g, +1); }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { stepLevel(g, -1); }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&) { stepLevel(g, -1); }},
};

void dispatch(GameContext& game, const SettingsWiring& settings, const RotationWiring& rotations,
              std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, settings, rotations);
            return;
        }
    }
}

}  // namespace

void openRotationScreen(GameContext& game) {
    game.rotationScreen.reset();
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::PALETTE_ROTATION;
}

bool rotationCanStep(const Settings& settings, const CustomRotations* rotations,
                     int delta) noexcept {
    return nextRotationChoice(rotationChoiceOf(settings, rotations), rotations, delta).has_value();
}

void rotationScreen(GameContext& game, const SettingsWiring& settings,
                    const RotationWiring& rotations) {
    blinkScreenCursor(game);
    game.rotationScreen.previewing = held(game, Action::Select);
    if (game.rotationScreen.previewing) {
        dispatch(game, settings, rotations, kPreviewBinds);
    } else {
        dispatch(game, settings, rotations, kWalkBinds);
    }
}

void installRotationScreen(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           RotationWiring rotations) {
    dispatcher.setHandler(GameState::PALETTE_ROTATION,
                          [settings = std::move(settings), rotations = std::move(rotations)](
                              GameContext& g) { rotationScreen(g, settings, rotations); });
}

}  // namespace kirpich::systems
