// The player's custom palettes and the document they persist in — behavioral tests over
// src/state/custom_palettes.h.
//
// Device-free except the store case, which runs through a hermetic SaveStore rooted at a temporary
// directory. The palettes are the port's own, so every asserted value comes from the surface's stated
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
#include "state/settings.h"

namespace {

using kirpich::CustomPalettes;
using kirpich::CustomPaletteSlot;
using kirpich::kCustomPaletteRecordBytes;
using kirpich::kCustomPaletteSlotCount;
using kirpich::kCustomPalettesImageBytes;
using kirpich::render::ShadeRamp;

// Four distinct colors built from one base, so a record's shades can be told apart and a palette can
// be told from every other palette the same test builds.
ShadeRamp rampFrom(std::uint8_t base) {
    const auto c = [base](std::uint8_t step) {
        return retropp::Rgba8{.r = static_cast<std::uint8_t>(base + step),
                              .g = static_cast<std::uint8_t>(base + step + 1),
                              .b = static_cast<std::uint8_t>(base + step + 2)};
    };
    return ShadeRamp{.darkest = c(0), .dark = c(10), .light = c(20), .lightest = c(30)};
}

CustomPaletteSlot present(std::uint8_t base) {
    return CustomPaletteSlot{.present = true, .shades = rampFrom(base)};
}

// Slots 1, 4 and 7 present, slot 4 holding the working palette.
CustomPalettes seeded() {
    CustomPalettes p;
    p.working  = rampFrom(40);
    p.slots[0] = present(10);
    p.slots[3] = present(40);
    p.slots[6] = present(70);
    return p;
}

std::size_t recordOffset(std::size_t record) { return record * kCustomPaletteRecordBytes; }

}  // namespace

// (1) A fresh custom palette is the default grayscale's four colors with the authoring flag off, and
// a default set is that working palette over ninety-nine empty slots holding the same.
TEST(CustomPalettes, FreshPaletteIsTheGrayscaleWithTheFlagOff) {
    const ShadeRamp  fresh = kirpich::render::freshCustomPalette();
    const ShadeRamp& gray  = kirpich::render::kShadeRamps[kirpich::render::kDefaultShadeRamp];

    EXPECT_EQ(fresh.darkest, gray.darkest);
    EXPECT_EQ(fresh.dark, gray.dark);
    EXPECT_EQ(fresh.light, gray.light);
    EXPECT_EQ(fresh.lightest, gray.lightest);
    EXPECT_FALSE(fresh.mayBottomOutAtBlack);
    EXPECT_TRUE(gray.mayBottomOutAtBlack) << "the table's own grayscale carries the flag; a custom "
                                             "palette never does";

    const CustomPalettes defaults;
    EXPECT_EQ(defaults.working, fresh);
    EXPECT_EQ(defaults.slots.size(), kCustomPaletteSlotCount);
    for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
        EXPECT_FALSE(defaults.slots[i].present) << "slot " << i + 1;
        EXPECT_EQ(defaults.slots[i].shades, fresh) << "slot " << i + 1;
    }
    EXPECT_FALSE(kirpich::savedSlotOf(defaults)) << "nothing is saved anywhere on a fresh set";
}

// (2) The image is one hundred records of thirteen bytes: record 0 the working palette with its
// presence written as 1, record k slot k; a record is the presence byte and then red, green and blue
// for the darkest, dark, light and lightest shades. Asserted at named offsets, so a transposed field
// or a reordered shade is caught.
TEST(CustomPalettes, ImageIsOneHundredRecordsOfPresenceThenFourShades) {
    EXPECT_EQ(kCustomPaletteSlotCount, 99u);
    EXPECT_EQ(kCustomPaletteRecordBytes, 13u);
    EXPECT_EQ(kCustomPalettesImageBytes, 1300u);

    const CustomPalettes p     = seeded();
    const auto           image = kirpich::encodeCustomPalettes(p);
    ASSERT_EQ(image.size(), kCustomPalettesImageBytes);

    const auto expectRecord = [&image](std::size_t record, std::uint8_t presence,
                                       const ShadeRamp& shades) {
        const std::size_t at = recordOffset(record);
        EXPECT_EQ(image[at], presence) << "record " << record;
        const std::array<retropp::Rgba8, 4> order{shades.darkest, shades.dark, shades.light,
                                                  shades.lightest};
        for (std::size_t s = 0; s < order.size(); ++s) {
            EXPECT_EQ(image[at + 1 + 3 * s], order[s].r) << "record " << record << " shade " << s;
            EXPECT_EQ(image[at + 2 + 3 * s], order[s].g) << "record " << record << " shade " << s;
            EXPECT_EQ(image[at + 3 + 3 * s], order[s].b) << "record " << record << " shade " << s;
        }
    };

    expectRecord(0, 1, p.working);
    expectRecord(1, 1, p.slots[0].shades);
    expectRecord(2, 0, p.slots[1].shades);  // empty: presence 0, the fresh palette's colors
    expectRecord(4, 1, p.slots[3].shades);
    expectRecord(7, 1, p.slots[6].shades);
    expectRecord(99, 0, p.slots[98].shades);
}

// (3) Encode and decode are inverse over a seeded set, and a decoded palette equals one built by
// hand: alpha comes back 255 and the authoring flag off, which is how the hand-built one stands.
TEST(CustomPalettes, CodecRoundTripsASeededSet) {
    const CustomPalettes p     = seeded();
    const auto           image = kirpich::encodeCustomPalettes(p);

    CustomPalettes back;
    ASSERT_TRUE(kirpich::decodeCustomPalettes(image, back));
    EXPECT_EQ(back, p);

    for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
        EXPECT_EQ(back.slots[i].shades.darkest.a, 255) << "slot " << i + 1;
        EXPECT_FALSE(back.slots[i].shades.mayBottomOutAtBlack) << "slot " << i + 1;
    }
    EXPECT_EQ(back.working.lightest.a, 255);
    EXPECT_EQ(kirpich::savedSlotOf(back), std::optional<std::uint8_t>{4});
}

// (4) A wrong length is refused outright and leaves the value as it was; any non-zero presence byte
// means present.
TEST(CustomPalettes, WrongLengthRefusedAndAnyNonZeroPresenceMeansPresent) {
    const CustomPalettes before = seeded();
    const auto           image  = kirpich::encodeCustomPalettes(before);

    std::array<std::uint8_t, kCustomPalettesImageBytes + 1> longer{};
    std::copy(image.begin(), image.end(), longer.begin());

    for (const std::size_t length :
         {std::size_t{0}, kCustomPalettesImageBytes - 1, kCustomPalettesImageBytes + 1}) {
        CustomPalettes target = before;
        EXPECT_FALSE(kirpich::decodeCustomPalettes(
            std::span<const std::uint8_t>(longer.data(), length), target))
            << "length " << length;
        EXPECT_EQ(target, before) << "length " << length;
    }

    auto odd                     = image;
    odd[recordOffset(2)]         = 0x5A;  // slot 2 was empty
    CustomPalettes decoded;
    ASSERT_TRUE(kirpich::decodeCustomPalettes(odd, decoded));
    EXPECT_TRUE(decoded.slots[1].present);
    EXPECT_EQ(decoded.slots[1].shades, kirpich::render::freshCustomPalette());
}

// (5) The store: absent is an ordinary first run, a written set comes back equal, a damaged document
// leaves the defaults and stays on disk, and the document lives beside the settings, each read at its
// own version.
TEST(CustomPalettes, StoreRoundTrip) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_palettes_store_roundtrip";
    std::filesystem::remove_all(root);

    const CustomPalettes saved = seeded();

    {
        auto           store = retropp::SaveStore::atPath(root);
        CustomPalettes loaded;
        EXPECT_FALSE(kirpich::loadCustomPalettes(store, loaded));
        EXPECT_EQ(loaded, CustomPalettes{});
    }

    const kirpich::Settings settings{.shadeRamp = 7, .customPalette = true};
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveSettings(settings, store));
        ASSERT_TRUE(kirpich::saveCustomPalettes(saved, store));

        CustomPalettes loaded;
        ASSERT_TRUE(kirpich::loadCustomPalettes(store, loaded));
        EXPECT_EQ(loaded, saved);

        // And the settings still read after the palettes declared their own version.
        kirpich::Settings again;
        ASSERT_TRUE(kirpich::loadSettings(store, again));
        EXPECT_EQ(again, settings);
    }

    {
        auto store     = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            if (!entry.path().filename().string().starts_with("palettes")) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no palettes document on disk to corrupt";

        CustomPalettes loaded;
        EXPECT_FALSE(kirpich::loadCustomPalettes(store, loaded));
        EXPECT_EQ(loaded, CustomPalettes{});

        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (entry.path().filename().string().starts_with("palettes")) stillPresent = true;
        }
        EXPECT_TRUE(stillPresent);

        kirpich::Settings settingsBack;
        ASSERT_TRUE(kirpich::loadSettings(store, settingsBack));
        EXPECT_EQ(settingsBack, settings);
    }

    std::filesystem::remove_all(root);
}

// (6) Saved is a comparison: nothing with every slot empty, nothing when the only present slot
// differs, the matching slot's number, and the FIRST of two present slots holding the same palette.
TEST(CustomPalettes, SavedSlotIsTheFirstPresentSlotHoldingTheWorkingPalette) {
    CustomPalettes p;
    p.working = rampFrom(40);
    EXPECT_FALSE(kirpich::savedSlotOf(p));

    p.slots[5] = present(10);
    EXPECT_FALSE(kirpich::savedSlotOf(p)) << "a different palette is not this one";

    p.slots[8] = present(40);
    EXPECT_EQ(kirpich::savedSlotOf(p), std::optional<std::uint8_t>{9});

    p.slots[2] = present(40);
    EXPECT_EQ(kirpich::savedSlotOf(p), std::optional<std::uint8_t>{3})
        << "two slots hold it; the lower-numbered one names it";

    // A slot holding the colors but not present is not saved: clearing a slot keeps its bytes.
    p.slots[2].present = false;
    p.slots[8].present = false;
    EXPECT_FALSE(kirpich::savedSlotOf(p));
}

// (7) The present slots as a walk, both ways, from every kind of position: before the first, on a
// slot, between slots, on the last, past the end.
TEST(CustomPalettes, PresentSlotWalkBothWays) {
    using Slot = std::optional<std::uint8_t>;
    CustomPalettes p;
    p.slots[0]  = present(10);
    p.slots[3]  = present(20);
    p.slots[6]  = present(30);
    p.slots[98] = present(90);

    EXPECT_EQ(kirpich::nextPresentSlot(p, 0), Slot{1});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 1), Slot{4});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 4), Slot{7});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 7), Slot{99});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 50), Slot{99});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 99), Slot{});
    EXPECT_EQ(kirpich::nextPresentSlot(p, 200), Slot{});

    EXPECT_EQ(kirpich::previousPresentSlot(p, 100), Slot{99});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 99), Slot{7});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 7), Slot{4});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 50), Slot{7});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 4), Slot{1});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 1), Slot{});
    EXPECT_EQ(kirpich::previousPresentSlot(p, 200), Slot{99}) << "past the end means the last";

    const CustomPalettes none;
    EXPECT_EQ(kirpich::nextPresentSlot(none, 0), Slot{});
    EXPECT_EQ(kirpich::previousPresentSlot(none, 100), Slot{});
}

// (8) A record read back from bytes equals the palette it was written from even when that palette was
// built by hand with the engine's defaults: the codec stores no alpha and no flag, and the hand-built
// value carries alpha 255 and the flag off, so nothing is lost in either direction.
TEST(CustomPalettes, AHandBuiltPaletteSurvivesTheBytesUnchanged) {
    CustomPalettes p;
    p.working = ShadeRamp{.darkest  = {.r = 1, .g = 2, .b = 3},
                          .dark     = {.r = 4, .g = 5, .b = 6},
                          .light    = {.r = 7, .g = 8, .b = 9},
                          .lightest = {.r = 250, .g = 251, .b = 252}};
    const auto image = kirpich::encodeCustomPalettes(p);

    CustomPalettes back;
    ASSERT_TRUE(kirpich::decodeCustomPalettes(image, back));
    EXPECT_EQ(back.working, p.working);
    EXPECT_EQ(back.working.darkest.a, 255);
    EXPECT_EQ(back.working.lightest.b, 252);
}
