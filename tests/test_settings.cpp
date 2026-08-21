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

#include "render/palettes.h"
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

// (2) The image is the bytes the contract names, in that order: the fullscreen flag as 0 or 1, then
// the scale as itself, then the shade ramp as itself.
TEST(SettingsValue, ImageIsTheFlagThenTheScaleThenTheRamp) {
    EXPECT_EQ(kirpich::kSettingsImageBytes, 3u);

    const auto off = kirpich::encodeSettings(
        Settings{.fullscreen = false, .windowScale = 3, .shadeRamp = 0});
    EXPECT_EQ(off[0], 0u);
    EXPECT_EQ(off[1], 3u);
    EXPECT_EQ(off[2], 0u);

    const auto on = kirpich::encodeSettings(
        Settings{.fullscreen = true, .windowScale = 6, .shadeRamp = 2});
    EXPECT_EQ(on[0], 1u);
    EXPECT_EQ(on[1], 6u);
    EXPECT_EQ(on[2], 2u);
}

// (3) Encode and decode are inverse over every value the screen can produce.
TEST(SettingsValue, CodecRoundTripsEveryReachableValue) {
    for (const bool fullscreen : {false, true}) {
        for (std::uint8_t scale = kirpich::kMinWindowScale; scale <= kirpich::kMaxWindowScale;
             ++scale) {
            for (std::uint8_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
                const Settings source{
                    .fullscreen = fullscreen, .windowScale = scale, .shadeRamp = ramp};
                const auto image = kirpich::encodeSettings(source);

                Settings decoded{};
                ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
                EXPECT_TRUE(decoded == source)
                    << "fullscreen " << fullscreen << " scale " << +scale << " ramp " << +ramp;
            }
        }
    }
}

// (4) A scale the build does not offer is clamped on the way in rather than refused, and the flag
// beside it survives — the whole point of clamping instead of rejecting. Any non-zero first byte
// means fullscreen, not just 1.
TEST(SettingsValue, DecodeClampsTheScaleAndKeepsTheFlag) {
    const std::array<std::uint8_t, 3> tooLarge{1, 200, 200};
    Settings                          decoded{};
    ASSERT_TRUE(kirpich::decodeSettings(tooLarge, decoded));
    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, kirpich::kMaxWindowScale);
    EXPECT_EQ(decoded.shadeRamp, kirpich::render::kShadeRampCount - 1)
        << "a ramp the build does not offer clamps rather than naming nothing";

    const std::array<std::uint8_t, 3> tooSmall{0, 0, 0};
    ASSERT_TRUE(kirpich::decodeSettings(tooSmall, decoded));
    EXPECT_FALSE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, kirpich::kMinWindowScale);

    const std::array<std::uint8_t, 3> oddFlag{0x5A, 2, 0};
    ASSERT_TRUE(kirpich::decodeSettings(oddFlag, decoded));
    EXPECT_TRUE(decoded.fullscreen);
}

// (5) An empty image, or one longer than this build writes, is refused outright and leaves the
// settings exactly as they were. Nothing can be said about bytes this build does not understand.
TEST(SettingsValue, EmptyOrOverlongImageIsRefusedAndWritesNothing) {
    const Settings before{.fullscreen = true, .windowScale = 5, .shadeRamp = 1};

    const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
    for (const std::size_t length : {std::size_t{0}, std::size_t{4}}) {
        Settings target = before;
        EXPECT_FALSE(kirpich::decodeSettings(std::span<const std::uint8_t>{bytes.data(), length},
                                             target));
        EXPECT_TRUE(target == before) << "length " << length;
    }
}

// (5b) A shorter image is read as far as it goes, and every value it does not carry keeps the
// default it already held. That is what lets a document written before a setting existed cost the
// player only that setting rather than all of them.
TEST(SettingsValue, ShorterImageLeavesTheValuesItDoesNotCarry) {
    const std::array<std::uint8_t, 3> full{1, 6, 2};

    // Just the flag: the scale and the ramp stay where they were.
    Settings target{.fullscreen = false, .windowScale = 3, .shadeRamp = 1};
    ASSERT_TRUE(kirpich::decodeSettings(std::span<const std::uint8_t>{full.data(), 1}, target));
    EXPECT_TRUE(target.fullscreen);
    EXPECT_EQ(target.windowScale, 3);
    EXPECT_EQ(target.shadeRamp, 1);

    // The flag and the scale: only the ramp stays.
    target = Settings{.fullscreen = false, .windowScale = 3, .shadeRamp = 1};
    ASSERT_TRUE(kirpich::decodeSettings(std::span<const std::uint8_t>{full.data(), 2}, target));
    EXPECT_TRUE(target.fullscreen);
    EXPECT_EQ(target.windowScale, 6);
    EXPECT_EQ(target.shadeRamp, 1);
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
        const Settings saved{.fullscreen = true, .windowScale = 2, .shadeRamp = 2};
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
    EXPECT_EQ(defaults.shadeRamp, kirpich::render::kDefaultShadeRamp);
}

}  // namespace
