// The palette walk — behavioral tests over src/systems/palette_walk.h.
//
// Device-free. The walk is the Palette screen's selector law: the eighty ramps in order, then the
// present custom slots in slot order, with the end stops tests/test_palette_settings_screen.cpp
// asserts through the screen. These cases assert the law on its own surface, and that the screen's
// arrows still agree with it.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "render/palettes.h"
#include "state/custom_palettes.h"
#include "state/settings.h"
#include "systems/palette_settings_screen.h"
#include "systems/palette_walk.h"

namespace {

using kirpich::CustomPalettes;
using kirpich::CustomPaletteSlot;
using kirpich::Settings;
using kirpich::render::ShadeRamp;
using kirpich::systems::PaletteChoice;

constexpr std::uint8_t kLastRamp = static_cast<std::uint8_t>(kirpich::render::kShadeRampCount - 1);

// Four distinct colors built from one base, so palettes built from different bases differ.
ShadeRamp rampFrom(std::uint8_t base) {
    const auto c = [base](std::uint8_t step) {
        return retropp::Rgba8{.r = static_cast<std::uint8_t>(base + step),
                              .g = static_cast<std::uint8_t>(base + step + 1),
                              .b = static_cast<std::uint8_t>(base + step + 2)};
    };
    return ShadeRamp{.darkest = c(0), .dark = c(10), .light = c(20), .lightest = c(30)};
}

// Slots 1, 4 and 7 present; the working palette is saved in none of them.
CustomPalettes seeded() {
    CustomPalettes p;
    p.working  = rampFrom(200);
    p.slots[0] = CustomPaletteSlot{.present = true, .shades = rampFrom(10)};
    p.slots[3] = CustomPaletteSlot{.present = true, .shades = rampFrom(40)};
    p.slots[6] = CustomPaletteSlot{.present = true, .shades = rampFrom(70)};
    return p;
}

PaletteChoice ramp(std::uint8_t r) { return PaletteChoice{.custom = false, .value = r}; }
PaletteChoice slot(std::uint8_t s) { return PaletteChoice{.custom = true, .value = s}; }

}  // namespace

// (1) Where the settings stand: the clamped ramp while the custom palette is not in use, the slot the
// working palette is saved in while it is, and nothing while it is in use and saved nowhere or with no
// palettes behind it.
TEST(PaletteWalk, PaletteChoiceOfTheSettings) {
    CustomPalettes p = seeded();

    EXPECT_EQ(kirpich::systems::paletteChoiceOf(Settings{.shadeRamp = 12}, &p), ramp(12));
    EXPECT_EQ(kirpich::systems::paletteChoiceOf(Settings{.shadeRamp = 200}, &p), ramp(kLastRamp));
    EXPECT_EQ(kirpich::systems::paletteChoiceOf(Settings{.shadeRamp = 12}, nullptr), ramp(12));

    const Settings custom{.shadeRamp = 12, .customPalette = true};
    EXPECT_EQ(kirpich::systems::paletteChoiceOf(custom, &p), std::nullopt)
        << "in use and saved in no slot is off the walk";
    EXPECT_EQ(kirpich::systems::paletteChoiceOf(custom, nullptr), std::nullopt);

    p.working = p.slots[3].shades;
    EXPECT_EQ(kirpich::systems::paletteChoiceOf(custom, &p), slot(4));
}

// (2) One step at a time with slots 1, 4 and 7 present: the ramps with the end stop at 0, the last
// ramp into slot 1, the present slots in order with the end stop after 7, back to the last ramp from
// slot 1, and from off the walk Left to the last ramp and Right to slot 1. With every slot empty, or
// no palettes at all, the last ramp is an end stop.
TEST(PaletteWalk, NextPaletteChoiceWalksTheRampsThenThePresentSlots) {
    using kirpich::systems::nextPaletteChoice;
    const CustomPalettes p = seeded();

    EXPECT_EQ(nextPaletteChoice(ramp(5), &p, +1), ramp(6));
    EXPECT_EQ(nextPaletteChoice(ramp(5), &p, -1), ramp(4));
    EXPECT_EQ(nextPaletteChoice(ramp(0), &p, -1), std::nullopt);
    EXPECT_EQ(nextPaletteChoice(ramp(kLastRamp), &p, +1), slot(1));
    EXPECT_EQ(nextPaletteChoice(slot(1), &p, +1), slot(4));
    EXPECT_EQ(nextPaletteChoice(slot(4), &p, +1), slot(7));
    EXPECT_EQ(nextPaletteChoice(slot(7), &p, +1), std::nullopt);
    EXPECT_EQ(nextPaletteChoice(slot(7), &p, -1), slot(4));
    EXPECT_EQ(nextPaletteChoice(slot(4), &p, -1), slot(1));
    EXPECT_EQ(nextPaletteChoice(slot(1), &p, -1), ramp(kLastRamp));
    EXPECT_EQ(nextPaletteChoice(std::nullopt, &p, -1), ramp(kLastRamp));
    EXPECT_EQ(nextPaletteChoice(std::nullopt, &p, +1), slot(1));

    const CustomPalettes empty;
    EXPECT_EQ(nextPaletteChoice(ramp(kLastRamp), &empty, +1), std::nullopt);
    EXPECT_EQ(nextPaletteChoice(std::nullopt, &empty, +1), std::nullopt);

    EXPECT_EQ(nextPaletteChoice(ramp(kLastRamp), nullptr, +1), std::nullopt);
    EXPECT_EQ(nextPaletteChoice(ramp(kLastRamp), nullptr, -1), ramp(kLastRamp - 1));
    EXPECT_EQ(nextPaletteChoice(std::nullopt, nullptr, +1), std::nullopt);
    EXPECT_EQ(nextPaletteChoice(std::nullopt, nullptr, -1), ramp(kLastRamp));
}

// (3) Colors find their stop: the first ramp holding them, a ramp over a slot holding the same colors,
// the first of two equal present slots, an empty slot never, and nothing for colors nobody holds. A
// ramp whose table entry carries mayBottomOutAtBlack still matches its colors without it.
TEST(PaletteWalk, ColorsFindTheFirstStopHoldingThem) {
    using kirpich::systems::paletteChoiceOfColors;
    CustomPalettes p = seeded();

    EXPECT_EQ(paletteChoiceOfColors(kirpich::render::kShadeRamps[9], &p), ramp(9));
    ASSERT_TRUE(kirpich::render::kShadeRamps[0].mayBottomOutAtBlack);
    EXPECT_EQ(paletteChoiceOfColors(kirpich::render::freshCustomPalette(), &p), ramp(0))
        << "the grayscale's four colors are ramp 0's, its flag aside";

    p.slots[2] = CustomPaletteSlot{.present = true, .shades = kirpich::render::kShadeRamps[9]};
    EXPECT_EQ(paletteChoiceOfColors(kirpich::render::kShadeRamps[9], &p), ramp(9))
        << "a ramp wins over a slot holding the same colors";

    p.slots[5] = CustomPaletteSlot{.present = true, .shades = rampFrom(40)};
    EXPECT_EQ(paletteChoiceOfColors(rampFrom(40), &p), slot(4)) << "the first of two equal slots";

    p.slots[8] = CustomPaletteSlot{.present = false, .shades = rampFrom(120)};
    EXPECT_EQ(paletteChoiceOfColors(rampFrom(120), &p), std::nullopt) << "an empty slot is no stop";
    EXPECT_EQ(paletteChoiceOfColors(rampFrom(40), nullptr), std::nullopt);
    EXPECT_EQ(paletteChoiceOfColors(rampFrom(150), &p), std::nullopt);
}

// (4) A stop's colors: a ramp's four from the table with the flag off whatever the table says, a
// slot's from the palettes, and the fresh palette for a slot number outside 1-99.
TEST(PaletteWalk, ColorsOfAStop) {
    using kirpich::systems::colorsOf;
    const CustomPalettes p = seeded();

    const ShadeRamp gray = colorsOf(ramp(0), p);
    EXPECT_EQ(gray.darkest, kirpich::render::kShadeRamps[0].darkest);
    EXPECT_EQ(gray.lightest, kirpich::render::kShadeRamps[0].lightest);
    EXPECT_FALSE(gray.mayBottomOutAtBlack);
    EXPECT_EQ(gray, kirpich::render::freshCustomPalette());

    EXPECT_EQ(colorsOf(ramp(200), p), colorsOf(ramp(kLastRamp), p)) << "a ramp past the table clamps";
    EXPECT_EQ(colorsOf(slot(4), p), rampFrom(40));
    EXPECT_EQ(colorsOf(slot(0), p), kirpich::render::freshCustomPalette());
    EXPECT_EQ(colorsOf(slot(100), p), kirpich::render::freshCustomPalette());
}

// (5) The Palette screen's arrows ask the same law. At every stop of the seeded walk, on a ramp, on
// each present slot and off the walk, in both directions, the walk moving is exactly what the screen
// says it can do.
TEST(PaletteWalk, ThePaletteScreenAgreesWithTheWalkAtEveryStop) {
    CustomPalettes p = seeded();

    const auto agree = [&p](const Settings& s, const char* where) {
        for (const int delta : {-1, +1}) {
            EXPECT_EQ(kirpich::systems::nextPaletteChoice(kirpich::systems::paletteChoiceOf(s, &p),
                                                          &p, delta)
                          .has_value(),
                      kirpich::systems::paletteCanStep(s, &p, delta))
                << where << " delta " << delta;
        }
    };

    for (std::uint8_t r = 0; r <= kLastRamp; ++r) {
        agree(Settings{.shadeRamp = r}, "ramp");
    }
    for (const std::size_t s : {std::size_t{1}, std::size_t{4}, std::size_t{7}}) {
        p.working = p.slots[s - 1].shades;
        agree(Settings{.shadeRamp = 3, .customPalette = true}, "slot");
    }
    p.working = rampFrom(200);
    agree(Settings{.shadeRamp = 3, .customPalette = true}, "off the walk");
}

// (6) Two stops are equal when they are the same kind and the same number, and a ramp is never a slot.
TEST(PaletteWalk, PaletteChoiceEquality) {
    EXPECT_EQ(ramp(4), ramp(4));
    EXPECT_NE(ramp(4), slot(4));
    EXPECT_NE(slot(4), slot(5));
    EXPECT_EQ(PaletteChoice{}, ramp(0));
}
