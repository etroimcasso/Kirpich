// The player's display settings and the document they persist in — behavioral tests over
// src/state/settings.h.
//
// Device-free except the store cases, which run through a hermetic SaveStore rooted at a temporary
// directory. There is no cartridge counterpart to any of this: the settings are the port's own, so
// every asserted value comes from the surface's own stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

#include <retropp/engine_config.h>
#include <retropp/save_store.h>

#include "state/settings.h"

namespace {

using kirpich::Settings;

// (1) The clamp brings a scale into the range this build offers, from either side, and leaves a
// value already inside it alone. Every value in the range is swept, not just its ends.
TEST(SettingsValue, ClampBringsAScaleIntoRange) {
    for (int scale = kirpich::kMinWindowScale; scale <= kirpich::kMaxWindowScale; ++scale) {
        EXPECT_EQ(kirpich::clampWindowScale(scale), scale) << "in-range scale " << scale;
    }

    EXPECT_EQ(kirpich::clampWindowScale(0), kirpich::kMinWindowScale);
    EXPECT_EQ(kirpich::clampWindowScale(-3), kirpich::kMinWindowScale);
    EXPECT_EQ(kirpich::clampWindowScale(kirpich::kMaxWindowScale + 1), kirpich::kMaxWindowScale);
    EXPECT_EQ(kirpich::clampWindowScale(255), kirpich::kMaxWindowScale);
}

// (2) The image is the two bytes the contract names, in that order: the fullscreen flag as 0 or 1,
// then the scale as itself.
TEST(SettingsValue, ImageIsTheFlagThenTheScale) {
    EXPECT_EQ(kirpich::kSettingsImageBytes, 2u);

    const auto off = kirpich::encodeSettings(Settings{.fullscreen = false, .windowScale = 3});
    EXPECT_EQ(off[0], 0u);
    EXPECT_EQ(off[1], 3u);

    const auto on = kirpich::encodeSettings(Settings{.fullscreen = true, .windowScale = 6});
    EXPECT_EQ(on[0], 1u);
    EXPECT_EQ(on[1], 6u);
}

// (3) Encode and decode are inverse over every value the screen can produce.
TEST(SettingsValue, CodecRoundTripsEveryReachableValue) {
    for (const bool fullscreen : {false, true}) {
        for (std::uint8_t scale = kirpich::kMinWindowScale; scale <= kirpich::kMaxWindowScale;
             ++scale) {
            const Settings source{.fullscreen = fullscreen, .windowScale = scale};
            const auto     image = kirpich::encodeSettings(source);

            Settings decoded{};
            ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
            EXPECT_TRUE(decoded == source) << "fullscreen " << fullscreen << " scale " << +scale;
        }
    }
}

// (4) A scale the build does not offer is clamped on the way in rather than refused, and the flag
// beside it survives — the whole point of clamping instead of rejecting. Any non-zero first byte
// means fullscreen, not just 1.
TEST(SettingsValue, DecodeClampsTheScaleAndKeepsTheFlag) {
    const std::array<std::uint8_t, 2> tooLarge{1, 200};
    Settings                          decoded{};
    ASSERT_TRUE(kirpich::decodeSettings(tooLarge, decoded));
    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, kirpich::kMaxWindowScale);

    const std::array<std::uint8_t, 2> tooSmall{0, 0};
    ASSERT_TRUE(kirpich::decodeSettings(tooSmall, decoded));
    EXPECT_FALSE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, kirpich::kMinWindowScale);

    const std::array<std::uint8_t, 2> oddFlag{0x5A, 2};
    ASSERT_TRUE(kirpich::decodeSettings(oddFlag, decoded));
    EXPECT_TRUE(decoded.fullscreen);
}

// (5) An image of the wrong length is refused outright, and the settings handed in are left exactly
// as they were — a short read must not half-write a value.
TEST(SettingsValue, WrongLengthImageIsRefusedAndWritesNothing) {
    const Settings before{.fullscreen = true, .windowScale = 5};

    for (const std::size_t length : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
        const std::array<std::uint8_t, 3> bytes{9, 9, 9};
        Settings                          target = before;
        EXPECT_FALSE(kirpich::decodeSettings(std::span<const std::uint8_t>{bytes.data(), length},
                                             target));
        EXPECT_TRUE(target == before) << "length " << length;
    }
}

// (6) The store round trip: absent is an ordinary first run, a written document comes back equal,
// and a damaged one leaves the defaults and stays on disk.
TEST(SettingsValue, StoreRoundTrip) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_store_roundtrip";
    std::filesystem::remove_all(root);

    // Absent document: the defaults stand and the load says so.
    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        EXPECT_FALSE(kirpich::loadSettings(store, loaded));
        EXPECT_TRUE(loaded == Settings{});
    }

    // Written, then read back.
    {
        auto           store = retropp::SaveStore::atPath(root);
        const Settings saved{.fullscreen = true, .windowScale = 2};
        ASSERT_TRUE(kirpich::saveSettings(saved, store));

        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded));
        EXPECT_TRUE(loaded == saved);
    }

    // Damaged document: the load reports it, leaves the defaults, and leaves the file where it is.
    {
        auto store     = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no settings document on disk to corrupt";

        Settings loaded{};
        EXPECT_FALSE(kirpich::loadSettings(store, loaded));
        EXPECT_TRUE(loaded == Settings{});

        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (entry.is_regular_file()) stillPresent = true;
        }
        EXPECT_TRUE(stillPresent);
    }

    std::filesystem::remove_all(root);
}

// (7) The default the screen and the window both start from is the engine's own window scale, so a
// player who never opens the screen gets the window the engine would have opened anyway.
TEST(SettingsValue, DefaultsAreWindowedAtTheEngineScale) {
    const Settings defaults{};
    EXPECT_FALSE(defaults.fullscreen);
    EXPECT_EQ(defaults.windowScale, kirpich::kDefaultWindowScale);
    EXPECT_EQ(defaults.windowScale, retropp::EngineConfig{}.enhancements.windowScale);
}

}  // namespace
