#pragma once

// The player's own palette rotation: ten palettes, one for each level a round can show modulo ten,
// which a round is drawn through while Settings::paletteRotation is CUSTOM
// (src/systems/frame_palette.h). The two calls at the bottom keep it across launches.
//
// Like the settings and the custom palettes, it belongs to the player rather than to the machine's
// state image: it survives the reset chord and outlives a launch, so it lives outside GameContext. It
// persists in its own save document beside the settings and under the same identity.
//
// Every level always holds a palette. A fresh progression is ten copies of the default grayscale the
// game ships in (freshCustomPalette), which draws exactly as no rotation at all; the shipped presets
// are not stored here (src/render/rotation_presets.h).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <retropp/save_store.h>

#include "render/palettes.h"  // ShadeRamp, freshCustomPalette

namespace kirpich {

// How many palettes a rotation holds: the level a round shows, modulo ten, picks one.
inline constexpr std::size_t kRotationLevelCount = 10;

// Ten fresh custom palettes, the progression a player starts from.
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

// Whether the progression differs from the fresh one in any color of any level.
[[nodiscard]] bool hasChanges(const RotationProgression& progression) noexcept;

// The save document: schema version and image size. The name is spelled as a literal at the call
// sites, as the other documents' are.
//
// Ten records of twelve bytes, level 0 first: red, green and blue for the darkest, dark, light and
// lightest shades. There is no presence byte, because every level always holds a palette. Alpha is
// not stored - a palette color is opaque - and a decoded color's alpha is 255.
inline constexpr std::uint32_t kRotationProgressionSchemaVersion = 1;
inline constexpr std::size_t   kRotationRecordBytes              = 12;
inline constexpr std::size_t   kRotationProgressionImageBytes =
    kRotationLevelCount * kRotationRecordBytes;

[[nodiscard]] std::array<std::uint8_t, kRotationProgressionImageBytes> encodeRotationProgression(
    const RotationProgression& progression);

// Decode an image into `progression`. Returns false and leaves `progression` untouched unless the
// image is exactly the length this build writes.
[[nodiscard]] bool decodeRotationProgression(std::span<const std::uint8_t> image,
                                             RotationProgression&          progression);

// Persist the progression as document "rotation" at the current schema version. Returns whatever the
// atomic write reports.
bool saveRotationProgression(const RotationProgression& progression, retropp::SaveStore& store);

// Load the progression from the store. Absent document (ordinary first run) -> leave the defaults,
// return false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error,
// leave the defaults, leave the file in place, return false.
//
// Declares this document's schema version on the store immediately before reading, because the
// version is the store's rather than the document's (src/state/settings.h says the same).
bool loadRotationProgression(retropp::SaveStore& store, RotationProgression& progression);

}  // namespace kirpich
