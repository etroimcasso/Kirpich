#include "state/settings.h"

#include <cstddef>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

#include "render/palettes.h"  // clampShadeRamp, kCustomRamp

namespace kirpich {

std::uint8_t clampWindowScale(int scale) {
    if (scale < kMinWindowScale) return kMinWindowScale;
    if (scale > kMaxWindowScale) return kMaxWindowScale;
    return static_cast<std::uint8_t>(scale);
}

std::uint8_t paletteIndexOf(const Settings& settings) noexcept {
    return settings.customPalette ? render::kCustomRamp : render::clampShadeRamp(settings.shadeRamp);
}

std::array<std::uint8_t, kSettingsImageBytes> encodeSettings(const Settings& settings) {
    return {static_cast<std::uint8_t>(settings.fullscreen ? 1 : 0), settings.windowScale,
            settings.shadeRamp, static_cast<std::uint8_t>(settings.ghostPiece ? 1 : 0),
            static_cast<std::uint8_t>(settings.newModes ? 1 : 0),
            static_cast<std::uint8_t>(settings.fixAudio ? 1 : 0),
            static_cast<std::uint8_t>(settings.showStats ? 1 : 0),
            static_cast<std::uint8_t>(settings.customPalette ? 1 : 0),
            static_cast<std::uint8_t>(settings.paletteRotation)};
}

bool decodeSettings(std::span<const std::uint8_t> image, Settings& settings) {
    if (image.empty() || image.size() > kSettingsImageBytes) return false;

    // Read as far as the image goes. A document written before a setting existed simply stops short
    // of it, and that setting keeps the default it already holds.
    settings.fullscreen = image[0] != 0;
    if (image.size() > 1) settings.windowScale = clampWindowScale(image[1]);
    if (image.size() > 2) settings.shadeRamp = render::clampShadeRamp(image[2]);
    if (image.size() > 3) settings.ghostPiece = image[3] != 0;
    if (image.size() > 4) settings.newModes = image[4] != 0;
    if (image.size() > 5) settings.fixAudio = image[5] != 0;
    if (image.size() > 6) settings.showStats = image[6] != 0;
    if (image.size() > 7) settings.customPalette = image[7] != 0;
    if (image.size() > 8) settings.paletteRotation = paletteRotationOf(image[8]);
    return true;
}

std::vector<std::byte> migrateSettingsV1ToV2(std::vector<std::byte> payload) {
    // One byte appended, not a resize to the version 2 length: the step's whole content is that
    // version 2 carries one more value than version 1. A payload of any other length is not this
    // step's to correct - decodeSettings refuses a wrong length downstream.
    payload.push_back(std::byte{0});
    return payload;
}

std::vector<std::byte> migrateSettingsV2ToV3(std::vector<std::byte> payload) {
    // The same shape as the step before it: version 3 carries the new-modes flag, and a document
    // written before it existed was written by a player who had never been offered the choice.
    payload.push_back(std::byte{0});
    return payload;
}

std::vector<std::byte> migrateSettingsV3ToV4(std::vector<std::byte> payload) {
    // And again: version 4 carries the audio-fix flag, off for a document that predates the fix
    // being offered - the quirk is the cartridge's own behavior, so off is what such a player had.
    payload.push_back(std::byte{0});
    return payload;
}

std::vector<std::byte> migrateSettingsV4ToV5(std::vector<std::byte> payload) {
    // And again: version 5 carries the show-stats flag, off for a document written before the
    // statistics were offered. The tables themselves were being filled either way, so turning it on
    // later shows a history rather than starting one.
    payload.push_back(std::byte{0});
    return payload;
}

std::vector<std::byte> migrateSettingsV5ToV6(std::vector<std::byte> payload) {
    // And again: version 6 carries the custom-palette flag, off for a document written before a
    // custom palette could be in use - such a player was drawing through a built-in ramp, and the
    // ramp byte they chose is still there to draw through.
    payload.push_back(std::byte{0});
    return payload;
}

std::vector<std::byte> migrateSettingsV6ToV7(std::vector<std::byte> payload) {
    // And again: version 7 carries the palette-rotation mode, OFF for a document written before a
    // round's palette could follow its level - such a player's rounds were drawn through the palette
    // they chose, and that choice is still there.
    payload.push_back(std::byte{static_cast<std::uint8_t>(PaletteRotation::OFF)});
    return payload;
}

bool saveSettings(const Settings& settings, retropp::SaveStore& store) {
    const auto image = encodeSettings(settings);
    return store.write("settings", kSettingsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadSettings(retropp::SaveStore& store, Settings& settings) {
    // Both of these are the store's, not the document's, so they are declared here rather than once
    // at startup: the same store also carries the top scores at their own version, and whichever
    // loader is about to read has to be the one that last said which version it means.
    store.setCurrentVersion(kSettingsSchemaVersion);
    store.registerMigration(1, migrateSettingsV1ToV2);
    store.registerMigration(2, migrateSettingsV2ToV3);
    store.registerMigration(3, migrateSettingsV3ToV4);
    store.registerMigration(4, migrateSettingsV4ToV5);
    store.registerMigration(5, migrateSettingsV5ToV6);
    store.registerMigration(6, migrateSettingsV6ToV7);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("settings");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("settings save is corrupt, running with the defaults: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the defaults

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeSettings(image, settings)) {
        spdlog::error("settings save has wrong length {} (expected {}), running with the defaults",
                      doc->payload.size(), kSettingsImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
