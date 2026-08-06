#pragma once

// Sprite layout grids: the shared (y, x) pixel-offset frames the sprite renderer walks.
//
// A composite sprite is drawn by walking its tile list against one of these grids in lockstep,
// consuming one offset pair per drawn cell and adding it to the sprite's origin to place each OAM
// entry. The grids are shared geometry - the same frame is reused across piece sprites, characters,
// the shuttle, rockets, digits, and menu labels - so they hold no piece-specific or rotation data.
// A tile list can terminate before a grid is exhausted, so a grid is a maximal frame.
//
// Every offset is a multiple of 8 (one 8x8 tile) in the range 0x00..0x38. The five grids and their
// element rows are generated from the disassembly by tools/asm_parser/parse_sprite_grids.py; edit
// the values there and regenerate, not here. The behavioral spec for how the renderer consumes a
// grid lives in docs/contracts/sprite-grids.md.

#include <array>
#include <cstdint>

namespace kirpich {

// One (y, x) offset pair, added to a sprite's origin per drawn cell. Y first: the renderer consumes
// the Y offset first, matching OAM byte order. Two plain bytes, byte-identical to the ROM table.
struct SpriteGridOffset {
    std::uint8_t y;  // pixels down from the sprite origin
    std::uint8_t x;  // pixels right from the sprite origin
};
static_assert(sizeof(SpriteGridOffset) == 2, "SpriteGridOffset must be two ROM-equivalent bytes");

// The five grids, generated at namespace scope: kSpriteGrid4x4 / 1x8 / 7x2 / 8x4Notched / 3x3.
#include "generated/sprite_grids_data.inc"

}  // namespace kirpich
