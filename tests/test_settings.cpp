// The player's display settings and the document they persist in — behavioral tests over
// src/state/settings.h.
//
// Device-free except the store cases, which run through a hermetic SaveStore rooted at a temporary
// directory. There is no cartridge counterpart to any of this: the settings are the port's own, so
// every asserted value comes from the surface's own stated contract rather than from tetris.asm.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>
#include <vector>

#include <retropp/engine_config.h>
#include <retropp/save_store.h>

#include "render/palettes.h"
#include "state/high_score_persistence.h"
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
// the scale as itself, then the shade ramp as itself, then the ghost-piece flag, then the new-modes
// flag, then the audio-fix flag, then the show-stats flag, then the custom-palette flag, then the
// palette-rotation mode as its own value.
TEST(SettingsValue, ImageIsTheFlagThenTheScaleThenTheRampThenTheFiveSwitches) {
    EXPECT_EQ(kirpich::kSettingsImageBytes, 9u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV1, 3u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV2, 4u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV3, 5u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV4, 6u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV5, 7u);
    EXPECT_EQ(kirpich::kSettingsImageBytesV6, 8u);
    EXPECT_EQ(kirpich::kSettingsSchemaVersion, 7u)
        << "the ninth byte is a new format, so it is a new version rather than a longer version 6";

    // Each version's length is one more than the one before it, which is what makes every migration a
    // single appended byte.
    EXPECT_EQ(kirpich::kSettingsImageBytesV2, kirpich::kSettingsImageBytesV1 + 1);
    EXPECT_EQ(kirpich::kSettingsImageBytesV3, kirpich::kSettingsImageBytesV2 + 1);
    EXPECT_EQ(kirpich::kSettingsImageBytesV4, kirpich::kSettingsImageBytesV3 + 1);
    EXPECT_EQ(kirpich::kSettingsImageBytesV5, kirpich::kSettingsImageBytesV4 + 1);
    EXPECT_EQ(kirpich::kSettingsImageBytesV6, kirpich::kSettingsImageBytesV5 + 1);
    EXPECT_EQ(kirpich::kSettingsImageBytes, kirpich::kSettingsImageBytesV6 + 1);

    const auto off = kirpich::encodeSettings(Settings{.fullscreen = false,
                                                      .windowScale = 3,
                                                      .shadeRamp = 0,
                                                      .ghostPiece = false,
                                                      .newModes = false,
                                                      .fixAudio = false,
                                                      .showStats = false,
                                                      .customPalette = false,
                                                      .paletteRotation = kirpich::PaletteRotation::OFF});
    EXPECT_EQ(off[0], 0u);
    EXPECT_EQ(off[1], 3u);
    EXPECT_EQ(off[2], 0u);
    EXPECT_EQ(off[3], 0u);
    EXPECT_EQ(off[4], 0u);
    EXPECT_EQ(off[5], 0u);
    EXPECT_EQ(off[6], 0u);
    EXPECT_EQ(off[7], 0u);
    EXPECT_EQ(off[8], 0u);

    const auto on = kirpich::encodeSettings(Settings{.fullscreen = true,
                                                     .windowScale = 6,
                                                     .shadeRamp = 2,
                                                     .ghostPiece = true,
                                                     .newModes = true,
                                                     .fixAudio = true,
                                                     .showStats = true,
                                                     .customPalette = true,
                                                     .paletteRotation = kirpich::PaletteRotation::CUSTOM});
    EXPECT_EQ(on[0], 1u);
    EXPECT_EQ(on[1], 6u);
    EXPECT_EQ(on[2], 2u);
    EXPECT_EQ(on[3], 1u);
    EXPECT_EQ(on[4], 1u);
    EXPECT_EQ(on[5], 1u);
    EXPECT_EQ(on[6], 1u);
    EXPECT_EQ(on[7], 1u);
    EXPECT_EQ(on[8], 4u);
}

// (3) Encode and decode are inverse over every value the screen can produce.
TEST(SettingsValue, CodecRoundTripsEveryReachableValue) {
    for (const bool fullscreen : {false, true}) {
        for (std::uint8_t scale = kirpich::kMinWindowScale; scale <= kirpich::kMaxWindowScale;
             ++scale) {
            for (std::uint8_t ramp = 0; ramp < kirpich::render::kShadeRampCount; ++ramp) {
                for (const bool ghost : {false, true}) {
                    for (const bool fix : {false, true}) {
                        for (const bool custom : {false, true}) {
                            for (std::size_t mode = 0; mode < kirpich::kPaletteRotationCount;
                                 ++mode) {
                                const Settings source{
                                    .fullscreen      = fullscreen,
                                    .windowScale     = scale,
                                    .shadeRamp       = ramp,
                                    .ghostPiece      = ghost,
                                    .fixAudio        = fix,
                                    .customPalette   = custom,
                                    .paletteRotation = static_cast<kirpich::PaletteRotation>(mode)};
                                const auto image = kirpich::encodeSettings(source);

                                Settings decoded{};
                                ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
                                EXPECT_TRUE(decoded == source)
                                    << "fullscreen " << fullscreen << " scale " << +scale
                                    << " ramp " << +ramp << " ghost " << ghost << " fix " << fix
                                    << " custom " << custom << " rotation " << mode;
                            }
                        }
                    }
                }
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

    const std::array<std::uint8_t, kirpich::kSettingsImageBytes + 1> bytes{1, 2, 3, 1, 1, 1, 9, 1, 1};
    for (const std::size_t length : {std::size_t{0}, kirpich::kSettingsImageBytes + 1}) {
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

// (7) The migration step, on its own: version 2 says one more thing than version 1 did, so the step
// appends exactly one byte and leaves every byte version 1 wrote where it was. Off is what the
// appended byte says, because a document written before the setting existed cannot say otherwise.
TEST(SettingsValue, MigrationsEachAppendOneFlagOff) {
    const std::vector<std::byte> v1{std::byte{1}, std::byte{6}, std::byte{2}};
    const std::vector<std::byte> v2 = kirpich::migrateSettingsV1ToV2(v1);

    ASSERT_EQ(v2.size(), kirpich::kSettingsImageBytesV2);
    EXPECT_EQ(v1.size(), kirpich::kSettingsImageBytesV1);
    EXPECT_EQ(v2[0], v1[0]);
    EXPECT_EQ(v2[1], v1[1]);
    EXPECT_EQ(v2[2], v1[2]);
    EXPECT_EQ(v2[3], std::byte{0});

    // The next steps have the same shape, and the six compose: a version 1 document reaches
    // version 7 through all of them, with everything it did carry surviving and everything it
    // predates coming up off.
    const std::vector<std::byte> v3 = kirpich::migrateSettingsV2ToV3(v2);

    ASSERT_EQ(v3.size(), kirpich::kSettingsImageBytesV3);
    EXPECT_EQ(v3[0], v1[0]);
    EXPECT_EQ(v3[1], v1[1]);
    EXPECT_EQ(v3[2], v1[2]);
    EXPECT_EQ(v3[3], std::byte{0});
    EXPECT_EQ(v3[4], std::byte{0});

    const std::vector<std::byte> v4 = kirpich::migrateSettingsV3ToV4(v3);

    ASSERT_EQ(v4.size(), kirpich::kSettingsImageBytesV4);
    EXPECT_EQ(v4[0], v1[0]);
    EXPECT_EQ(v4[1], v1[1]);
    EXPECT_EQ(v4[2], v1[2]);
    EXPECT_EQ(v4[3], std::byte{0});
    EXPECT_EQ(v4[4], std::byte{0});
    EXPECT_EQ(v4[5], std::byte{0});

    const std::vector<std::byte> v5 = kirpich::migrateSettingsV4ToV5(v4);

    ASSERT_EQ(v5.size(), kirpich::kSettingsImageBytesV5);
    EXPECT_EQ(v5[0], v1[0]);
    EXPECT_EQ(v5[1], v1[1]);
    EXPECT_EQ(v5[2], v1[2]);
    EXPECT_EQ(v5[3], std::byte{0});
    EXPECT_EQ(v5[4], std::byte{0});
    EXPECT_EQ(v5[5], std::byte{0});
    EXPECT_EQ(v5[6], std::byte{0});

    const std::vector<std::byte> v6 = kirpich::migrateSettingsV5ToV6(v5);

    ASSERT_EQ(v6.size(), kirpich::kSettingsImageBytesV6);
    EXPECT_EQ(v6[0], v1[0]);
    EXPECT_EQ(v6[1], v1[1]);
    EXPECT_EQ(v6[2], v1[2]);
    EXPECT_EQ(v6[3], std::byte{0});
    EXPECT_EQ(v6[4], std::byte{0});
    EXPECT_EQ(v6[5], std::byte{0});
    EXPECT_EQ(v6[6], std::byte{0});
    EXPECT_EQ(v6[7], std::byte{0});

    const std::vector<std::byte> v7 = kirpich::migrateSettingsV6ToV7(v6);

    ASSERT_EQ(v7.size(), kirpich::kSettingsImageBytes);
    EXPECT_EQ(v7[0], v1[0]);
    EXPECT_EQ(v7[1], v1[1]);
    EXPECT_EQ(v7[2], v1[2]);
    for (std::size_t i = 3; i < kirpich::kSettingsImageBytes; ++i) {
        EXPECT_EQ(v7[i], std::byte{0}) << "byte " << i;
    }
}

// (7b) The newest step on its own, against a version 5 image that carries every switch ON. It has to
// append the custom-palette byte off and touch nothing else: a step that resized the payload, or
// wrote its byte over one of the seven already there, would cost the player a setting they had
// chosen - and the ramp byte in particular, which is what the player is drawn through while the
// new flag is off.
TEST(SettingsValue, TheCustomPaletteMigrationAppendsOffAndLeavesTheRestAlone) {
    const std::vector<std::byte> v5{std::byte{1}, std::byte{6}, std::byte{2}, std::byte{1},
                                    std::byte{1}, std::byte{1}, std::byte{1}};
    ASSERT_EQ(v5.size(), kirpich::kSettingsImageBytesV5);

    const std::vector<std::byte> v6 = kirpich::migrateSettingsV5ToV6(v5);

    ASSERT_EQ(v6.size(), kirpich::kSettingsImageBytesV6);
    for (std::size_t i = 0; i < v5.size(); ++i) {
        EXPECT_EQ(v6[i], v5[i]) << "byte " << i << " belonged to version 5 and must survive";
    }
    EXPECT_EQ(v6[kirpich::kSettingsImageBytesV5], std::byte{0})
        << "a document written before a custom palette could be in use was drawn through a ramp";

    // And what that image decodes to: seven values as the player left them, the new one off, and
    // the frame still drawn through the ramp the player chose.
    Settings decoded{};
    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(v6.data()), v6.size());
    ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, 6);
    EXPECT_EQ(decoded.shadeRamp, 2);
    EXPECT_TRUE(decoded.ghostPiece);
    EXPECT_TRUE(decoded.newModes);
    EXPECT_TRUE(decoded.fixAudio);
    EXPECT_TRUE(decoded.showStats);
    EXPECT_FALSE(decoded.customPalette);
    EXPECT_EQ(kirpich::paletteIndexOf(decoded), 2);
}

// (7c) A settings document written by the shipped 0.9.5 build - version 4, six bytes - loads through
// the shipped loader, with its six values intact and the statistics off.
//
// It is asked what the payload arrived AS, not only what it decoded to. The short-image fallback
// reaches the same decoded values from a six-byte version 5 document, so the decoded values alone
// cannot tell a migration from no migration at all.
TEST(SettingsValue, AVersionFourDocumentFromZeroNinePointFiveMigratesForward) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_v4_migration";
    std::filesystem::remove_all(root);

    {
        auto store = retropp::SaveStore::atPath(root);
        // Windowed, scale 4, ramp 17, ghost on, new modes on, the audio fix on - and no seventh byte,
        // because there was no seventh setting.
        const std::array<std::uint8_t, kirpich::kSettingsImageBytesV4> v4{0, 4, 17, 1, 1, 1};
        ASSERT_TRUE(store.write("settings", 4, std::as_bytes(std::span<const std::uint8_t>(v4))));
    }

    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded))
            << "a document from the last release must still load";
        EXPECT_FALSE(loaded.fullscreen);
        EXPECT_EQ(loaded.windowScale, 4);
        EXPECT_EQ(loaded.shadeRamp, 17);
        EXPECT_TRUE(loaded.ghostPiece);
        EXPECT_TRUE(loaded.newModes);
        EXPECT_TRUE(loaded.fixAudio);
        EXPECT_FALSE(loaded.showStats) << "a setting the document predates takes its default";
        EXPECT_FALSE(loaded.customPalette) << "a setting the document predates takes its default";

        const auto migrated = store.read("settings");
        ASSERT_TRUE(migrated.has_value());
        EXPECT_EQ(migrated->schemaVersion, kirpich::kSettingsSchemaVersion);
        EXPECT_EQ(migrated->payload.size(), kirpich::kSettingsImageBytes)
            << "a version 4 document must reach the decoder at the current length";
    }

    std::filesystem::remove_all(root);
}

// (7d) A settings document written by the shipped 0.10 and 0.10.1 builds - version 5, seven bytes -
// loads through the shipped loader with its seven values intact, no custom palette in use, and the
// payload arriving at version 6's length: migrated, not read short.
TEST(SettingsValue, AVersionFiveDocumentFromZeroTenMigratesForward) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_v5_migration";
    std::filesystem::remove_all(root);

    {
        auto store = retropp::SaveStore::atPath(root);
        // Fullscreen, scale 3, ramp 66, ghost on, new modes off, the audio fix on, the statistics on -
        // and no eighth byte, because there was no eighth setting.
        const std::array<std::uint8_t, kirpich::kSettingsImageBytesV5> v5{1, 3, 66, 1, 0, 1, 1};
        ASSERT_TRUE(store.write("settings", 5, std::as_bytes(std::span<const std::uint8_t>(v5))));
    }

    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded))
            << "a document from the last release must still load";
        EXPECT_TRUE(loaded.fullscreen);
        EXPECT_EQ(loaded.windowScale, 3);
        EXPECT_EQ(loaded.shadeRamp, 66);
        EXPECT_TRUE(loaded.ghostPiece);
        EXPECT_FALSE(loaded.newModes);
        EXPECT_TRUE(loaded.fixAudio);
        EXPECT_TRUE(loaded.showStats);
        EXPECT_FALSE(loaded.customPalette) << "a setting the document predates takes its default";
        EXPECT_EQ(kirpich::paletteIndexOf(loaded), 66) << "still drawn through the chosen ramp";

        const auto migrated = store.read("settings");
        ASSERT_TRUE(migrated.has_value());
        EXPECT_EQ(migrated->schemaVersion, kirpich::kSettingsSchemaVersion);
        EXPECT_EQ(migrated->payload.size(), kirpich::kSettingsImageBytes)
            << "a version 5 document must reach the decoder at the current length";
        EXPECT_EQ(loaded.paletteRotation, kirpich::PaletteRotation::OFF)
            << "a setting the document predates takes its default";
    }

    std::filesystem::remove_all(root);
}

// (8) A settings document written by a released build - Kirpich 0.9.0 and 0.9.1 wrote version 1,
// three bytes - is migrated on the way in rather than read short. The three values it carries survive
// exactly, and the one it predates comes up off.
//
// This is written as the old build wrote it: a version-1 document with a version-1 payload, straight
// through the store. Reading it back through the shipped loader is the whole assertion.
TEST(SettingsValue, AVersionOneDocumentFromAReleasedBuildMigratesForward) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_v1_migration";
    std::filesystem::remove_all(root);

    {
        auto store = retropp::SaveStore::atPath(root);
        // Fullscreen on, scale 2, ramp 5 - and no fourth byte, because there was no fourth setting.
        const std::array<std::uint8_t, kirpich::kSettingsImageBytesV1> v1{1, 2, 5};
        ASSERT_TRUE(store.write("settings", 1,
                                std::as_bytes(std::span<const std::uint8_t>(v1))));
    }

    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded))
            << "a document from the last release must still load";
        EXPECT_TRUE(loaded.fullscreen);
        EXPECT_EQ(loaded.windowScale, 2);
        EXPECT_EQ(loaded.shadeRamp, 5);
        EXPECT_FALSE(loaded.ghostPiece) << "a setting the document predates takes its default";

        // …and it got there by being migrated, not by the short-image fallback happening to leave
        // the same answer. The two are indistinguishable from the decoded values alone - both end
        // with the ghost off - so the store is asked what the payload arrived as. loadSettings has
        // already declared the version and registered the step on this store, so this read walks the
        // same chain the load did.
        const auto migrated = store.read("settings");
        ASSERT_TRUE(migrated.has_value());
        EXPECT_EQ(migrated->schemaVersion, kirpich::kSettingsSchemaVersion);
        EXPECT_EQ(migrated->payload.size(), kirpich::kSettingsImageBytes)
            << "a version 1 document must reach the decoder at the current length";
    }

    // Saved again, it is written at the current version and reads back without migrating.
    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded));
        loaded.ghostPiece = true;
        ASSERT_TRUE(kirpich::saveSettings(loaded, store));

        const auto doc = store.read("settings");
        ASSERT_TRUE(doc.has_value());
        EXPECT_EQ(doc->schemaVersion, kirpich::kSettingsSchemaVersion);
        EXPECT_EQ(doc->payload.size(), kirpich::kSettingsImageBytes);
    }

    std::filesystem::remove_all(root);
}

// (9) The settings and the top scores share one store, and a store carries ONE current version and
// ONE migration chain for every document in it. So a loader that declares version 2 and registers a
// 1 -> 2 step has changed the terms every other document in that store is read under - and the top
// scores are still at version 1, on the same disk, belonging to the same player.
//
// Each loader declaring its own version immediately before its own read is what keeps them apart.
// This asserts it in the order a launch performs it, because that order is the one that would lose a
// player's scores: settings first (main.cpp), then the boot's read of the tables.
TEST(SettingsValue, LoadingSettingsDoesNotDisturbTheTopScoresInTheSameStore) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_shares_a_store";
    std::filesystem::remove_all(root);

    kirpich::HighScoreState written{};
    written.typeA[0][0].score = 12345;
    using C                   = kirpich::CharTile;
    written.typeA[0][0].name  = {C::LETTER_A, C::LETTER_B, C::LETTER_C,
                                 C::SPACE,    C::SPACE,    C::SPACE};

    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveTopScores(written, store));
        ASSERT_TRUE(kirpich::saveSettings(Settings{.shadeRamp = 3}, store));
    }

    {
        auto store = retropp::SaveStore::atPath(root);

        Settings settings{};
        ASSERT_TRUE(kirpich::loadSettings(store, settings));
        EXPECT_EQ(settings.shadeRamp, 3);

        // Now the store is set to the settings' version with the settings' migration registered.
        // The top scores must still read as themselves.
        kirpich::HighScoreState scores{};
        ASSERT_TRUE(kirpich::loadTopScores(store, scores))
            << "the settings' schema version must not follow the top scores into their own read";
        EXPECT_EQ(scores.typeA[0][0].score, 12345u);
        EXPECT_TRUE(scores == written);
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
    EXPECT_FALSE(defaults.customPalette);
}

// (10) The palette index the frame draws through: the custom set while the flag is set, whatever the
// ramp byte says underneath, and the clamped ramp otherwise - swept over every ramp and past the
// table, since a stored byte can hold anything.
TEST(SettingsValue, PaletteIndexIsTheCustomSetOrTheClampedRamp) {
    for (int ramp = 0; ramp <= 255; ++ramp) {
        const auto byte = static_cast<std::uint8_t>(ramp);
        EXPECT_EQ(kirpich::paletteIndexOf(Settings{.shadeRamp = byte, .customPalette = false}),
                  kirpich::render::clampShadeRamp(ramp))
            << "ramp " << ramp;
        EXPECT_EQ(kirpich::paletteIndexOf(Settings{.shadeRamp = byte, .customPalette = true}),
                  kirpich::render::kCustomRamp)
            << "ramp " << ramp;
    }
    EXPECT_EQ(kirpich::render::kCustomRamp, kirpich::render::kShadeRampCount)
        << "the custom set is the one index past the table";
    EXPECT_EQ(kirpich::render::kPaletteChoiceCount, kirpich::render::kShadeRampCount + 2)
        << "the custom set and the rotation set are the two indices past the table";
}

// (11) The newest step on its own, against a version 6 image that carries every switch ON and the
// custom palette in use. It appends the rotation mode off and touches nothing else, and the image it
// makes decodes with the frame still drawn through the custom set.
TEST(SettingsValue, ThePaletteRotationMigrationAppendsOffAndLeavesTheRestAlone) {
    const std::vector<std::byte> v6{std::byte{1}, std::byte{6}, std::byte{2}, std::byte{1},
                                    std::byte{1}, std::byte{1}, std::byte{1}, std::byte{1}};
    ASSERT_EQ(v6.size(), kirpich::kSettingsImageBytesV6);

    const std::vector<std::byte> v7 = kirpich::migrateSettingsV6ToV7(v6);

    ASSERT_EQ(v7.size(), kirpich::kSettingsImageBytes);
    for (std::size_t i = 0; i < v6.size(); ++i) {
        EXPECT_EQ(v7[i], v6[i]) << "byte " << i << " belonged to version 6 and must survive";
    }
    EXPECT_EQ(v7[kirpich::kSettingsImageBytesV6], std::byte{0})
        << "a document written before the palette could follow the level kept one palette";

    Settings decoded{};
    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(v7.data()), v7.size());
    ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_EQ(decoded.windowScale, 6);
    EXPECT_EQ(decoded.shadeRamp, 2);
    EXPECT_TRUE(decoded.ghostPiece);
    EXPECT_TRUE(decoded.newModes);
    EXPECT_TRUE(decoded.fixAudio);
    EXPECT_TRUE(decoded.showStats);
    EXPECT_TRUE(decoded.customPalette);
    EXPECT_EQ(decoded.paletteRotation, kirpich::PaletteRotation::OFF);
    EXPECT_EQ(kirpich::paletteIndexOf(decoded), kirpich::render::kCustomRamp);
}

// (12) A settings document written by the shipped 0.10.2 build - version 6, eight bytes - loads
// through the shipped loader with its eight values intact, the palette not following the level, and
// the payload arriving at the current length: migrated, not read short.
TEST(SettingsValue, AVersionSixDocumentFromZeroTenPointTwoMigratesForward) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_settings_v6_migration";
    std::filesystem::remove_all(root);

    {
        auto store = retropp::SaveStore::atPath(root);
        // Windowed, scale 5, ramp 41, ghost off, new modes on, the audio fix off, the statistics on,
        // the custom palette in use - and no ninth byte, because there was no ninth setting.
        const std::array<std::uint8_t, kirpich::kSettingsImageBytesV6> v6{0, 5, 41, 0, 1, 0, 1, 1};
        ASSERT_TRUE(store.write("settings", 6, std::as_bytes(std::span<const std::uint8_t>(v6))));
    }

    {
        auto     store = retropp::SaveStore::atPath(root);
        Settings loaded{};
        ASSERT_TRUE(kirpich::loadSettings(store, loaded))
            << "a document from the last release must still load";
        EXPECT_FALSE(loaded.fullscreen);
        EXPECT_EQ(loaded.windowScale, 5);
        EXPECT_EQ(loaded.shadeRamp, 41);
        EXPECT_FALSE(loaded.ghostPiece);
        EXPECT_TRUE(loaded.newModes);
        EXPECT_FALSE(loaded.fixAudio);
        EXPECT_TRUE(loaded.showStats);
        EXPECT_TRUE(loaded.customPalette);
        EXPECT_EQ(loaded.paletteRotation, kirpich::PaletteRotation::OFF)
            << "a setting the document predates takes its default";

        const auto migrated = store.read("settings");
        ASSERT_TRUE(migrated.has_value());
        EXPECT_EQ(migrated->schemaVersion, kirpich::kSettingsSchemaVersion);
        EXPECT_EQ(migrated->payload.size(), kirpich::kSettingsImageBytes)
            << "a version 6 document must reach the decoder at the current length";
    }

    std::filesystem::remove_all(root);
}

// (13) A stored mode byte names a mode only when this build has one of that value; anything past the
// last reads as off, both on its own and as the ninth byte of an image.
TEST(SettingsValue, AModeByteThisBuildDoesNotNameReadsAsOff) {
    using kirpich::PaletteRotation;
    EXPECT_EQ(kirpich::paletteRotationOf(0), PaletteRotation::OFF);
    EXPECT_EQ(kirpich::paletteRotationOf(1), PaletteRotation::HOTTER);
    EXPECT_EQ(kirpich::paletteRotationOf(2), PaletteRotation::COLDER);
    EXPECT_EQ(kirpich::paletteRotationOf(3), PaletteRotation::RAINBOW);
    EXPECT_EQ(kirpich::paletteRotationOf(4), PaletteRotation::CUSTOM);
    EXPECT_EQ(kirpich::paletteRotationOf(5), PaletteRotation::OFF);
    EXPECT_EQ(kirpich::paletteRotationOf(255), PaletteRotation::OFF);

    for (const auto& [byte, mode] : {std::pair{std::uint8_t{3}, PaletteRotation::RAINBOW},
                                     std::pair{std::uint8_t{5}, PaletteRotation::OFF},
                                     std::pair{std::uint8_t{255}, PaletteRotation::OFF}}) {
        const std::array<std::uint8_t, kirpich::kSettingsImageBytes> image{0, 4, 0, 0, 0,
                                                                           0, 0, 0, byte};
        Settings decoded{.paletteRotation = PaletteRotation::COLDER};
        ASSERT_TRUE(kirpich::decodeSettings(image, decoded));
        EXPECT_EQ(decoded.paletteRotation, mode) << "byte " << +byte;
    }
}

}  // namespace
