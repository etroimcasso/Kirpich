#include "state/custom_palettes.h"

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

void writeRecord(std::span<std::uint8_t> record, bool present, const render::ShadeRamp& shades) {
    record[0]          = present ? 1 : 0;
    std::size_t offset = 1;
    for (const retropp::Rgba8* shade : shadesOfRamp(shades)) {
        record[offset++] = shade->r;
        record[offset++] = shade->g;
        record[offset++] = shade->b;
    }
}

render::ShadeRamp readRecord(std::span<const std::uint8_t> record) {
    render::ShadeRamp shades = render::freshCustomPalette();
    std::size_t       offset = 1;
    for (retropp::Rgba8* shade : shadesOfRamp(shades)) {
        *shade = retropp::Rgba8{
            .r = record[offset], .g = record[offset + 1], .b = record[offset + 2], .a = 255};
        offset += 3;
    }
    return shades;
}

}  // namespace

std::optional<std::uint8_t> savedSlotOf(const CustomPalettes& palettes) noexcept {
    for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
        const CustomPaletteSlot& slot = palettes.slots[i];
        if (slot.present && slot.shades == palettes.working) {
            return static_cast<std::uint8_t>(i + 1);
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> nextPresentSlot(const CustomPalettes& palettes,
                                            std::uint8_t          after) noexcept {
    for (std::size_t number = after + std::size_t{1}; number <= kCustomPaletteSlotCount; ++number) {
        if (palettes.slots[number - 1].present) {
            return static_cast<std::uint8_t>(number);
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> previousPresentSlot(const CustomPalettes& palettes,
                                                std::uint8_t          before) noexcept {
    const std::size_t top = before > kCustomPaletteSlotCount + 1
                                ? kCustomPaletteSlotCount + 1
                                : before;
    for (std::size_t number = top; number > 1;) {
        --number;
        if (palettes.slots[number - 1].present) {
            return static_cast<std::uint8_t>(number);
        }
    }
    return std::nullopt;
}

std::array<std::uint8_t, kCustomPalettesImageBytes> encodeCustomPalettes(
    const CustomPalettes& palettes) {
    std::array<std::uint8_t, kCustomPalettesImageBytes> image{};
    const std::span<std::uint8_t>                       bytes(image);

    writeRecord(bytes.subspan(0, kCustomPaletteRecordBytes), /*present=*/true, palettes.working);
    for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
        writeRecord(bytes.subspan((i + 1) * kCustomPaletteRecordBytes, kCustomPaletteRecordBytes),
                    palettes.slots[i].present, palettes.slots[i].shades);
    }
    return image;
}

bool decodeCustomPalettes(std::span<const std::uint8_t> image, CustomPalettes& palettes) {
    if (image.size() != kCustomPalettesImageBytes) return false;

    CustomPalettes decoded;
    decoded.working = readRecord(image.subspan(0, kCustomPaletteRecordBytes));
    for (std::size_t i = 0; i < kCustomPaletteSlotCount; ++i) {
        const auto record =
            image.subspan((i + 1) * kCustomPaletteRecordBytes, kCustomPaletteRecordBytes);
        decoded.slots[i] = CustomPaletteSlot{.present = record[0] != 0, .shades = readRecord(record)};
    }

    palettes = decoded;
    return true;
}

bool saveCustomPalettes(const CustomPalettes& palettes, retropp::SaveStore& store) {
    const auto image = encodeCustomPalettes(palettes);
    return store.write("palettes", kCustomPalettesSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadCustomPalettes(retropp::SaveStore& store, CustomPalettes& palettes) {
    store.setCurrentVersion(kCustomPalettesSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("palettes");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("palettes save is corrupt, running with the defaults: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the defaults

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeCustomPalettes(image, palettes)) {
        spdlog::error("palettes save has wrong length {} (expected {}), running with the defaults",
                      doc->payload.size(), kCustomPalettesImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
