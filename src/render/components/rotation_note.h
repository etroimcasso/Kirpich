#pragma once

// The words that say a round's palette follows its level: "(rotating)", through the ramp's dim font
// palette. Drawn while Settings::paletteRotation is not OFF; the settings screen places it under its
// palette row (render/settings_overlay.h).

#include <cstdint>
#include <string_view>

#include "render/components/glyphs.h"  // DimGlyphs
#include "render/tile_atlas.h"
#include "render/types.h"
#include "state/settings.h"

namespace kirpich::render {

inline constexpr std::string_view kRotationNote = "(rotating)";

// Whether the note is drawn: while a round's palette follows its level.
[[nodiscard]] constexpr bool rotationNoteShown(const kirpich::Settings& s) noexcept {
    return s.paletteRotation != kirpich::PaletteRotation::OFF;
}

// The note with its first glyph's top-left at (x, y), one glyph every `pitch` pixels, named as
// DimGlyphs names them; nothing while the rotation is off.
[[nodiscard]] inline Sprites RotationNote(const kirpich::Settings& s, int x, int y, int pitch,
                                          const TileAtlas& atlas, std::uint8_t ramp) {
    return rotationNoteShown(s) ? DimGlyphs(kRotationNote, x, y, pitch, atlas, ramp) : Sprites{};
}

}  // namespace kirpich::render
