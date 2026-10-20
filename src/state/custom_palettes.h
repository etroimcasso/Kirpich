#pragma once

// The player's own palettes: ninety-nine slots they can fill, and the working palette - the one being
// edited, which is also the one the game draws through while a custom palette is in use. The two
// calls at the bottom keep them across launches.
//
// Like the settings and the controls, these belong to the player rather than to the machine's state
// image: they survive the reset chord and outlive a launch, so they live outside GameContext and reach
// the screens through their wiring. They persist in their own save document beside the settings and
// under the same identity.
//
// A slot is EMPTY until a palette is saved into it, and empty again once cleared; an empty slot is not
// offered on the Palette screen's selector. A fresh custom palette - what an empty slot loads and what
// the working palette starts as - is the default grayscale the game ships in (freshCustomPalette).
//
// Whether the working palette is SAVED is never stored. It is a comparison: the working palette is
// saved when some present slot holds exactly its colors, and that slot's number is what the Palette
// screen shows after the C. Nothing can therefore drift from the colors beside it.
//
// Slot numbers are one-based at every surface below, as they are on screen: C01 is the first slot.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <retropp/save_store.h>

#include "render/palettes.h"  // ShadeRamp, freshCustomPalette

namespace kirpich {

inline constexpr std::size_t kCustomPaletteSlotCount = 99;

// One slot: whether anything has been saved into it, and the palette it holds. A custom palette is a
// ShadeRamp whose mayBottomOutAtBlack is always off - that flag is the built-in table's note about how
// a ramp was authored, so a slot compares equal to the working palette color for color.
struct CustomPaletteSlot {
    bool              present = false;
    render::ShadeRamp shades  = render::freshCustomPalette();

    friend constexpr bool operator==(const CustomPaletteSlot&, const CustomPaletteSlot&) = default;
};

struct CustomPalettes {
    render::ShadeRamp                                        working = render::freshCustomPalette();
    std::array<CustomPaletteSlot, kCustomPaletteSlotCount>   slots{};

    friend constexpr bool operator==(const CustomPalettes&, const CustomPalettes&) = default;
};

// The slot the working palette is saved in: the first present slot holding exactly its colors, or
// nothing when none does - which is what "unsaved" means.
[[nodiscard]] std::optional<std::uint8_t> savedSlotOf(const CustomPalettes& palettes) noexcept;

// The present slots as a walk. nextPresentSlot gives the first present slot numbered above `after`
// (0 for the first of all); previousPresentSlot gives the last present slot numbered below `before`
// (kCustomPaletteSlotCount + 1 for the last of all). Nothing when there is none that way.
[[nodiscard]] std::optional<std::uint8_t> nextPresentSlot(const CustomPalettes& palettes,
                                                          std::uint8_t          after) noexcept;
[[nodiscard]] std::optional<std::uint8_t> previousPresentSlot(const CustomPalettes& palettes,
                                                              std::uint8_t          before) noexcept;

// The save document: schema version and image size. The name is spelled as a literal at the call
// sites, as the other documents' are.
//
// One hundred records of thirteen bytes: a presence byte, then red, green and blue for the darkest,
// dark, light and lightest shades. Record 0 is the working palette, whose presence byte is written as
// 1 and not read; records 1 to 99 are the slots in order. Alpha is not stored - a palette color is
// opaque - and a decoded color's alpha is 255.
inline constexpr std::uint32_t kCustomPalettesSchemaVersion = 1;
inline constexpr std::size_t   kCustomPaletteRecordBytes    = 13;
inline constexpr std::size_t   kCustomPalettesImageBytes =
    (1 + kCustomPaletteSlotCount) * kCustomPaletteRecordBytes;

[[nodiscard]] std::array<std::uint8_t, kCustomPalettesImageBytes> encodeCustomPalettes(
    const CustomPalettes& palettes);

// Decode an image into `palettes`. Returns false and leaves `palettes` untouched unless the image is
// exactly the length this build writes; any non-zero presence byte means present.
[[nodiscard]] bool decodeCustomPalettes(std::span<const std::uint8_t> image,
                                        CustomPalettes&               palettes);

// Persist the palettes as document "palettes" at the current schema version. Returns whatever the
// atomic write reports.
bool saveCustomPalettes(const CustomPalettes& palettes, retropp::SaveStore& store);

// Load the palettes from the store. Absent document (ordinary first run) -> leave the defaults, return
// false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error, leave the
// defaults, leave the file in place, return false.
//
// Declares this document's schema version on the store immediately before reading, because the
// version is the store's rather than the document's (src/state/settings.h says the same).
bool loadCustomPalettes(retropp::SaveStore& store, CustomPalettes& palettes);

}  // namespace kirpich
