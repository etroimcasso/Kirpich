#pragma once

// Miscellaneous data: the loose tables and constants that do not belong to any larger data class.
//
// Four kinds of data live here:
//
//   - Raw-OAM object tables. A few screens (the two-player face pairs, the "PUSH START" prompt) draw
//     sprites by copying fixed {y, x, tile, x-flip} records straight into the object buffer, rather
//     than through the composed-sprite path in src/data/sprites.h. Those records are OamObject.
//   - Cursor coordinate tables. Each menu that moves a selection cursor over a row of choices - the
//     Type-A and Type-B level pickers, the Type-B and two-player start-height pickers, the music-type
//     picker - stores the screen position of every choice as a {y, x} SpriteCoordinate, indexed by
//     the selected value.
//   - Text strings. The four two-player win-screen strings are raw gameplay-tileset bytes (their tile
//     numbers are NOT the character map's - a byte that collides with a character-map slot draws a
//     different glyph), so they are plain tile arrays. "pause" is written through the character map,
//     so it is a CharTile array (see include/kirpich/char_tile.h).
//   - Constants. The value that marks demo recording as active, and the two numbers that describe the
//     completed-row scan (which field row it starts on and how many rows it checks).
//
// The tables and constants are generated from the disassembly by tools/asm_parser/parse_misc.py;
// change a value there and regenerate rather than editing misc_data.inc. The consumer sites for each
// table and the reasons behind the two scan constants are specified in docs/contracts/misc.md.

#include <array>
#include <cstdint>

#include <kirpich/char_tile.h>
#include <kirpich/music_type.h>

namespace kirpich {

// One sprite object copied verbatim into the object buffer: its screen position, the tile to draw,
// and whether it is mirrored horizontally. `tile` is a raw gameplay-tileset index, not a SpriteId -
// these tables bypass the composed-sprite path.
struct OamObject {
    std::uint8_t y;      // OAM Y coordinate
    std::uint8_t x;      // OAM X coordinate
    std::uint8_t tile;   // gameplay-tileset VRAM tile index
    bool         xflip;  // OAM horizontal flip

    friend constexpr bool operator==(const OamObject&, const OamObject&) = default;
};

// One on-screen cursor position: the OAM {y, x} a selection cursor sits at for one menu choice.
struct SpriteCoordinate {
    std::uint8_t y;  // OAM Y coordinate
    std::uint8_t x;  // OAM X coordinate

    friend constexpr bool operator==(const SpriteCoordinate&, const SpriteCoordinate&) = default;
};

// The OamObject / SpriteCoordinate / string arrays and the three constants, generated at namespace
// scope. The level and start-height coordinate tables are indexed directly by the selected value
// (0-based); music-type is indexed through musicTypeSpriteCoordinate below.
#include "generated/misc_data.inc"

// The cursor position for a music-type selection. The stored music-type value is the cursor's tile
// number rather than a 0-based index (see include/kirpich/music_type.h), so the table index is the
// value measured from the first music-type tile.
[[nodiscard]] constexpr SpriteCoordinate musicTypeSpriteCoordinate(MusicType type) noexcept {
    return kMusicTypeSpriteCoordinates[static_cast<std::uint8_t>(type)
                                       - static_cast<std::uint8_t>(MusicType::MUSIC_A)];
}

}  // namespace kirpich
