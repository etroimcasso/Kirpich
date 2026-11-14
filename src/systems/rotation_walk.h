#pragma once

// The rotation walk: every rotation the game offers a player, in the order a selector steps through
// them. It is one sequence - `off`, the three presets in PaletteRotation's order, then every present
// slot of the player's custom rotations in slot order - so with slots 1, 4 and 7 saved it runs
// `off, hotter, colder, rainbow, C01, C04, C07` as the rotation screen shows it.
//
// A position on the walk is a RotationChoice. The rotation screen steps the player's settings along it
// (src/render/screens/palette_rotation/rotation_screen.h).

#include <cstdint>
#include <optional>

#include "state/custom_rotations.h"  // CustomRotations
#include "state/settings.h"          // PaletteRotation, Settings

namespace kirpich::systems {

// One stop on the walk: OFF or a preset, with `slot` 0; or CUSTOM with `slot` a present slot, 1-99.
struct RotationChoice {
    PaletteRotation mode = PaletteRotation::OFF;
    std::uint8_t    slot = 0;

    friend constexpr bool operator==(const RotationChoice&, const RotationChoice&) = default;
};

// Where the settings stand on the walk: OFF and the presets are themselves; CUSTOM is the slot the
// working rotation is saved in. Nothing while the mode is CUSTOM and the working rotation is saved in
// no slot - the rotation screen's `C` - or while it is CUSTOM with no rotations behind it.
[[nodiscard]] std::optional<RotationChoice> rotationChoiceOf(
    const Settings& settings, const CustomRotations* rotations) noexcept;

// One step of `delta` (negative is left, positive is right) from `from` over `off`, the presets and the
// present slots of `rotations` (none when null). Nothing at an end stop.
//
// From `off` or a preset, a step goes to the next or previous of those; Right from the last preset goes
// to the first present slot, and Left from `off` is an end stop. From a slot, a step goes to the next
// or previous present slot; Right from the last is an end stop and Left from the first goes to the
// last preset. From off the walk (nothing), Left goes to the last preset and Right to the first
// present slot.
[[nodiscard]] std::optional<RotationChoice> nextRotationChoice(std::optional<RotationChoice> from,
                                                               const CustomRotations* rotations,
                                                               int delta) noexcept;

}  // namespace kirpich::systems
