#pragma once

// The player's own palette rotations: ninety-nine slots they can fill, and the working rotation - the
// one being edited, which is also the one a round is drawn through while Settings::paletteRotation is
// CUSTOM (src/systems/frame_palette.h). A rotation is ten palettes, one for each level a round can
// show modulo ten. The two calls at the bottom keep them across launches.
//
// Like the settings and the custom palettes, these belong to the player rather than to the machine's
// state image: they survive the reset chord and outlive a launch, so they live outside GameContext.
// They persist in their own save document beside the settings and under the same identity.
//
// A slot is EMPTY until a rotation is saved into it, and empty again once cleared. A fresh rotation -
// what an empty slot holds and what the working rotation starts as - is ten copies of the default
// grayscale the game ships in (freshCustomPalette), which draws exactly as no rotation at all. The
// shipped presets are not stored here (src/render/rotation_presets.h).
//
// Whether the working rotation is SAVED is never stored. It is a comparison: the working rotation is
// saved when some present slot holds exactly its colors, level for level. Nothing can therefore drift
// from the colors beside it.
//
// Slot numbers are one-based at every surface below: slot 1 is the first.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <retropp/save_store.h>

#include "render/palettes.h"  // ShadeRamp, freshCustomPalette

namespace kirpich {

// How many palettes a rotation holds: the level a round shows, modulo ten, picks one.
inline constexpr std::size_t kRotationLevelCount = 10;

inline constexpr std::size_t kCustomRotationSlotCount = 99;

// Ten fresh custom palettes, the rotation a player starts from.
[[nodiscard]] constexpr std::array<render::ShadeRamp, kRotationLevelCount>
freshRotationLevels() noexcept {
    std::array<render::ShadeRamp, kRotationLevelCount> levels{};
    for (render::ShadeRamp& level : levels) {
        level = render::freshCustomPalette();
    }
    return levels;
}

// One palette per level, level 0 first. Each is a ShadeRamp whose mayBottomOutAtBlack is always off,
// as a custom palette's is, so a level compares equal to any palette holding the same colors.
struct RotationProgression {
    std::array<render::ShadeRamp, kRotationLevelCount> levels = freshRotationLevels();

    friend constexpr bool operator==(const RotationProgression&,
                                     const RotationProgression&) = default;
};

// One slot: whether anything has been saved into it, and the rotation it holds. A slot compares equal
// to the working rotation color for color, as a custom palette slot does.
struct CustomRotationSlot {
    bool                present = false;
    RotationProgression progression{};

    friend constexpr bool operator==(const CustomRotationSlot&, const CustomRotationSlot&) = default;
};

struct CustomRotations {
    RotationProgression                                     working{};
    std::array<CustomRotationSlot, kCustomRotationSlotCount> slots{};

    friend constexpr bool operator==(const CustomRotations&, const CustomRotations&) = default;
};

// The slot the working rotation is saved in: the first present slot holding exactly its colors, or
// nothing when none does - which is what "unsaved" means.
[[nodiscard]] std::optional<std::uint8_t> savedSlotOf(const CustomRotations& rotations) noexcept;

// Whether the working rotation holds changes that would be lost: it is saved in no slot, and it is not
// the fresh rotation a player starts from. A rotation never edited has nothing to lose.
[[nodiscard]] bool hasUnsavedChanges(const CustomRotations& rotations) noexcept;

// The present slots as a walk. nextPresentSlot gives the first present slot numbered above `after`
// (0 for the first of all); previousPresentSlot gives the last present slot numbered below `before`
// (kCustomRotationSlotCount + 1 for the last of all). Nothing when there is none that way.
[[nodiscard]] std::optional<std::uint8_t> nextPresentSlot(const CustomRotations& rotations,
                                                          std::uint8_t           after) noexcept;
[[nodiscard]] std::optional<std::uint8_t> previousPresentSlot(const CustomRotations& rotations,
                                                              std::uint8_t           before) noexcept;

// The save document: schema version and image size. The name is spelled as a literal at the call
// sites, as the other documents' are.
//
// One hundred records of 121 bytes: a presence byte, then level 0 to 9, each red, green and blue for
// the darkest, dark, light and lightest shades - twelve bytes a level, level j at offset 1 + 12j.
// Record 0 is the working rotation, whose presence byte is written as 1 and not read; records 1 to 99
// are the slots in order. Alpha is not stored - a palette color is opaque - and a decoded color's
// alpha is 255.
inline constexpr std::uint32_t kCustomRotationsSchemaVersion = 1;
inline constexpr std::size_t   kCustomRotationRecordBytes    = 1 + kRotationLevelCount * 12;
inline constexpr std::size_t   kCustomRotationsImageBytes =
    (1 + kCustomRotationSlotCount) * kCustomRotationRecordBytes;

[[nodiscard]] std::array<std::uint8_t, kCustomRotationsImageBytes> encodeCustomRotations(
    const CustomRotations& rotations);

// Decode an image into `rotations`. Returns false and leaves `rotations` untouched unless the image is
// exactly the length this build writes; any non-zero presence byte means present.
[[nodiscard]] bool decodeCustomRotations(std::span<const std::uint8_t> image,
                                         CustomRotations&              rotations);

// Persist the rotations as document "rotations" at the current schema version. Returns whatever the
// atomic write reports.
bool saveCustomRotations(const CustomRotations& rotations, retropp::SaveStore& store);

// Load the rotations from the store. Absent document (ordinary first run) -> leave the defaults,
// return false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error,
// leave the defaults, leave the file in place, return false.
//
// Declares this document's schema version on the store immediately before reading, because the
// version is the store's rather than the document's (src/state/settings.h says the same).
bool loadCustomRotations(retropp::SaveStore& store, CustomRotations& rotations);

}  // namespace kirpich
