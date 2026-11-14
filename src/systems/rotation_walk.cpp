#include "systems/rotation_walk.h"

namespace kirpich::systems {

namespace {

// The walk runs in PaletteRotation's declaration order, so the enum's order is the walk's: a reordered
// enum would reorder what the player steps through.
static_assert(PaletteRotation::OFF < PaletteRotation::HOTTER &&
                  PaletteRotation::HOTTER < PaletteRotation::COLDER &&
                  PaletteRotation::COLDER < PaletteRotation::RAINBOW &&
                  PaletteRotation::RAINBOW < PaletteRotation::CUSTOM,
              "the walk steps through the modes in the order they are declared");

constexpr PaletteRotation kLastPreset = PaletteRotation::RAINBOW;

[[nodiscard]] RotationChoice modeChoice(PaletteRotation mode) noexcept {
    return RotationChoice{.mode = mode, .slot = 0};
}

[[nodiscard]] RotationChoice slotChoice(std::uint8_t slot) noexcept {
    return RotationChoice{.mode = PaletteRotation::CUSTOM, .slot = slot};
}

}  // namespace

std::optional<RotationChoice> rotationChoiceOf(const Settings&        settings,
                                               const CustomRotations* rotations) noexcept {
    if (settings.paletteRotation != PaletteRotation::CUSTOM) {
        return modeChoice(settings.paletteRotation);
    }
    if (rotations != nullptr) {
        if (const auto saved = savedSlotOf(*rotations)) {
            return slotChoice(*saved);
        }
    }
    return std::nullopt;
}

std::optional<RotationChoice> nextRotationChoice(std::optional<RotationChoice> from,
                                                 const CustomRotations* rotations,
                                                 int delta) noexcept {
    if (from && from->mode != PaletteRotation::CUSTOM) {
        const auto mode = static_cast<int>(from->mode);
        if (delta < 0) {
            if (from->mode == PaletteRotation::OFF) return std::nullopt;
            return modeChoice(static_cast<PaletteRotation>(mode - 1));
        }
        if (from->mode != kLastPreset) {
            return modeChoice(static_cast<PaletteRotation>(mode + 1));
        }
        if (rotations != nullptr) {
            if (const auto first = nextPresentSlot(*rotations, 0)) {
                return slotChoice(*first);
            }
        }
        return std::nullopt;
    }

    // On a slot, or off the walk. Off the walk counts as standing before the first slot going right
    // and after the last preset going left.
    const std::optional<std::uint8_t> slot =
        from ? std::optional<std::uint8_t>{from->slot} : std::nullopt;

    if (delta > 0) {
        if (rotations == nullptr) return std::nullopt;
        if (const auto next = nextPresentSlot(*rotations, slot.value_or(0))) {
            return slotChoice(*next);
        }
        return std::nullopt;
    }

    if (rotations != nullptr && slot) {
        if (const auto previous = previousPresentSlot(*rotations, *slot)) {
            return slotChoice(*previous);
        }
    }
    return modeChoice(kLastPreset);
}

}  // namespace kirpich::systems
