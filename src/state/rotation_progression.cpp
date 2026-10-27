#include "state/rotation_progression.h"

#include <cstddef>
#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// The four shades a record carries, in the order they are written.
std::array<retropp::Rgba8*, 4> shadesOfRamp(render::ShadeRamp& r) noexcept {
    return {&r.darkest, &r.dark, &r.light, &r.lightest};
}
std::array<const retropp::Rgba8*, 4> shadesOfRamp(const render::ShadeRamp& r) noexcept {
    return {&r.darkest, &r.dark, &r.light, &r.lightest};
}

void writeRecord(std::span<std::uint8_t> record, const render::ShadeRamp& shades) {
    std::size_t offset = 0;
    for (const retropp::Rgba8* shade : shadesOfRamp(shades)) {
        record[offset++] = shade->r;
        record[offset++] = shade->g;
        record[offset++] = shade->b;
    }
}

render::ShadeRamp readRecord(std::span<const std::uint8_t> record) {
    render::ShadeRamp shades = render::freshCustomPalette();
    std::size_t       offset = 0;
    for (retropp::Rgba8* shade : shadesOfRamp(shades)) {
        *shade = retropp::Rgba8{
            .r = record[offset], .g = record[offset + 1], .b = record[offset + 2], .a = 255};
        offset += 3;
    }
    return shades;
}

}  // namespace

bool hasChanges(const RotationProgression& progression) noexcept {
    return progression != RotationProgression{};
}

std::array<std::uint8_t, kRotationProgressionImageBytes> encodeRotationProgression(
    const RotationProgression& progression) {
    std::array<std::uint8_t, kRotationProgressionImageBytes> image{};
    const std::span<std::uint8_t>                            bytes(image);

    for (std::size_t level = 0; level < kRotationLevelCount; ++level) {
        writeRecord(bytes.subspan(level * kRotationRecordBytes, kRotationRecordBytes),
                    progression.levels[level]);
    }
    return image;
}

bool decodeRotationProgression(std::span<const std::uint8_t> image,
                               RotationProgression&          progression) {
    if (image.size() != kRotationProgressionImageBytes) return false;

    RotationProgression decoded;
    for (std::size_t level = 0; level < kRotationLevelCount; ++level) {
        decoded.levels[level] =
            readRecord(image.subspan(level * kRotationRecordBytes, kRotationRecordBytes));
    }

    progression = decoded;
    return true;
}

bool saveRotationProgression(const RotationProgression& progression, retropp::SaveStore& store) {
    const auto image = encodeRotationProgression(progression);
    return store.write("rotation", kRotationProgressionSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadRotationProgression(retropp::SaveStore& store, RotationProgression& progression) {
    store.setCurrentVersion(kRotationProgressionSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("rotation");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("rotation save is corrupt, running with the defaults: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the defaults

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeRotationProgression(image, progression)) {
        spdlog::error("rotation save has wrong length {} (expected {}), running with the defaults",
                      doc->payload.size(), kRotationProgressionImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
