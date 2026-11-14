#include "render/screens/rotation_editor/rotation_editor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "data/sfx.h"                  // SquareSfxId
#include "render/rotation_presets.h"   // presetRotation, presetRotationName
#include "render/screens/rotation_editor/layout.h"  // the grid's shape: gridColumn, gridRow, kGridRows
#include "retropp/input.h"             // actionId
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"             // keyRepeatFire

namespace kirpich::systems {

namespace {

namespace layout = render::rotation_editor;

// The shipped rotations the preset selector offers, in PaletteRotation's order: every mode after OFF
// and before CUSTOM.
static_assert(PaletteRotation::OFF < PaletteRotation::HOTTER &&
                  PaletteRotation::RAINBOW < PaletteRotation::CUSTOM,
              "the shipped rotations stand between OFF and CUSTOM");
constexpr auto kFirstShipped =
    static_cast<PaletteRotation>(static_cast<std::uint8_t>(PaletteRotation::OFF) + 1);
constexpr auto kLastShipped =
    static_cast<PaletteRotation>(static_cast<std::uint8_t>(PaletteRotation::CUSTOM) - 1);

// The name wheel, in the order Up turns it: the letters, the symbols, the digits, the space - the
// top-score wheel's order.
constexpr std::string_view kRotationNameWheel = "abcdefghijklmnopqrstuvwxyz.-0123456789 ";

// The palettes an entered number reads with no custom palettes behind the screen. With none, the walk
// is the ramps alone, and no ramp's colors are read from here.
const CustomPalettes kNoPalettes{};

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

bool held(const GameContext& game, Action action) {
    return game.joypad.held.test(retropp::actionId(action));
}

void cue(GameContext& game, SquareSfxId sound) {
    game.audioCues.square = sound;
}

[[nodiscard]] bool isSlot(RotationChoice preset) noexcept {
    return preset.mode == PaletteRotation::CUSTOM && preset.slot >= 1 &&
           preset.slot <= kCustomRotationSlotCount;
}

CustomRotationSlot& presetSlot(CustomRotations& rotations, RotationChoice preset) {
    return rotations.slots[preset.slot - 1];
}

void writeDocument(const RotationWiring& wiring) {
    if (wiring.save) {
        wiring.save(*wiring.rotations);
    }
}

// The preset selector's next stop `delta`'s way over the shipped rotations, then slots 1-99, or
// nothing at an end stop.
[[nodiscard]] std::optional<RotationChoice> nextPreset(RotationChoice from, int delta) noexcept {
    if (from.mode != PaletteRotation::CUSTOM) {
        if (delta < 0) {
            if (from.mode == kFirstShipped) return std::nullopt;
            return RotationChoice{
                .mode = static_cast<PaletteRotation>(static_cast<std::uint8_t>(from.mode) - 1)};
        }
        if (from.mode == kLastShipped) {
            return RotationChoice{.mode = PaletteRotation::CUSTOM, .slot = 1};
        }
        return RotationChoice{
            .mode = static_cast<PaletteRotation>(static_cast<std::uint8_t>(from.mode) + 1)};
    }
    if (delta < 0) {
        if (from.slot <= 1) return RotationChoice{.mode = kLastShipped};
        return RotationChoice{.mode = PaletteRotation::CUSTOM,
                              .slot = static_cast<std::uint8_t>(from.slot - 1)};
    }
    if (from.slot >= kCustomRotationSlotCount) return std::nullopt;
    return RotationChoice{.mode = PaletteRotation::CUSTOM,
                          .slot = static_cast<std::uint8_t>(from.slot + 1)};
}

// ── Leaving ───────────────────────────────────────────────────────────────────────────────────────

void leave(GameContext& game) {
    cue(game, SquareSfxId::CHANGE_SCREEN);
    openRotationScreen(game, RotationScreenRow::EDITOR);
}

// ── The walk ──────────────────────────────────────────────────────────────────────────────────────

// Among the four upper rows the cursor steps a row, Up on the first being an end stop, and Down from
// the last enters the grid at the top of the column it was last in. In the grid Up and Down walk the
// column, Up from its top cell returning to the rows above and Down on its bottom cell an end stop.
void moveRow(GameContext& game, int delta) {
    RotationEditorState& ui = game.rotationEditor;

    if (ui.row != RotationEditorRow::LEVELS) {
        if (delta < 0) {
            if (ui.row == RotationEditorRow::PRESET) return;
            ui.row = static_cast<RotationEditorRow>(static_cast<int>(ui.row) - 1);
        } else if (ui.row == RotationEditorRow::CLEAR) {
            ui.row   = RotationEditorRow::LEVELS;
            ui.level = static_cast<std::uint8_t>(layout::gridColumn(ui.level) * layout::kGridRows);
        } else {
            ui.row = static_cast<RotationEditorRow>(static_cast<int>(ui.row) + 1);
        }
        cue(game, SquareSfxId::TINK);
        return;
    }

    const int row = layout::gridRow(ui.level);
    if (delta < 0) {
        if (row == 0) {
            ui.row = RotationEditorRow::CLEAR;
        } else {
            ui.level = static_cast<std::uint8_t>(ui.level - 1);
        }
    } else {
        if (row == layout::kGridRows - 1) return;
        ui.level = static_cast<std::uint8_t>(ui.level + 1);
    }
    cue(game, SquareSfxId::TINK);
}

// Left and Right in the grid: the same row of the other column, an end stop where there is none.
void crossColumn(GameContext& game, int delta) {
    RotationEditorState& ui   = game.rotationEditor;
    const int            next = static_cast<int>(ui.level) + delta * layout::kGridRows;
    if (next < 0 || next >= static_cast<int>(kRotationLevelCount)) {
        return;
    }
    ui.level = static_cast<std::uint8_t>(next);
    cue(game, SquareSfxId::TINK);
}

void stepPreset(GameContext& game, const RotationWiring& wiring, int delta) {
    RotationEditorState& ui = game.rotationEditor;
    if (!rotationEditorPresetCanStep(ui.preset, wiring.rotations, delta)) {
        return;
    }
    ui.preset = *nextPreset(ui.preset, delta);
    cue(game, SquareSfxId::TINK);
}

// Left and Right: the preset on its row, the other column in the grid, nothing on an action row.
void sideways(GameContext& game, const RotationWiring& wiring, int delta) {
    switch (game.rotationEditor.row) {
        case RotationEditorRow::PRESET:
            stepPreset(game, wiring, delta);
            return;
        case RotationEditorRow::LEVELS:
            crossColumn(game, delta);
            return;
        default:
            return;
    }
}

void openQuestion(GameContext& game, RotationEditorQuestion question) {
    game.rotationEditor.question  = question;
    game.rotationEditor.answerYes = false;
    game.screens.cursorVisible    = true;
    game.flow.timer1              = kScreenBlinkFrames;
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

// The name screen, holding the slot's name - or its number where it has none - left-aligned.
void openNaming(GameContext& game, const CustomRotations& rotations) {
    RotationEditorState& ui = game.rotationEditor;
    const std::string    text = rotationPresetText(ui.preset, rotations);

    ui.naming    = true;
    ui.namePlace = 0;
    ui.typed     = blankRotationName();
    std::copy_n(text.begin(), std::min(text.size(), kRotationNameLength), ui.typed.begin());

    game.flow.keyRepeatTimer   = 0;
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

// The three writes the actions make, after any question is answered yes. The save's write is the name
// screen's finish (finishName).
void loadPreset(GameContext& game, const RotationWiring& wiring) {
    CustomRotations&     rotations = *wiring.rotations;
    const RotationChoice preset    = game.rotationEditor.preset;
    if (const render::RotationPalettes* table = render::presetRotation(preset.mode)) {
        rotations.working.levels = *table;
    } else {
        rotations.working = presetSlot(rotations, preset).progression;
    }
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);
}

// Clearing the slot that holds the rotation in use - with no other slot holding it - turns the
// rotation off, so the game goes back to the palette the player chose. The working rotation is left
// as it is.
void clearPreset(GameContext& game, const SettingsWiring& settings, const RotationWiring& wiring) {
    CustomRotations&    rotations = *wiring.rotations;
    CustomRotationSlot& slot      = presetSlot(rotations, game.rotationEditor.preset);
    const bool          heldInUse = slot.progression == rotations.working;

    slot = CustomRotationSlot{};
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);

    const Settings current = settings.current();
    if (current.paletteRotation == PaletteRotation::CUSTOM && heldInUse &&
        !savedSlotOf(rotations)) {
        Settings next        = current;
        next.paletteRotation = PaletteRotation::OFF;
        changeSettings(game, settings, next);
    }
}

void enterNumber(GameContext& game, const RotationWiring& wiring, const CustomPalettes* palettes) {
    if (wiring.rotations == nullptr) {
        return;
    }
    RotationEditorState&     ui     = game.rotationEditor;
    const render::ShadeRamp& colors = wiring.rotations->working.levels[ui.level];
    ui.entering                     = true;
    ui.entered                      = paletteChoiceOfColors(colors, palettes);
    ui.colorsOnEntry                = colors;
    game.flow.keyRepeatTimer        = 0;
}

// A or Start: an action on its row, entering the number in the grid, nothing on the preset row.
void choose(GameContext& game, const RotationWiring& wiring, const CustomPalettes* palettes) {
    const RotationEditorRow row    = game.rotationEditor.row;
    const RotationChoice    preset = game.rotationEditor.preset;

    if (row == RotationEditorRow::LEVELS) {
        enterNumber(game, wiring, palettes);
        return;
    }
    if (!rotationEditorActionCanAct(row, preset, wiring.rotations)) {
        return;
    }

    CustomRotations& rotations = *wiring.rotations;
    switch (row) {
        case RotationEditorRow::LOAD:
            if (hasUnsavedChanges(rotations)) {
                openQuestion(game, RotationEditorQuestion::LOAD_UNSAVED);
            } else {
                loadPreset(game, wiring);
            }
            return;
        case RotationEditorRow::SAVE: {
            const CustomRotationSlot& slot = presetSlot(rotations, preset);
            if (slot.present && slot.progression != rotations.working) {
                openQuestion(game, RotationEditorQuestion::SAVE_OVER);
            } else {
                openNaming(game, rotations);
            }
            return;
        }
        case RotationEditorRow::CLEAR:
            openQuestion(game, RotationEditorQuestion::CLEAR);
            return;
        default:
            return;
    }
}

// ── An entered number ─────────────────────────────────────────────────────────────────────────────

// How many stops of the palette walk Up and Down move an entered number.
constexpr int kNumberTenStep = 10;

// `delta` stops along the palette walk (negative is back), from where the entered number stands -
// never from a stop read back off the level's colors, which would put a slot holding a ramp's colors
// back on that ramp and leave every slot after it out of reach. A move of several stops clamps: it
// runs as far as the walk goes and stops at its end, and is an end stop only when it cannot move at
// all.
void stepNumber(GameContext& game, const RotationWiring& wiring, const CustomPalettes* palettes,
                int delta) {
    RotationEditorState& ui = game.rotationEditor;
    if (wiring.rotations == nullptr) {
        return;
    }
    const int                    direction = delta < 0 ? -1 : +1;
    std::optional<PaletteChoice> target;
    std::optional<PaletteChoice> at = ui.entered;
    for (int i = 0; i < (delta < 0 ? -delta : delta); ++i) {
        const std::optional<PaletteChoice> next = nextPaletteChoice(at, palettes, direction);
        if (!next) {
            break;
        }
        target = next;
        at     = next;
    }
    if (!target) {
        return;
    }
    ui.entered = target;
    wiring.rotations->working.levels[ui.level] =
        colorsOf(*target, palettes != nullptr ? *palettes : kNoPalettes);
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);
}

void keepNumber(GameContext& game) {
    game.rotationEditor.entering = false;
    game.rotationEditor.entered.reset();
    cue(game, SquareSfxId::TINK);
}

// B: the level goes back to the palette it wore when it was entered, unannounced.
void putBackNumber(GameContext& game, const RotationWiring& wiring) {
    RotationEditorState& ui = game.rotationEditor;
    ui.entering             = false;
    ui.entered.reset();
    if (wiring.rotations == nullptr) {
        return;
    }
    render::ShadeRamp& colors = wiring.rotations->working.levels[ui.level];
    if (colors == ui.colorsOnEntry) {
        return;
    }
    colors = ui.colorsOnEntry;
    writeDocument(wiring);
}

// ── The name screen ───────────────────────────────────────────────────────────────────────────────

void turnName(GameContext& game, int delta) {
    RotationEditorState& ui = game.rotationEditor;
    ui.typed[ui.namePlace]  = turnedNameWheel(ui.typed[ui.namePlace], delta);
    cue(game, SquareSfxId::TINK);
}

// The save itself: the slot takes the working rotation under the name typed - or under its own
// number where every place is a space.
void finishName(GameContext& game, const RotationWiring& wiring) {
    RotationEditorState& ui = game.rotationEditor;
    ui.naming               = false;
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    if (wiring.rotations == nullptr) {
        return;
    }

    RotationName name = ui.typed;
    if (rotationNameText(name).empty()) {
        const std::string number = std::to_string(ui.preset.slot);
        name                     = blankRotationName();
        std::copy_n(number.begin(), std::min(number.size(), kRotationNameLength), name.begin());
    }

    CustomRotations& rotations   = *wiring.rotations;
    presetSlot(rotations, ui.preset) =
        CustomRotationSlot{.present = true, .name = name, .progression = rotations.working};
    writeDocument(wiring);
    cue(game, SquareSfxId::TINK);
}

// A: the next place, a blank one starting at `a`; on the last place, the name is finished.
void nextNamePlace(GameContext& game, const RotationWiring& wiring) {
    RotationEditorState& ui = game.rotationEditor;
    if (ui.namePlace + std::size_t{1} >= kRotationNameLength) {
        finishName(game, wiring);
        return;
    }
    ++ui.namePlace;
    if (ui.typed[ui.namePlace] == ' ') {
        ui.typed[ui.namePlace] = 'a';
    }
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

// B: the previous place, unannounced; on the first place, the save is canceled and nothing written.
void previousNamePlace(GameContext& game) {
    RotationEditorState& ui = game.rotationEditor;
    if (ui.namePlace == 0) {
        ui.naming                  = false;
        game.screens.cursorVisible = true;
        game.flow.timer1           = kScreenBlinkFrames;
        cue(game, SquareSfxId::CHANGE_SCREEN);
        return;
    }
    --ui.namePlace;
}

// ── A question ────────────────────────────────────────────────────────────────────────────────────

void pickAnswer(GameContext& game, bool yes) {
    if (game.rotationEditor.answerYes == yes) {
        return;
    }
    game.rotationEditor.answerYes = yes;
    cue(game, SquareSfxId::TINK);
}

void closeQuestion(GameContext& game) {
    game.rotationEditor.question  = RotationEditorQuestion::NONE;
    game.rotationEditor.answerYes = false;
    game.screens.cursorVisible    = true;
    game.flow.timer1              = kScreenBlinkFrames;
    cue(game, SquareSfxId::CHANGE_SCREEN);
}

void answer(GameContext& game, const SettingsWiring& settings, const RotationWiring& wiring) {
    const RotationEditorQuestion question = game.rotationEditor.question;
    const bool yes = game.rotationEditor.answerYes && wiring.rotations != nullptr;
    if (yes && question == RotationEditorQuestion::SAVE_OVER) {
        // The question is answered; the name screen takes the screen in its place.
        closeQuestion(game);
        openNaming(game, *wiring.rotations);
        return;
    }
    if (yes) {
        switch (question) {
            case RotationEditorQuestion::LOAD_UNSAVED:
                loadPreset(game, wiring);
                break;
            case RotationEditorQuestion::CLEAR:
                clearPreset(game, settings, wiring);
                break;
            case RotationEditorQuestion::SAVE_OVER:
            case RotationEditorQuestion::NONE:
                break;
        }
    }
    closeQuestion(game);
}

// ── The held preview ──────────────────────────────────────────────────────────────────────────────

void stepPreviewLevel(GameContext& game, int delta) {
    const int next = static_cast<int>(game.rotationEditor.level) + delta;
    if (next < 0 || next >= static_cast<int>(kRotationLevelCount)) {
        return;
    }
    game.rotationEditor.level = static_cast<std::uint8_t>(next);
    cue(game, SquareSfxId::TINK);
}

// ── The tables ────────────────────────────────────────────────────────────────────────────────────

using Effect = void (*)(GameContext&, const SettingsWiring&, const RotationWiring&,
                        const CustomPalettes*);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kWalkBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             moveRow(g, -1);
         }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             moveRow(g, +1);
         }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r, const CustomPalettes*) {
             sideways(g, r, -1);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r, const CustomPalettes*) {
             sideways(g, r, +1);
         }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r,
            const CustomPalettes* p) { choose(g, r, p); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r,
            const CustomPalettes* p) { choose(g, r, p); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             leave(g);
         }},
};

// The four directions are not here: an entered number reads them as held, on the key repeat, ahead
// of this table (enteringFrame).
constexpr std::array kEnteringBinds{
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             keepNumber(g);
         }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             keepNumber(g);
         }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r, const CustomPalettes*) {
             putBackNumber(g, r);
         }},
};

// Up and Down are not here: the name screen reads them as held, on the key repeat, ahead of this
// table (namingFrame). Left and Right do nothing.
constexpr std::array kNamingBinds{
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r, const CustomPalettes*) {
             nextNamePlace(g, r);
         }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring&, const RotationWiring& r, const CustomPalettes*) {
             finishName(g, r);
         }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             previousNamePlace(g);
         }},
};

// B and "no" both leave a question with nothing changed.
constexpr std::array kQuestionBinds{
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             pickAnswer(g, false);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             pickAnswer(g, true);
         }},
    Bind{Action::Confirm,
         [](GameContext& g, const SettingsWiring& s, const RotationWiring& r,
            const CustomPalettes*) { answer(g, s, r); }},
    Bind{Action::Start,
         [](GameContext& g, const SettingsWiring& s, const RotationWiring& r,
            const CustomPalettes*) { answer(g, s, r); }},
    Bind{Action::Back,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             closeQuestion(g);
         }},
};

// Up and Right are the next level, Down and Left the previous one.
constexpr std::array kPreviewBinds{
    Bind{Action::MenuUp,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             stepPreviewLevel(g, +1);
         }},
    Bind{Action::MenuRight,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             stepPreviewLevel(g, +1);
         }},
    Bind{Action::MenuDown,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             stepPreviewLevel(g, -1);
         }},
    Bind{Action::MenuLeft,
         [](GameContext& g, const SettingsWiring&, const RotationWiring&, const CustomPalettes*) {
             stepPreviewLevel(g, -1);
         }},
};

void dispatch(GameContext& game, const SettingsWiring& settings, const RotationWiring& rotations,
              const CustomPalettes* palettes, std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game, settings, rotations, palettes);
            return;
        }
    }
}

// One direction read on the key repeat, and the step it makes.
struct Repeated {
    Action action;
    int    delta;
};

// Directions on the key repeat - a press fires at once, a hold fires again after 23 frames and every
// 9 after that - each read ahead of the ones after it. Returns whether any was down, which makes a
// held direction the whole frame's input.
template <typename Fire>
bool repeatedDirections(GameContext& game, std::initializer_list<Repeated> directions, Fire fire) {
    for (const Repeated& direction : directions) {
        const bool isPressed = pressed(game, direction.action);
        const bool isHeld    = held(game, direction.action);
        if (isPressed || isHeld) {
            if (keyRepeatFire(game.flow.keyRepeatTimer, isPressed, isHeld)) {
                fire(direction.delta);
            }
            return true;
        }
    }
    return false;
}

// An entered number's frame: Left and Right one stop, then Up and Down ten, on the key repeat, and
// only then the table.
void enteringFrame(GameContext& game, const SettingsWiring& settings,
                   const RotationWiring& rotations, const CustomPalettes* palettes) {
    if (repeatedDirections(game,
                           {{Action::MenuLeft, -1},
                            {Action::MenuRight, +1},
                            {Action::MenuUp, +kNumberTenStep},
                            {Action::MenuDown, -kNumberTenStep}},
                           [&](int delta) { stepNumber(game, rotations, palettes, delta); })) {
        return;
    }
    dispatch(game, settings, rotations, palettes, kEnteringBinds);
}

// The name screen's frame: Up, then Down, on the key repeat, and only then the table.
void namingFrame(GameContext& game, const SettingsWiring& settings, const RotationWiring& rotations,
                 const CustomPalettes* palettes) {
    if (repeatedDirections(game, {{Action::MenuUp, +1}, {Action::MenuDown, -1}},
                           [&](int delta) { turnName(game, delta); })) {
        return;
    }
    dispatch(game, settings, rotations, palettes, kNamingBinds);
}

}  // namespace

void openRotationEditor(GameContext& game, const CustomRotations* rotations) {
    game.rotationEditor.reset();
    if (rotations != nullptr) {
        game.rotationEditor.preset = RotationChoice{.mode = PaletteRotation::CUSTOM,
                                                    .slot = savedSlotOf(*rotations).value_or(1)};
    }
    game.screens.cursorVisible = true;
    game.flow.timer1           = kScreenBlinkFrames;
    game.flow.gameState        = GameState::ROTATION_EDITOR;
}

bool rotationEditorPresetCanStep(RotationChoice preset, const CustomRotations* rotations,
                                 int delta) noexcept {
    return rotations != nullptr && nextPreset(preset, delta).has_value();
}

bool rotationEditorActionCanAct(RotationEditorRow action, RotationChoice preset,
                                const CustomRotations* rotations) noexcept {
    if (rotations == nullptr) {
        return false;
    }
    if (const render::RotationPalettes* table = render::presetRotation(preset.mode)) {
        return action == RotationEditorRow::LOAD && *table != rotations->working.levels;
    }
    if (!isSlot(preset)) {
        return false;
    }
    const CustomRotationSlot& slot = rotations->slots[preset.slot - 1];
    switch (action) {
        case RotationEditorRow::LOAD:
            return slot.present && slot.progression != rotations->working;
        case RotationEditorRow::SAVE:
            return true;
        case RotationEditorRow::CLEAR:
            return slot.present;
        default:
            return false;
    }
}

std::string rotationPresetText(RotationChoice preset, const CustomRotations& rotations) {
    if (preset.mode != PaletteRotation::CUSTOM) {
        return std::string{render::presetRotationName(preset.mode)};
    }
    if (!isSlot(preset)) {
        return {};
    }
    std::string name = rotationNameText(rotations.slots[preset.slot - 1].name);
    if (name.empty()) {
        name = std::to_string(preset.slot);
    }
    return name;
}

char turnedNameWheel(char current, int delta) noexcept {
    const auto size = static_cast<int>(kRotationNameWheel.size());
    std::size_t at  = kRotationNameWheel.find(current);
    if (at == std::string_view::npos) {
        at = kRotationNameWheel.find(' ');
    }
    const int next = ((static_cast<int>(at) + delta) % size + size) % size;
    return kRotationNameWheel[static_cast<std::size_t>(next)];
}

std::optional<PaletteChoice> levelPaletteChoice(const RotationEditorState& ui,
                                                const CustomRotations&     rotations,
                                                const CustomPalettes*      palettes,
                                                std::size_t                level) noexcept {
    if (ui.entering && level == ui.level) {
        return ui.entered;
    }
    return paletteChoiceOfColors(rotations.working.levels[level], palettes);
}

bool enteredNumberCanStep(const RotationEditorState& ui, const CustomPalettes* palettes,
                          int delta) noexcept {
    return ui.entering && nextPaletteChoice(ui.entered, palettes, delta).has_value();
}

void rotationEditor(GameContext& game, const SettingsWiring& settings,
                    const RotationWiring& rotations, const CustomPalettes* palettes) {
    RotationEditorState& ui = game.rotationEditor;
    blinkScreenCursor(game);
    ui.previewing = held(game, Action::Select);
    if (ui.previewing) {
        // Only the walk steps the preview's level. Every other mode keeps its place until release.
        if (!ui.entering && !ui.naming && ui.question == RotationEditorQuestion::NONE) {
            dispatch(game, settings, rotations, palettes, kPreviewBinds);
        }
        return;
    }
    if (ui.naming) {
        namingFrame(game, settings, rotations, palettes);
    } else if (ui.question != RotationEditorQuestion::NONE) {
        dispatch(game, settings, rotations, palettes, kQuestionBinds);
    } else if (ui.entering) {
        enteringFrame(game, settings, rotations, palettes);
    } else {
        dispatch(game, settings, rotations, palettes, kWalkBinds);
    }
}

void installRotationEditor(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           RotationWiring rotations, const CustomPalettes* palettes) {
    dispatcher.setHandler(GameState::ROTATION_EDITOR,
                          [settings = std::move(settings), rotations = std::move(rotations),
                           palettes](GameContext& g) {
                              rotationEditor(g, settings, rotations, palettes);
                          });
}

}  // namespace kirpich::systems
