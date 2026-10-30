#include "state/custom_rotations.h"

#include <cstddef>
#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// The bytes one level takes in a record: red, green and blue for each of the four shades.
inline constexpr std::size_t kLevelBytes = 12;

// The four shades a level carries, in the order they are written.
std::array<retropp::Rgba8*, 4> shadesOfRamp(render::ShadeRamp& r) noexcept {
    return {&r.darkest, &r.dark, &r.light, &r.lightest};
}
std::array<const retropp::Rgba8*, 4> shadesOfRamp(const render::ShadeRamp& r) noexcept {
    return {&r.darkest, &r.dark, &r.light, &r.lightest};
}

void writeRecord(std::span<std::uint8_t> record, bool present,
                 const RotationProgression& progression) {
    record[0]          = present ? 1 : 0;
    std::size_t offset = 1;
    for (const render::ShadeRamp& level : progression.levels) {
        for (const retropp::Rgba8* shade : shadesOfRamp(level)) {
            record[offset++] = shade->r;
            record[offset++] = shade->g;
            record[offset++] = shade->b;
        }
    }
}

RotationProgression readRecord(std::span<const std::uint8_t> record) {
    RotationProgression progression;
    std::size_t         offset = 1;
    for (render::ShadeRamp& level : progression.levels) {
        for (retropp::Rgba8* shade : shadesOfRamp(level)) {
            *shade = retropp::Rgba8{
                .r = record[offset], .g = record[offset + 1], .b = record[offset + 2], .a = 255};
            offset += 3;
        }
    }
    return progression;
}

static_assert(kCustomRotationRecordBytes == 1 + kRotationLevelCount * kLevelBytes);

}  // namespace

std::optional<std::uint8_t> savedSlotOf(const CustomRotations& rotations) noexcept {
    for (std::size_t i = 0; i < kCustomRotationSlotCount; ++i) {
        const CustomRotationSlot& slot = rotations.slots[i];
        if (slot.present && slot.progression == rotations.working) {
            return static_cast<std::uint8_t>(i + 1);
        }
    }
    return std::nullopt;
}

bool hasUnsavedChanges(const CustomRotations& rotations) noexcept {
    return !savedSlotOf(rotations) && rotations.working != RotationProgression{};
}

std::optional<std::uint8_t> nextPresentSlot(const CustomRotations& rotations,
                                            std::uint8_t           after) noexcept {
    for (std::size_t number = after + std::size_t{1}; number <= kCustomRotationSlotCount; ++number) {
        if (rotations.slots[number - 1].present) {
            return static_cast<std::uint8_t>(number);
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> previousPresentSlot(const CustomRotations& rotations,
                                                std::uint8_t           before) noexcept {
    const std::size_t top = before > kCustomRotationSlotCount + 1
                                ? kCustomRotationSlotCount + 1
                                : before;
    for (std::size_t number = top; number > 1;) {
        --number;
        if (rotations.slots[number - 1].present) {
            return static_cast<std::uint8_t>(number);
        }
    }
    return std::nullopt;
}

std::array<std::uint8_t, kCustomRotationsImageBytes> encodeCustomRotations(
    const CustomRotations& rotations) {
    std::array<std::uint8_t, kCustomRotationsImageBytes> image{};
    const std::span<std::uint8_t>                        bytes(image);

    writeRecord(bytes.subspan(0, kCustomRotationRecordBytes), /*present=*/true, rotations.working);
    for (std::size_t i = 0; i < kCustomRotationSlotCount; ++i) {
        writeRecord(bytes.subspan((i + 1) * kCustomRotationRecordBytes, kCustomRotationRecordBytes),
                    rotations.slots[i].present, rotations.slots[i].progression);
    }
    return image;
}

bool decodeCustomRotations(std::span<const std::uint8_t> image, CustomRotations& rotations) {
    if (image.size() != kCustomRotationsImageBytes) return false;

    CustomRotations decoded;
    decoded.working = readRecord(image.subspan(0, kCustomRotationRecordBytes));
    for (std::size_t i = 0; i < kCustomRotationSlotCount; ++i) {
        const auto record =
            image.subspan((i + 1) * kCustomRotationRecordBytes, kCustomRotationRecordBytes);
        decoded.slots[i] =
            CustomRotationSlot{.present = record[0] != 0, .progression = readRecord(record)};
    }

    rotations = decoded;
    return true;
}

bool saveCustomRotations(const CustomRotations& rotations, retropp::SaveStore& store) {
    const auto image = encodeCustomRotations(rotations);
    return store.write("rotations", kCustomRotationsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadCustomRotations(retropp::SaveStore& store, CustomRotations& rotations) {
    store.setCurrentVersion(kCustomRotationsSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("rotations");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("rotations save is corrupt, running with the defaults: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the defaults

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeCustomRotations(image, rotations)) {
        spdlog::error("rotations save has wrong length {} (expected {}), running with the defaults",
                      doc->payload.size(), kCustomRotationsImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
