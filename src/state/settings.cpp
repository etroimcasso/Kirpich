#include "state/settings.h"

#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

std::uint8_t clampWindowScale(int scale) {
    if (scale < kMinWindowScale) return kMinWindowScale;
    if (scale > kMaxWindowScale) return kMaxWindowScale;
    return static_cast<std::uint8_t>(scale);
}

std::array<std::uint8_t, kSettingsImageBytes> encodeSettings(const Settings& settings) {
    return {static_cast<std::uint8_t>(settings.fullscreen ? 1 : 0), settings.windowScale};
}

bool decodeSettings(std::span<const std::uint8_t> image, Settings& settings) {
    if (image.size() != kSettingsImageBytes) return false;

    settings.fullscreen  = image[0] != 0;
    settings.windowScale = clampWindowScale(image[1]);
    return true;
}

bool saveSettings(const Settings& settings, retropp::SaveStore& store) {
    const auto image = encodeSettings(settings);
    return store.write("settings", kSettingsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadSettings(retropp::SaveStore& store, Settings& settings) {
    store.setCurrentVersion(kSettingsSchemaVersion);

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
