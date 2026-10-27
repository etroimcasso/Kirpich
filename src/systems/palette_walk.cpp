#include "systems/palette_walk.h"

#include <cstddef>

namespace kirpich::systems {

namespace {

constexpr std::uint8_t kLastRamp = static_cast<std::uint8_t>(render::kShadeRampCount - 1);

// Two palettes hold the same colors when their four shades are equal, whatever their flags say.
[[nodiscard]] bool sameColors(const render::ShadeRamp& a, const render::ShadeRamp& b) noexcept {
    return a.darkest == b.darkest && a.dark == b.dark && a.light == b.light &&
           a.lightest == b.lightest;
}

[[nodiscard]] render::ShadeRamp withoutFlag(const render::ShadeRamp& r) noexcept {
    return render::ShadeRamp{
        .darkest = r.darkest, .dark = r.dark, .light = r.light, .lightest = r.lightest};
}

}  // namespace

std::optional<PaletteChoice> paletteChoiceOf(const Settings&       settings,
                                             const CustomPalettes* palettes) noexcept {
    if (!settings.customPalette) {
        return PaletteChoice{.custom = false, .value = render::clampShadeRamp(settings.shadeRamp)};
    }
    if (palettes != nullptr) {
        if (const auto saved = savedSlotOf(*palettes)) {
            return PaletteChoice{.custom = true, .value = *saved};
        }
    }
    return std::nullopt;
}

std::optional<PaletteChoice> nextPaletteChoice(std::optional<PaletteChoice> from,
                                               const CustomPalettes* palettes, int delta) noexcept {
    if (from && !from->custom) {
        const std::uint8_t ramp = render::clampShadeRamp(from->value);
        if (delta < 0) {
            if (ramp == 0) return std::nullopt;
            return PaletteChoice{.custom = false, .value = static_cast<std::uint8_t>(ramp - 1)};
        }
        if (ramp < kLastRamp) {
            return PaletteChoice{.custom = false, .value = static_cast<std::uint8_t>(ramp + 1)};
        }
        if (palettes != nullptr) {
            if (const auto first = nextPresentSlot(*palettes, 0)) {
                return PaletteChoice{.custom = true, .value = *first};
            }
        }
        return std::nullopt;
    }

    // On a slot, or off the walk. Off the walk counts as standing before the first slot going right
    // and after the last ramp going left.
    const std::optional<std::uint8_t> slot =
        from ? std::optional<std::uint8_t>{from->value} : std::nullopt;

    if (delta > 0) {
        if (palettes == nullptr) return std::nullopt;
        if (const auto next = nextPresentSlot(*palettes, slot.value_or(0))) {
            return PaletteChoice{.custom = true, .value = *next};
        }
        return std::nullopt;
    }

    if (palettes != nullptr && slot) {
        if (const auto previous = previousPresentSlot(*palettes, *slot)) {
            return PaletteChoice{.custom = true, .value = *previous};
        }
    }
    return PaletteChoice{.custom = false, .value = kLastRamp};
}

std::optional<PaletteChoice> paletteChoiceOfColors(const render::ShadeRamp& shades,
                                                   const CustomPalettes*    palettes) noexcept {
    for (std::size_t ramp = 0; ramp < render::kShadeRampCount; ++ramp) {
        if (sameColors(render::kShadeRamps[ramp], shades)) {
            return PaletteChoice{.custom = false, .value = static_cast<std::uint8_t>(ramp)};
        }
    }
    if (palettes != nullptr) {
        for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
            const CustomPaletteSlot& slot = palettes->slots[i];
            if (slot.present && sameColors(slot.shades, shades)) {
                return PaletteChoice{.custom = true, .value = static_cast<std::uint8_t>(i + 1)};
            }
        }
    }
    return std::nullopt;
}

render::ShadeRamp colorsOf(PaletteChoice choice, const CustomPalettes& palettes) noexcept {
    if (!choice.custom) {
        return withoutFlag(render::kShadeRamps[render::clampShadeRamp(choice.value)]);
    }
    if (choice.value < 1 || choice.value > kCustomPaletteSlotCount) {
        return render::freshCustomPalette();
    }
    return withoutFlag(palettes.slots[choice.value - 1].shades);
}

}  // namespace kirpich::systems
