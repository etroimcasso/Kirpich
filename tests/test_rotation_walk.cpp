// The rotation walk - behavioral tests over src/systems/rotation_walk.h: `off`, the three presets, then
// the present slots of the player's rotations, with an end stop at each end.
//
// Device-free: the walk is a pure function of the settings and the rotations.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "state/custom_rotations.h"
#include "state/settings.h"
#include "systems/rotation_walk.h"

namespace {

using kirpich::CustomRotations;
using kirpich::CustomRotationSlot;
using kirpich::PaletteRotation;
using kirpich::RotationProgression;
using kirpich::Settings;
using kirpich::systems::nextRotationChoice;
using kirpich::systems::RotationChoice;
using kirpich::systems::rotationChoiceOf;

// Ten distinct palettes from one base, so rotations can be told apart.
RotationProgression distinct(std::uint8_t base) {
    RotationProgression r;
    for (std::size_t j = 0; j < kirpich::kRotationLevelCount; ++j) {
        const auto v = static_cast<std::uint8_t>(base + 3 * j);
        r.levels[j]  = kirpich::render::ShadeRamp{.darkest  = {.r = v, .g = 1, .b = 2},
                                                  .dark     = {.r = v, .g = 60, .b = 61},
                                                  .light    = {.r = v, .g = 120, .b = 121},
                                                  .lightest = {.r = v, .g = 200, .b = 201}};
    }
    return r;
}

// Slots 1, 4 and 7 saved.
CustomRotations threeSlots() {
    CustomRotations r;
    r.slots[0] = CustomRotationSlot{.present = true, .progression = distinct(10)};
    r.slots[3] = CustomRotationSlot{.present = true, .progression = distinct(40)};
    r.slots[6] = CustomRotationSlot{.present = true, .progression = distinct(70)};
    return r;
}

RotationChoice mode(PaletteRotation m) { return RotationChoice{.mode = m, .slot = 0}; }
RotationChoice slot(std::uint8_t s) { return RotationChoice{.mode = PaletteRotation::CUSTOM, .slot = s}; }

Settings withMode(PaletteRotation m) { return Settings{.paletteRotation = m}; }

}  // namespace

// (1) Where the settings stand: OFF and the presets are themselves; CUSTOM is the slot the working
// rotation is saved in, and nothing when it is saved in none or there are no rotations.
TEST(RotationWalk, TheSettingsStandOnTheirOwnStop) {
    CustomRotations r = threeSlots();
    for (const PaletteRotation m : {PaletteRotation::OFF, PaletteRotation::HOTTER,
                                    PaletteRotation::COLDER, PaletteRotation::RAINBOW}) {
        EXPECT_EQ(rotationChoiceOf(withMode(m), &r), mode(m)) << static_cast<int>(m);
        EXPECT_EQ(rotationChoiceOf(withMode(m), nullptr), mode(m)) << static_cast<int>(m);
    }

    r.working = r.slots[3].progression;
    EXPECT_EQ(rotationChoiceOf(withMode(PaletteRotation::CUSTOM), &r), slot(4));

    r.working = distinct(99);
    EXPECT_EQ(rotationChoiceOf(withMode(PaletteRotation::CUSTOM), &r), std::nullopt);
    EXPECT_EQ(rotationChoiceOf(withMode(PaletteRotation::CUSTOM), nullptr), std::nullopt);
}

// (2) Each step both ways, with the end stops: OFF Left is an end stop, RAINBOW Right lands on the
// first slot, the last slot Right is an end stop, the first slot Left lands on RAINBOW; from nothing,
// Left is RAINBOW and Right the first slot. With no present slot RAINBOW Right is an end stop, and
// with no rotations the presets are the whole walk.
TEST(RotationWalk, EachStepBothWaysWithTheEndStops) {
    const CustomRotations r = threeSlots();
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::OFF), &r, -1), std::nullopt);
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::OFF), &r, +1), mode(PaletteRotation::HOTTER));
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::HOTTER), &r, -1), mode(PaletteRotation::OFF));
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::RAINBOW), &r, +1), slot(1));
    EXPECT_EQ(nextRotationChoice(slot(1), &r, +1), slot(4));
    EXPECT_EQ(nextRotationChoice(slot(4), &r, -1), slot(1));
    EXPECT_EQ(nextRotationChoice(slot(7), &r, +1), std::nullopt);
    EXPECT_EQ(nextRotationChoice(slot(1), &r, -1), mode(PaletteRotation::RAINBOW));
    EXPECT_EQ(nextRotationChoice(std::nullopt, &r, -1), mode(PaletteRotation::RAINBOW));
    EXPECT_EQ(nextRotationChoice(std::nullopt, &r, +1), slot(1));

    const CustomRotations empty{};
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::RAINBOW), &empty, +1), std::nullopt);
    EXPECT_EQ(nextRotationChoice(std::nullopt, &empty, +1), std::nullopt);

    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::RAINBOW), nullptr, +1), std::nullopt);
    EXPECT_EQ(nextRotationChoice(mode(PaletteRotation::RAINBOW), nullptr, -1),
              mode(PaletteRotation::COLDER));
    EXPECT_EQ(nextRotationChoice(std::nullopt, nullptr, +1), std::nullopt);
    EXPECT_EQ(nextRotationChoice(std::nullopt, nullptr, -1), mode(PaletteRotation::RAINBOW));
}

// (3) The whole walk, enumerated from OFF by Right and back by Left.
TEST(RotationWalk, TheWholeWalkInOrder) {
    const CustomRotations             r = threeSlots();
    const std::vector<RotationChoice> expected{
        mode(PaletteRotation::OFF),     mode(PaletteRotation::HOTTER),
        mode(PaletteRotation::COLDER),  mode(PaletteRotation::RAINBOW),
        slot(1),                        slot(4),
        slot(7),
    };

    std::vector<RotationChoice>   right{mode(PaletteRotation::OFF)};
    std::optional<RotationChoice> at = right.back();
    while ((at = nextRotationChoice(at, &r, +1))) {
        right.push_back(*at);
    }
    EXPECT_EQ(right, expected);

    std::vector<RotationChoice> left{slot(7)};
    at = left.back();
    while ((at = nextRotationChoice(at, &r, -1))) {
        left.push_back(*at);
    }
    EXPECT_EQ(left, std::vector<RotationChoice>(expected.rbegin(), expected.rend()));
}

// (4) The walk steps the modes in their declared order: from OFF, Right visits the values 1, 2 and 3
// in turn, and CUSTOM, after the presets, is reached only through a slot.
TEST(RotationWalk, TheModesStepInTheirDeclaredOrder) {
    std::optional<RotationChoice> at = mode(PaletteRotation::OFF);
    for (int value = 1; value <= 3; ++value) {
        at = nextRotationChoice(at, nullptr, +1);
        ASSERT_TRUE(at);
        EXPECT_EQ(static_cast<int>(at->mode), value);
        EXPECT_EQ(at->slot, 0);
    }
    EXPECT_EQ(at->mode, PaletteRotation::RAINBOW);
    EXPECT_EQ(static_cast<int>(PaletteRotation::CUSTOM), 4);
}

// (5) Two stops are equal exactly when the mode and the slot are.
TEST(RotationWalk, StopsCompareByModeAndSlot) {
    EXPECT_EQ(slot(4), slot(4));
    EXPECT_NE(slot(4), slot(7));
    EXPECT_NE(mode(PaletteRotation::HOTTER), mode(PaletteRotation::COLDER));
    EXPECT_NE(mode(PaletteRotation::CUSTOM), slot(1));
    EXPECT_EQ(RotationChoice{}, mode(PaletteRotation::OFF));
}
