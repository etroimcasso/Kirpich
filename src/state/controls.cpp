#include "state/controls.h"

#include <cstddef>
#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// The last controller button the engine names. A stored value past it was not written by a build that
// knew what it meant.
constexpr auto kLastPadButton = retropp::PadButton::RightStickRight;

}  // namespace

std::array<std::uint8_t, kControlsImageBytes> encodeControls(const Controls& controls) {
    std::array<std::uint8_t, kControlsImageBytes> image{};
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        const auto  key    = static_cast<std::uint16_t>(controls.buttons[i].key);
        std::size_t offset = i * kControlsRecordBytes;
        image[offset]      = static_cast<std::uint8_t>(key & 0xFF);
        image[offset + 1]  = static_cast<std::uint8_t>(key >> 8);
        image[offset + 2]  = static_cast<std::uint8_t>(controls.buttons[i].pad);
    }
    return image;
}

bool decodeControls(std::span<const std::uint8_t> image, Controls& controls) {
    if (image.size() != kControlsImageBytes) return false;

    Controls decoded;
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        const std::size_t   offset = i * kControlsRecordBytes;
        const std::uint16_t key =
            static_cast<std::uint16_t>(image[offset] | (image[offset + 1] << 8));
        const std::uint8_t pad = image[offset + 2];

        if (key == SDL_SCANCODE_UNKNOWN || key >= SDL_SCANCODE_COUNT) return false;
        if (static_cast<SDL_Scancode>(key) == kCancelKey) return false;
        if (pad > static_cast<std::uint8_t>(kLastPadButton)) return false;

        decoded.buttons[i] = ButtonBinding{.key = static_cast<SDL_Scancode>(key),
                                           .pad = static_cast<retropp::PadButton>(pad)};
    }

    // No source may stand for two buttons: one press would then be two buttons at once.
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        for (std::size_t j = i + 1; j < kGbButtonCount; ++j) {
            if (decoded.buttons[i].key == decoded.buttons[j].key) return false;
            if (decoded.buttons[i].pad == decoded.buttons[j].pad) return false;
        }
    }

    controls = decoded;
    return true;
}

bool saveControls(const Controls& controls, retropp::SaveStore& store) {
    const auto image = encodeControls(controls);
    return store.write("controls", kControlsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadControls(retropp::SaveStore& store, Controls& controls) {
    store.setCurrentVersion(kControlsSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("controls");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("controls save is corrupt, running with the defaults: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the defaults

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeControls(image, controls)) {
        spdlog::error("controls save is not a usable set ({} bytes), running with the defaults",
                      doc->payload.size());
        return false;
    }
    return true;
}

}  // namespace kirpich
