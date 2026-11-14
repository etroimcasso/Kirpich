#pragma once

// What the rotation editor is currently showing: which row and level cell the cursor is on, which
// preset the actions name, whether a level's number is being entered and where it stands on the
// palette walk, whether the name screen is up and what has been typed on it, which question is up,
// and whether the held preview has taken the screen.
//
// This is the screen's own state, not the player's. The rotations it edits live in
// state/custom_rotations.h and outlive a reset; where the cursor sits does not. It is a GameContext
// member of its own, because the screen owns its whole state space in one place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below, and
// opening the screen does the same before it seeds the preset.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "render/palettes.h"         // ShadeRamp
#include "state/custom_rotations.h"  // RotationName, blankRotationName
#include "state/settings.h"          // PaletteRotation
#include "systems/palette_walk.h"    // PaletteChoice
#include "systems/rotation_walk.h"   // RotationChoice

namespace kirpich {

// The screen's rows, top to bottom: the preset selector, the three actions on the preset, then the
// grid of the ten levels, which is one row the cursor enters - RotationEditorState::level says which
// cell.
enum class RotationEditorRow : std::uint8_t {
    PRESET,
    LOAD,
    SAVE,
    CLEAR,
    LEVELS,
};

// How many rows the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kRotationEditorRowCount =
    static_cast<std::size_t>(RotationEditorRow::LEVELS) + 1;

// Which question the screen is asking, if any.
enum class RotationEditorQuestion : std::uint8_t {
    NONE,
    LOAD_UNSAVED,  // load preset, while the working rotation has unsaved changes
    SAVE_OVER,     // save preset, over a slot holding a different rotation
    CLEAR,         // clear preset
};

struct RotationEditorState {
    // Where the cursor is: a row, and in the grid the level cell, 0-9. The level is kept while the
    // cursor is on the rows above the grid, and the held preview shows it.
    RotationEditorRow row   = RotationEditorRow::PRESET;
    std::uint8_t      level = 0;

    // What the three actions name: a shipped rotation with slot 0, or CUSTOM with a slot, 1-99.
    systems::RotationChoice preset{.mode = PaletteRotation::CUSTOM, .slot = 1};

    // A level's number is being entered: `entered` is where it stands on the palette walk - nothing
    // while the level's colors match no stop - and `colorsOnEntry` is the palette the level wore when
    // it was entered, which B puts back.
    bool                                  entering = false;
    std::optional<systems::PaletteChoice> entered;
    render::ShadeRamp                     colorsOnEntry{};

    // The name screen is up: `namePlace` is the character Up and Down turn, 0-6, and `typed` is the
    // name as typed so far. Nothing is written until the name is finished.
    bool         naming    = false;
    std::uint8_t namePlace = 0;
    RotationName typed     = blankRotationName();

    // The question up, and the answer the cursor is on. A question opens on "no".
    RotationEditorQuestion question  = RotationEditorQuestion::NONE;
    bool                   answerYes = false;

    // The held preview is up: Select was held on the frame the handler last ran.
    bool previewing = false;

    void reset() { *this = RotationEditorState{}; }

    friend bool operator==(const RotationEditorState&, const RotationEditorState&) = default;
};

}  // namespace kirpich
