#pragma once

// The palette walk: every palette the game offers a player, in the order a selector steps through
// them. It is one sequence - the eighty built-in ramps in order, then every present slot of the
// player's custom palettes in slot order - so with slots 1, 4 and 7 saved it runs
// `... 79, 80, C01, C04, C07` as the Palette screen shows it.
//
// A position on the walk is a PaletteChoice. The Palette screen steps the player's settings along it
// (src/systems/palette_settings_screen.h), and anything else that picks one of the game's palettes
// walks the same sequence with the same end stops.

#include <cstdint>
#include <optional>

#include "render/palettes.h"         // ShadeRamp
#include "state/custom_palettes.h"   // CustomPalettes
#include "state/settings.h"          // Settings

namespace kirpich::systems {

// One stop on the walk: a built-in ramp 0-79, or a present custom slot numbered 1-99.
struct PaletteChoice {
    bool         custom = false;
    std::uint8_t value  = 0;

    friend constexpr bool operator==(const PaletteChoice&, const PaletteChoice&) = default;
};

// Where the settings stand on the walk: the clamped ramp while the custom palette is not in use; the
// slot the working palette is saved in while it is. Nothing while the custom palette is in use and
// saved in no slot - the Palette screen's `C` - or while it is in use with no palettes behind it.
[[nodiscard]] std::optional<PaletteChoice> paletteChoiceOf(const Settings&       settings,
                                                           const CustomPalettes* palettes) noexcept;

// One step of `delta` (negative is left, positive is right) from `from` over the ramps and the present
// slots of `palettes` (none when null). Nothing at an end stop.
//
// From a ramp, a step goes to the next or previous ramp; Right from the last ramp goes to the first
// present slot, and Left from the first ramp is an end stop. From a slot, a step goes to the next or
// previous present slot; Right from the last is an end stop and Left from the first goes to the last
// ramp. From off the walk (nothing), Left goes to the last ramp and Right to the first present slot.
[[nodiscard]] std::optional<PaletteChoice> nextPaletteChoice(std::optional<PaletteChoice> from,
                                                             const CustomPalettes*        palettes,
                                                             int delta) noexcept;

// The stop whose colors these are: the first ramp whose four shades equal them, else the first
// present slot that does, else nothing. Only the four colors are compared, so a ramp's own
// mayBottomOutAtBlack does not keep it from matching, and a ramp wins over a slot holding the same
// colors.
[[nodiscard]] std::optional<PaletteChoice> paletteChoiceOfColors(
    const render::ShadeRamp& shades, const CustomPalettes* palettes) noexcept;

// A stop's four colors, with mayBottomOutAtBlack off: a ramp's from the table, clamped as a stored
// ramp is, and a slot's from `palettes`. A slot number outside 1-99 reads the fresh custom palette.
[[nodiscard]] render::ShadeRamp colorsOf(PaletteChoice choice,
                                         const CustomPalettes& palettes) noexcept;

}  // namespace kirpich::systems
