// The player's custom palette rotations and the document they persist in — behavioral tests over
// src/state/custom_rotations.h.
//
// Device-free except the store case, which runs through a hermetic SaveStore rooted at a temporary
// directory. The rotations are the port's own, so every asserted value comes from the surface's stated
// contract.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>

#include <retropp/save_store.h>

#include "render/palettes.h"
#include "state/custom_palettes.h"
#include "state/custom_rotations.h"
#include "state/settings.h"

namespace {

using kirpich::CustomRotations;
using kirpich::CustomRotationSlot;
using kirpich::kCustomRotationRecordBytes;
using kirpich::kCustomRotationSlotCount;
using kirpich::kCustomRotationsImageBytes;
using kirpich::kRotationLevelCount;
using kirpich::RotationProgression;
using kirpich::render::ShadeRamp;

// Four distinct colors built from one base, so a level's shades can be told apart.
ShadeRamp rampFrom(std::uint8_t base) {
    const auto c = [base](std::uint8_t step) {
        return retropp::Rgba8{.r = static_cast<std::uint8_t>(base + step),
                              .g = static_cast<std::uint8_t>(base + step + 1),
                              .b = static_cast<std::uint8_t>(base + step + 2)};
    };
    return ShadeRamp{.darkest = c(0), .dark = c(10), .light = c(20), .lightest = c(30)};
}

// Ten distinct palettes from one base, so a rotation can be told from every other rotation the same
// test builds and each of its levels from the other nine.
RotationProgression rotationFrom(std::uint8_t base) {
    RotationProgression r;
    for (std::size_t j = 0; j < kRotationLevelCount; ++j) {
        r.levels[j] = rampFrom(static_cast<std::uint8_t>(base + 3 * j));
    }
    return r;
}

CustomRotationSlot present(std::uint8_t base) {
    return CustomRotationSlot{.present = true, .progression = rotationFrom(base)};
}

// Slots 1, 4 and 7 present, slot 4 holding the working rotation.
CustomRotations seeded() {
    CustomRotations r;
    r.working  = rotationFrom(40);
    r.slots[0] = present(10);
    r.slots[3] = present(40);
    r.slots[6] = present(70);
    return r;
}

std::size_t recordOffset(std::size_t record) { return record * kCustomRotationRecordBytes; }

}  // namespace

// (1) A default set is one fresh working rotation - ten grayscales, the authoring flag off - over
// ninety-nine empty slots holding the same, and nothing is saved anywhere.
TEST(CustomRotations, DefaultSetIsOneFreshRotationOverEmptySlots) {
    const ShadeRamp fresh = kirpich::render::freshCustomPalette();

    const CustomRotations defaults;
    for (std::size_t j = 0; j < kRotationLevelCount; ++j) {
        EXPECT_EQ(defaults.working.levels[j], fresh) << "level " << j;
        EXPECT_FALSE(defaults.working.levels[j].mayBottomOutAtBlack) << "level " << j;
    }
    EXPECT_EQ(defaults.slots.size(), kCustomRotationSlotCount);
    for (std::size_t i = 0; i < kCustomRotationSlotCount; ++i) {
        EXPECT_FALSE(defaults.slots[i].present) << "slot " << i + 1;
        EXPECT_EQ(defaults.slots[i].progression, RotationProgression{}) << "slot " << i + 1;
    }
    EXPECT_FALSE(kirpich::savedSlotOf(defaults));
}

// (2) The image is one hundred records of 121 bytes: record 0 the working rotation with its presence
// written as 1, record k slot k; a record is the presence byte and then, level by level, red, green and
// blue for the darkest, dark, light and lightest shades - level j at 1 + 12j. Asserted at named
// offsets, so a transposed field, a reordered shade or a reordered level is caught.
TEST(CustomRotations, ImageIsOneHundredRecordsOfPresenceThenTenLevels) {
    EXPECT_EQ(kCustomRotationSlotCount, 99u);
    EXPECT_EQ(kCustomRotationRecordBytes, 121u);
    EXPECT_EQ(kCustomRotationsImageBytes, 12100u);

    const CustomRotations r     = seeded();
    const auto            image = kirpich::encodeCustomRotations(r);
    ASSERT_EQ(image.size(), kCustomRotationsImageBytes);

    const auto expectRecord = [&image](std::size_t record, std::uint8_t presence,
                                       const RotationProgression& progression) {
        const std::size_t at = recordOffset(record);
        EXPECT_EQ(image[at], presence) << "record " << record;
        for (std::size_t j = 0; j < kRotationLevelCount; ++j) {
            const ShadeRamp&                    level = progression.levels[j];
            const std::array<retropp::Rgba8, 4> order{level.darkest, level.dark, level.light,
                                                      level.lightest};
            const std::size_t                   levelAt = at + 1 + 12 * j;
            for (std::size_t s = 0; s < order.size(); ++s) {
                EXPECT_EQ(image[levelAt + 3 * s], order[s].r)
                    << "record " << record << " level " << j << " shade " << s;
                EXPECT_EQ(image[levelAt + 1 + 3 * s], order[s].g)
                    << "record " << record << " level " << j << " shade " << s;
                EXPECT_EQ(image[levelAt + 2 + 3 * s], order[s].b)
                    << "record " << record << " level " << j << " shade " << s;
            }
        }
    };

    expectRecord(0, 1, r.working);
    expectRecord(1, 1, r.slots[0].progression);
    expectRecord(2, 0, r.slots[1].progression);  // empty: presence 0, the fresh rotation's colors
    expectRecord(4, 1, r.slots[3].progression);
    expectRecord(7, 1, r.slots[6].progression);
    expectRecord(99, 0, r.slots[98].progression);
}

// (3) Encode and decode are inverse over a seeded set: alpha comes back 255, the authoring flag off,
// and the working rotation is saved in slot 4.
TEST(CustomRotations, CodecRoundTripsASeededSet) {
    const CustomRotations r     = seeded();
    const auto            image = kirpich::encodeCustomRotations(r);

    CustomRotations back;
    ASSERT_TRUE(kirpich::decodeCustomRotations(image, back));
    EXPECT_EQ(back, r);

    for (std::size_t i = 0; i < kCustomRotationSlotCount; ++i) {
        for (std::size_t j = 0; j < kRotationLevelCount; ++j) {
            const ShadeRamp& level = back.slots[i].progression.levels[j];
            EXPECT_EQ(level.darkest.a, 255) << "slot " << i + 1 << " level " << j;
            EXPECT_EQ(level.lightest.a, 255) << "slot " << i + 1 << " level " << j;
            EXPECT_FALSE(level.mayBottomOutAtBlack) << "slot " << i + 1 << " level " << j;
        }
    }
    EXPECT_EQ(back.working.levels[9].lightest.a, 255);
    EXPECT_EQ(kirpich::savedSlotOf(back), std::optional<std::uint8_t>{4});
}

// (4) A wrong length is refused outright and leaves the value as it was; any non-zero presence byte
// means present.
TEST(CustomRotations, WrongLengthRefusedAndAnyNonZeroPresenceMeansPresent) {
    const CustomRotations before = seeded();
    const auto            image  = kirpich::encodeCustomRotations(before);

    std::array<std::uint8_t, kCustomRotationsImageBytes + 1> longer{};
    std::copy(image.begin(), image.end(), longer.begin());

    for (const std::size_t length :
         {std::size_t{0}, kCustomRotationsImageBytes - 1, kCustomRotationsImageBytes + 1}) {
        CustomRotations target = before;
        EXPECT_FALSE(kirpich::decodeCustomRotations(
            std::span<const std::uint8_t>(longer.data(), length), target))
            << "length " << length;
        EXPECT_EQ(target, before) << "length " << length;
    }

    auto odd             = image;
    odd[recordOffset(2)] = 0x5A;  // slot 2 was empty
    CustomRotations decoded;
    ASSERT_TRUE(kirpich::decodeCustomRotations(odd, decoded));
    EXPECT_TRUE(decoded.slots[1].present);
    EXPECT_EQ(decoded.slots[1].progression, RotationProgression{});
}

// (5) The store: absent is an ordinary first run, a written set comes back equal, a damaged document
// leaves the defaults and stays on disk, and the document lives beside the settings and the custom
// palettes, each read at its own version.
TEST(CustomRotations, StoreRoundTrip) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_rotations_store_roundtrip";
    std::filesystem::remove_all(root);

    const CustomRotations saved = seeded();

    {
        auto            store = retropp::SaveStore::atPath(root);
        CustomRotations loaded;
        EXPECT_FALSE(kirpich::loadCustomRotations(store, loaded));
        EXPECT_EQ(loaded, CustomRotations{});
    }

    const kirpich::Settings settings{.shadeRamp       = 7,
                                     .customPalette   = true,
                                     .paletteRotation = kirpich::PaletteRotation::CUSTOM};
    kirpich::CustomPalettes palettes;
    palettes.working  = rampFrom(33);
    palettes.slots[2] = kirpich::CustomPaletteSlot{.present = true, .shades = rampFrom(33)};
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveSettings(settings, store));
        ASSERT_TRUE(kirpich::saveCustomPalettes(palettes, store));
        ASSERT_TRUE(kirpich::saveCustomRotations(saved, store));

        CustomRotations loaded;
        ASSERT_TRUE(kirpich::loadCustomRotations(store, loaded));
        EXPECT_EQ(loaded, saved);

        // And the other two documents still read after the rotations declared their own version.
        kirpich::Settings again;
        ASSERT_TRUE(kirpich::loadSettings(store, again));
        EXPECT_EQ(again, settings);
        kirpich::CustomPalettes palettesAgain;
        ASSERT_TRUE(kirpich::loadCustomPalettes(store, palettesAgain));
        EXPECT_EQ(palettesAgain, palettes);
    }

    {
        auto store     = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            if (!entry.path().filename().string().starts_with("rotations")) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no rotations document on disk to corrupt";

        CustomRotations loaded;
        EXPECT_FALSE(kirpich::loadCustomRotations(store, loaded));
        EXPECT_EQ(loaded, CustomRotations{});

        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (entry.path().filename().string().starts_with("rotations")) stillPresent = true;
        }
        EXPECT_TRUE(stillPresent);

        kirpich::Settings settingsBack;
        ASSERT_TRUE(kirpich::loadSettings(store, settingsBack));
        EXPECT_EQ(settingsBack, settings);
        kirpich::CustomPalettes palettesBack;
        ASSERT_TRUE(kirpich::loadCustomPalettes(store, palettesBack));
        EXPECT_EQ(palettesBack, palettes);
    }

    std::filesystem::remove_all(root);
}

// (6) Saved is a comparison: nothing with every slot empty, nothing when the only present slot
// differs, the matching slot's number, the FIRST of two present slots holding the same rotation, and
// nothing once those slots are cleared though their bytes stay. Unsaved changes follow from it: none
// on a fresh set, none while saved, and some when one channel of one level differs from every slot.
TEST(CustomRotations, SavedSlotIsTheFirstPresentSlotHoldingTheWorkingRotation) {
    CustomRotations r;
    EXPECT_FALSE(kirpich::hasUnsavedChanges(r)) << "a rotation never edited has nothing to lose";

    r.working = rotationFrom(40);
    EXPECT_FALSE(kirpich::savedSlotOf(r));
    EXPECT_TRUE(kirpich::hasUnsavedChanges(r));

    r.slots[5] = present(10);
    EXPECT_FALSE(kirpich::savedSlotOf(r)) << "a different rotation is not this one";

    r.slots[8] = present(40);
    EXPECT_EQ(kirpich::savedSlotOf(r), std::optional<std::uint8_t>{9});
    EXPECT_FALSE(kirpich::hasUnsavedChanges(r));

    r.slots[2] = present(40);
    EXPECT_EQ(kirpich::savedSlotOf(r), std::optional<std::uint8_t>{3})
        << "two slots hold it; the lower-numbered one names it";

    // One channel of one level apart from both: no longer saved anywhere.
    r.working.levels[6].light.g = static_cast<std::uint8_t>(r.working.levels[6].light.g + 1);
    EXPECT_FALSE(kirpich::savedSlotOf(r));
    EXPECT_TRUE(kirpich::hasUnsavedChanges(r));
    r.working.levels[6].light.g = static_cast<std::uint8_t>(r.working.levels[6].light.g - 1);

    // A slot holding the colors but not present is not saved: clearing a slot keeps its bytes.
    r.slots[2].present = false;
    r.slots[8].present = false;
    EXPECT_FALSE(kirpich::savedSlotOf(r));
    EXPECT_EQ(r.slots[2].progression, r.working) << "the cleared slot still holds the colors";
}

// (7) The present slots as a walk, both ways, from every kind of position: before the first, on a
// slot, between slots, on the last, past the end.
TEST(CustomRotations, PresentSlotWalkBothWays) {
    using Slot = std::optional<std::uint8_t>;
    CustomRotations r;
    r.slots[0]  = present(10);
    r.slots[3]  = present(20);
    r.slots[6]  = present(30);
    r.slots[98] = present(90);

    EXPECT_EQ(kirpich::nextPresentSlot(r, 0), Slot{1});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 1), Slot{4});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 4), Slot{7});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 7), Slot{99});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 50), Slot{99});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 99), Slot{});
    EXPECT_EQ(kirpich::nextPresentSlot(r, 200), Slot{});

    EXPECT_EQ(kirpich::previousPresentSlot(r, 100), Slot{99});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 99), Slot{7});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 7), Slot{4});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 50), Slot{7});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 4), Slot{1});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 1), Slot{});
    EXPECT_EQ(kirpich::previousPresentSlot(r, 200), Slot{99}) << "past the end means the last";

    const CustomRotations none;
    EXPECT_EQ(kirpich::nextPresentSlot(none, 0), Slot{});
    EXPECT_EQ(kirpich::previousPresentSlot(none, 100), Slot{});
}

// (8) A rotation read back from bytes equals the one it was written from even when it was built by
// hand with the engine's defaults: the codec stores no alpha and no flag, and the hand-built value
// carries alpha 255 and the flag off, so nothing is lost in either direction.
TEST(CustomRotations, AHandBuiltRotationSurvivesTheBytesUnchanged) {
    CustomRotations r;
    r.working.levels[0] = ShadeRamp{.darkest  = {.r = 1, .g = 2, .b = 3},
                                    .dark     = {.r = 4, .g = 5, .b = 6},
                                    .light    = {.r = 7, .g = 8, .b = 9},
                                    .lightest = {.r = 250, .g = 251, .b = 252}};
    r.working.levels[9] = ShadeRamp{.darkest  = {.r = 9, .g = 8, .b = 7},
                                    .dark     = {.r = 60, .g = 50, .b = 40},
                                    .light    = {.r = 160, .g = 150, .b = 140},
                                    .lightest = {.r = 249, .g = 248, .b = 247}};
    const auto image = kirpich::encodeCustomRotations(r);

    CustomRotations back;
    ASSERT_TRUE(kirpich::decodeCustomRotations(image, back));
    EXPECT_EQ(back.working, r.working);
    EXPECT_EQ(back.working.levels[0].darkest.a, 255);
    EXPECT_EQ(back.working.levels[0].lightest.b, 252);
    EXPECT_EQ(back.working.levels[9].dark.r, 60);
}
