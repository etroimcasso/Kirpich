#pragma once

// What the palette editor is currently showing: which row and channel the cursor is on, which preset
// the actions name, whether a value is being entered, which question is up, and whether the held
// preview has taken the screen.
//
// This is the screen's own state, not the player's. The palettes it edits live in
// state/custom_palettes.h and outlive a reset; where the cursor sits does not. It is a GameContext
// member of its own, because the screen owns its whole state space in one place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below, and
// opening the screen does the same before it seeds the preset.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "state/custom_palettes.h"  // ColorChannel, kShadeCount

namespace kirpich {

// The screen's rows, top to bottom: the preset selector, the three actions on the preset, then one
// row per shade of the working palette, darkest first.
enum class PaletteEditorRow : std::uint8_t {
    PRESET,
    LOAD,
    SAVE,
    CLEAR,
    SHADE_1,
    SHADE_2,
    SHADE_3,
    SHADE_4,
};

// How many rows the cursor walks. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kPaletteEditorRowCount =
    static_cast<std::size_t>(PaletteEditorRow::SHADE_4) + 1;

static_assert(static_cast<std::size_t>(PaletteEditorRow::SHADE_4) -
                      static_cast<std::size_t>(PaletteEditorRow::SHADE_1) + 1 ==
                  kShadeCount,
              "one shade row per shade of a palette");

// The shade a row edits, 0-3 from the darkest, or nothing for the preset row and the actions.
[[nodiscard]] constexpr std::optional<std::size_t> shadeOf(PaletteEditorRow row) noexcept {
    if (row < PaletteEditorRow::SHADE_1) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(row) - static_cast<std::size_t>(PaletteEditorRow::SHADE_1);
}

// The place of a value being entered: the digit Up and Down step.
enum class DigitPlace : std::uint8_t {
    HUNDREDS,
    TENS,
    ONES,
};

// What one step at `place` adds or takes away.
[[nodiscard]] constexpr int placeStep(DigitPlace place) noexcept {
    switch (place) {
        case DigitPlace::HUNDREDS:
            return 100;
        case DigitPlace::TENS:
            return 10;
        case DigitPlace::ONES:
            break;
    }
    return 1;
}

// Which question the screen is asking, if any.
enum class PaletteEditorQuestion : std::uint8_t {
    NONE,
    LOAD_UNSAVED,  // load preset, while the working palette has unsaved changes
    SAVE_OVER,     // save preset, over a slot holding a different palette
    CLEAR,         // clear preset
};

struct PaletteEditorState {
    // Where the cursor is: a row, and on a shade row the channel. The channel is kept while the
    // cursor is on the rows above the shades, and a shade row reached from them uses it again.
    PaletteEditorRow row     = PaletteEditorRow::PRESET;
    ColorChannel     channel = ColorChannel::RED;

    // The slot the three actions name, 1-99.
    std::uint8_t preset = 1;

    // A value is being entered: `place` is the digit Up and Down step, and `valueOnEntry` is what the
    // value was when it was entered, which B puts back.
    bool         entering     = false;
    DigitPlace   place        = DigitPlace::HUNDREDS;
    std::uint8_t valueOnEntry = 0;

    // The question up, and the answer the cursor is on. A question opens on "no".
    PaletteEditorQuestion question  = PaletteEditorQuestion::NONE;
    bool                  answerYes = false;

    // The held preview is up: Select was held on the frame the handler last ran. While it is set no
    // input table runs, so every other field keeps its value and the editor resumes on release.
    bool previewing = false;

    void reset() { *this = PaletteEditorState{}; }

    friend constexpr bool operator==(const PaletteEditorState&,
                                     const PaletteEditorState&) = default;
};

}  // namespace kirpich
