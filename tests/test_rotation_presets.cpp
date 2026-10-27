// The palette rotations the game ships — behavioral tests over src/render/rotation_presets.h.
//
// Device-free. A preset's colors are authored rather than derived, so what is asserted here is what
// its name promises, in numbers: every entry runs dark to light as every ramp does, every entry has
// range enough to read, a preset changes at every level, and each one moves the way it is named.
// Whether the colors look hotter, colder or like a rainbow is judged by eye.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <utility>

#include "render/palettes.h"
#include "render/rotation_presets.h"
#include "state/rotation_progression.h"
#include "state/settings.h"

namespace {

using kirpich::PaletteRotation;
using kirpich::render::RotationPalettes;
using kirpich::render::ShadeRamp;

// Rec.601 luminance, the measure the ramp table's own dark-to-light check uses.
double luminance(retropp::Rgba8 c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

std::array<retropp::Rgba8, 4> shades(const ShadeRamp& r) {
    return {r.darkest, r.dark, r.light, r.lightest};
}

const std::array<std::pair<const char*, const RotationPalettes*>, 3> kPresets{{
    {"hotter", &kirpich::render::kHotterRotation},
    {"colder", &kirpich::render::kColderRotation},
    {"rainbow", &kirpich::render::kRainbowRotation},
}};

}  // namespace

// (1) Every entry of every preset runs dark to light by luminance, its lightest shade is at least 100
// above its darkest, and no two consecutive entries of a preset are the same palette, so every
// level-up changes the picture.
TEST(RotationPresets, EveryEntryRunsDarkToLightWithRangeAndEveryLevelChanges) {
    EXPECT_EQ(kirpich::kRotationLevelCount, 10u);
    for (const auto& [name, preset] : kPresets) {
        ASSERT_EQ(preset->size(), kirpich::kRotationLevelCount) << name;
        for (std::size_t level = 0; level < preset->size(); ++level) {
            const auto s = shades((*preset)[level]);
            for (std::size_t i = 0; i + 1 < s.size(); ++i) {
                EXPECT_LT(luminance(s[i]), luminance(s[i + 1]))
                    << name << " level " << level << ", shade " << i << " is not darker than shade "
                    << (i + 1);
            }
            EXPECT_GE(luminance(s[3]) - luminance(s[0]), 100.0)
                << name << " level " << level << " has too little range to read";
            EXPECT_FALSE((*preset)[level].mayBottomOutAtBlack) << name << " level " << level;
            if (level > 0) {
                EXPECT_NE((*preset)[level], (*preset)[level - 1])
                    << name << " level " << level << " repeats the level before it";
            }
        }
    }
}

// (2) Hotter and colder move the way they are named, level over level: hotter's darkest shade gains
// red while its dark shade loses green; colder's darkest shade gains blue while its dark shade loses
// red.
TEST(RotationPresets, HotterWarmsAndColderCoolsAtEveryLevel) {
    const RotationPalettes& hot  = kirpich::render::kHotterRotation;
    const RotationPalettes& cold = kirpich::render::kColderRotation;
    for (std::size_t level = 1; level < kirpich::kRotationLevelCount; ++level) {
        EXPECT_GT(hot[level].darkest.r, hot[level - 1].darkest.r) << "hotter level " << level;
        EXPECT_LT(hot[level].dark.g, hot[level - 1].dark.g) << "hotter level " << level;
        EXPECT_GT(cold[level].darkest.b, cold[level - 1].darkest.b) << "colder level " << level;
        EXPECT_LT(cold[level].dark.r, cold[level - 1].dark.r) << "colder level " << level;
    }
}

// (3) The rainbow is a walk through ten hues, so no two of its entries share a dark shade.
TEST(RotationPresets, TheRainbowsTenEntriesAreTenDifferentHues) {
    const RotationPalettes& rainbow = kirpich::render::kRainbowRotation;
    for (std::size_t a = 0; a < rainbow.size(); ++a) {
        for (std::size_t b = a + 1; b < rainbow.size(); ++b) {
            EXPECT_NE(rainbow[a].dark, rainbow[b].dark) << "levels " << a << " and " << b;
        }
    }
}

// (4) presetRotation names each preset's own table for the three preset modes, and nothing for OFF
// and CUSTOM, which have no table of their own.
TEST(RotationPresets, PresetRotationMapsEachModeToItsTable) {
    EXPECT_EQ(kirpich::render::presetRotation(PaletteRotation::HOTTER),
              &kirpich::render::kHotterRotation);
    EXPECT_EQ(kirpich::render::presetRotation(PaletteRotation::COLDER),
              &kirpich::render::kColderRotation);
    EXPECT_EQ(kirpich::render::presetRotation(PaletteRotation::RAINBOW),
              &kirpich::render::kRainbowRotation);
    EXPECT_EQ(kirpich::render::presetRotation(PaletteRotation::OFF), nullptr);
    EXPECT_EQ(kirpich::render::presetRotation(PaletteRotation::CUSTOM), nullptr);
}
