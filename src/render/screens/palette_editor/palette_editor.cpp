#include "render/screens/palette_editor/palette_editor.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"         // SquareSfxId
#include "render/palettes.h"  // freshCustomPalette
#include "retropp/input.h"    // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"    // keyRepeatFire

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

// The slot the preset names.
CustomPaletteSlot& presetSlot(CustomPalettes& palettes, std::uint8_t preset) {
    return palettes.slots[preset - 1];
}

// Put the working palette's new colors into effect and write them out, in that order.
void commitWorking(const CustomPaletteWiring& wiring) {
    if (wiring.apply) {
        wiring.apply(wiring.palettes->working);
    }
    if (wiring.save) {
        wiring.save(*wiring.palettes);
    }
}

void writeDocument(const CustomPaletteWiring& wiring) {
    if (wiring.save) {
        wiring.save(*wiring.palettes);
    }
}

// ── Leaving ───────────────────────────────────────────────────────────────────────────────────────

void leave(GameContext& game) {
    cue(game, SquareSfxId::CHANGE_SCREEN);
    openPaletteSettings(game);
}

// ── The walk ──────────────────────────────────────────────────────────────────────────────────────

// Up past the first row leaves; Down at the last row is an end stop.
void moveRow(GameContext& game, int delta) {
    const int next = static_cast<int>(game.paletteEditor.row) + delta;
    if (next < 0) {
        leave(game);
        return;
    }
    if (next >= static_cast<int>(kPaletteEditorRowCount)) {
        return;
    }
    game.paletteEditor.row = static_cast<PaletteEditorRow>(next);
    cue(game, SquareSfxId::TINK);
}

void stepPreset(GameContext& game, const CustomPaletteWiring& wiring, int delta) {
    PaletteEditorState& ui = game.paletteEditor;
    if (!presetCanStep(ui.preset, wiring.palettes, delta)) {
        return;
    }
    ui.preset = static_cast<std::uint8_t>(ui.preset + delta);
    cue(game, SquareSfxId::TINK);
}

void moveChannel(GameContext& game, int delta) {
    const int next = static_cast<int>(game.paletteEditor.channel) + delta;
    if (next < 0 || next >= static_cast<int>(kColorChannelCount)) {
        return;
    }
    game.paletteEditor.channel = static_cast<ColorChannel>(next);
    cue(game, SquareSfxId::TINK);
}

// Left and Right: the preset on its row, the channel on a shade row, nothing on an action row.
void sideways(GameContext& game, const CustomPaletteWiring& wiring, int delta) {
    const PaletteEditorRow row = game.paletteEditor.row;
    if (row == PaletteEditorRow::PRESET) {
        stepPreset(game, wiring, delta);
    } else if (shadeOf(row)) {
        moveChannel(game, delta);
    }
}

void openQuestion(GameContext& game, PaletteEditorQuestion question) {
    game.paletteEditor.question  = question;
    game.paletteEditor.answerYes = false;
    game.screens.cursorVisible   = true;
    game.flow.timer1             = kScreenBlinkFrames;
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

// The three writes the actions make, after any question is answered yes.
void loadPreset(GameContext& game, const CustomPaletteWiring& wiring) {
    CustomPalettes& palettes = *wiring.palettes;
    palettes.working         = presetSlot(palettes, game.paletteEditor.preset).shades;
    commitWorking(wiring);
    cue(game, SquareSfxId::TINK);
}

void savePreset(GameContext& game, const CustomPaletteWiring& wiring) {
    CustomPalettes& palettes = *wiring.palettes;
    presetSlot(palettes, game.paletteEditor.preset) =
        CustomPaletteSlot{.present = true, .shades = palettes.working};
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);
}

// Clearing the slot that holds the palette in use - with no other slot holding it - puts the game back
// on the built-in ramp underneath, the one stepping off the slots on the Palette screen returns to.
// The working palette is left as it is.
void clearPreset(GameContext& game, const SettingsWiring& settings,
                 const CustomPaletteWiring& wiring) {
    CustomPalettes&    palettes = *wiring.palettes;
    CustomPaletteSlot& slot     = presetSlot(palettes, game.paletteEditor.preset);
    const bool         heldInUse = slot.shades == palettes.working;

    slot = CustomPaletteSlot{};
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);

    const Settings current = settings.current();
    if (current.customPalette && heldInUse && !savedSlotOf(palettes)) {
        Settings next      = current;
        next.customPalette = false;
        changeSettings(game, settings, next);
    }
}

void enterValue(GameContext& game, const CustomPaletteWiring& wiring) {
    PaletteEditorState&              ui    = game.paletteEditor;
    const std::optional<std::size_t> shade = shadeOf(ui.row);
    if (!shade || wiring.palettes == nullptr) {
        return;
    }
    ui.entering            = true;
    ui.place               = DigitPlace::HUNDREDS;
    ui.valueOnEntry        = channelValue(wiring.palettes->working, *shade, ui.channel);
    game.flow.keyRepeatTimer = 0;
}

// A or Start: an action on its row, entering the value on a shade row, nothing on the preset row.
void choose(GameContext& game, const CustomPaletteWiring& wiring) {
    const PaletteEditorRow row    = game.paletteEditor.row;
    const std::uint8_t     preset = game.paletteEditor.preset;

    if (shadeOf(row)) {
        enterValue(game, wiring);
        return;
    }
    if (!paletteEditorActionCanAct(row, preset, wiring.palettes)) {
        return;
    }

    switch (row) {
        case PaletteEditorRow::LOAD:
            if (hasUnsavedChanges(*wiring.palettes)) {
                openQuestion(game, PaletteEditorQuestion::LOAD_UNSAVED);
            } else {
                loadPreset(game, wiring);
            }
            return;
        case PaletteEditorRow::SAVE:
            if (presetSlot(*wiring.palettes, preset).present) {
                openQuestion(game, PaletteEditorQuestion::SAVE_OVER);
            } else {
                savePreset(game, wiring);
            }
            return;
        case PaletteEditorRow::CLEAR:
            openQuestion(game, PaletteEditorQuestion::CLEAR);
            return;
        default:
            return;
    }
}

// ── An entered value ──────────────────────────────────────────────────────────────────────────────

void stepValue(GameContext& game, const CustomPaletteWiring& wiring, int delta) {
    const PaletteEditorState&        ui    = game.paletteEditor;
    const std::optional<std::size_t> shade = shadeOf(ui.row);
    if (!shade || wiring.palettes == nullptr) {
        return;
    }
    const std::uint8_t value = channelValue(wiring.palettes->working, *shade, ui.channel);
    const std::uint8_t next  = steppedValue(value, ui.place, delta);
    if (next == value) {
        return;
    }
    setChannelValue(wiring.palettes->working, *shade, ui.channel, next);
    commitWorking(wiring);
    cue(game, SquareSfxId::TINK);
}

void movePlace(GameContext& game, int delta) {
    const int next = static_cast<int>(game.paletteEditor.place) + delta;
    if (next < static_cast<int>(DigitPlace::HUNDREDS) || next > static_cast<int>(DigitPlace::ONES)) {
        return;
    }
    game.paletteEditor.place = static_cast<DigitPlace>(next);
    cue(game, SquareSfxId::TINK);
}

void keepValue(GameContext& game) {
    game.paletteEditor.entering = false;
    cue(game, SquareSfxId::TINK);
}

// B: the value goes back to what it was when it was entered, unannounced.
void cancelValue(GameContext& game, const CustomPaletteWiring& wiring) {
    PaletteEditorState&              ui    = game.paletteEditor;
    const std::optional<std::size_t> shade = shadeOf(ui.row);
    ui.entering                            = false;
    if (!shade || wiring.palettes == nullptr) {
        return;
    }
    if (channelValue(wiring.palettes->working, *shade, ui.channel) == ui.valueOnEntry) {
        return;
    }
    setChannelValue(wiring.palettes->working, *shade, ui.channel, ui.valueOnEntry);
    commitWorking(wiring);
}

// ── A question ────────────────────────────────────────────────────────────────────────────────────

void pickAnswer(GameContext& game, bool yes) {
    if (game.paletteEditor.answerYes == yes) {
        return;
    }
    game.paletteEditor.answerYes = yes;
    cue(game, SquareSfxId::TINK);
}

void closeQuestion(GameContext& game) {
    game.paletteEditor.question  = PaletteEditorQuestion::NONE;
    game.paletteEditor.answerYes = false;
    game.screens.cursorVisible   = true;
    game.flow.timer1             = kScreenBlinkFrames;
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

void answer(GameContext& game, const SettingsWiring& settings, const CustomPaletteWiring& wiring) {
    if (game.paletteEditor.answerYes && wiring.palettes != nullptr) {
        switch (game.paletteEditor.question) {
            case PaletteEditorQuestion::LOAD_UNSAVED:
                loadPreset(game, wiring);
                break;
            case PaletteEditorQuestion::SAVE_OVER:
                savePreset(game, wiring);
                break;
            case PaletteEditorQuestion::CLEAR:
                clearPreset(game, settings, wiring);
                break;
            case PaletteEditorQuestion::NONE:
                break;
        }
    }
    closeQuestion(game);
}

// ── The tables ────────────────────────────────────────────────────────────────────────────────────

using Effect = void (*)(GameContext&, const SettingsWiring&, const CustomPaletteWiring&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kWalkBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { moveRow(g, -1); }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { moveRow(g, +1); }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& w) {
             sideways(g, w, -1);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& w) {
             sideways(g, w, +1);
         }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& w) { choose(g, w); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& w) { choose(g, w); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { leave(g); }},
};

// Up and Down are not here: an entered value reads them as held, on the key repeat, ahead of this
// table (paletteEditor).
constexpr std::array kEnteringBinds{
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { movePlace(g, -1); }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { movePlace(g, +1); }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { keepValue(g); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { keepValue(g); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring& w) {
             cancelValue(g, w);
         }},
};

// B and "no" both leave a question with nothing changed.
constexpr std::array kQuestionBinds{
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) {
             pickAnswer(g, false);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) {
             pickAnswer(g, true);
         }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring& s, const CustomPaletteWiring& w) {
             answer(g, s, w);
         }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring& s, const CustomPaletteWiring& w) {
             answer(g, s, w);
         }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const CustomPaletteWiring&) { closeQuestion(g); }},
};

void dispatch(GameContext& game, const SettingsWiring& settings, const CustomPaletteWiring& wiring,
              std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, settings, wiring);
            return;
        }
    }
}

// An entered value's frame: Up, then Down, on the key repeat - a press steps at once, a hold steps
// again after 23 frames and every 9 after that - and only then the table. A held direction is the
// whole frame's input.
void enteringFrame(GameContext& game, const SettingsWiring& settings,
                   const CustomPaletteWiring& wiring) {
    const bool upPressed   = pressed(game, Action::MenuUp);
    const bool upHeld      = held(game, Action::MenuUp);
    const bool downPressed = pressed(game, Action::MenuDown);
    const bool downHeld    = held(game, Action::MenuDown);

    if (upPressed || upHeld) {
        if (keyRepeatFire(game.flow.keyRepeatTimer, upPressed, upHeld)) {
            stepValue(game, wiring, +1);
        }
        return;
    }
    if (downPressed || downHeld) {
        if (keyRepeatFire(game.flow.keyRepeatTimer, downPressed, downHeld)) {
            stepValue(game, wiring, -1);
        }
        return;
    }
    dispatch(game, settings, wiring, kEnteringBinds);
}

}  // namespace

void openPaletteEditor(GameContext& game, const CustomPalettes* palettes) {
    game.paletteEditor.reset();
    if (palettes != nullptr) {
        game.paletteEditor.preset = savedSlotOf(*palettes).value_or(1);
    }
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::PALETTE_EDITOR;
}

bool presetCanStep(std::uint8_t preset, const CustomPalettes* palettes, int delta) noexcept {
    if (palettes == nullptr) {
        return false;
    }
    const int next = static_cast<int>(preset) + delta;
    return next >= 1 && next <= static_cast<int>(kCustomPaletteSlotCount);
}

std::uint8_t steppedValue(std::uint8_t value, DigitPlace place, int delta) noexcept {
    const int next = static_cast<int>(value) + delta * placeStep(place);
    return static_cast<std::uint8_t>(std::clamp(next, 0, 255));
}

bool paletteEditorActionCanAct(PaletteEditorRow action, std::uint8_t preset,
                               const CustomPalettes* palettes) noexcept {
    if (palettes == nullptr || preset < 1 || preset > kCustomPaletteSlotCount) {
        return false;
    }
    const CustomPaletteSlot& slot = palettes->slots[preset - 1];
    switch (action) {
        case PaletteEditorRow::LOAD:
            return slot.present && slot.shades != palettes->working;
        case PaletteEditorRow::SAVE:
            return !(slot.present && slot.shades == palettes->working);
        case PaletteEditorRow::CLEAR:
            return slot.present;
        default:
            return false;
    }
}

void paletteEditor(GameContext& game, const SettingsWiring& settings,
                   const CustomPaletteWiring& palettes) {
    blinkScreenCursor(game);
    game.paletteEditor.previewing = held(game, Action::Select);
    if (game.paletteEditor.previewing) {
        return;
    }
    if (game.paletteEditor.question != PaletteEditorQuestion::NONE) {
        dispatch(game, settings, palettes, kQuestionBinds);
    } else if (game.paletteEditor.entering) {
        enteringFrame(game, settings, palettes);
    } else {
        dispatch(game, settings, palettes, kWalkBinds);
    }
}

void installPaletteEditor(GameStateDispatcher& dispatcher, SettingsWiring settings,
                          CustomPaletteWiring palettes) {
    dispatcher.setHandler(GameState::PALETTE_EDITOR,
                          [settings = std::move(settings), palettes = std::move(palettes)](
                              GameContext& g) { paletteEditor(g, settings, palettes); });
}

}  // namespace kirpich::systems
