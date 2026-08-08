#pragma once

// Composed OAM sprites: every multi-tile sprite the game can draw, resolved into a self-contained
// part list.
//
// The original assembles each sprite from three levels at draw time - an identity that selects a
// record, a record that names a tile list and two render offsets, and a tile list that walks a
// shared pixel-offset grid, using a little escape encoding to skip cells and mirror tiles. This
// surface stores the resolved result: one Sprite per identity, each carrying its two offsets and the
// full list of parts (grid position, x-flip, and tile) the renderer would place. Nothing here needs
// the grids or the escape stream at run time - the composition is already done.
//
// A part's y and x are the pixel offset of its tile from the sprite's composed origin; the renderer
// adds the record's offset_y / offset_x and the on-screen descriptor position on top. xflip toggles
// the OAM horizontal-flip bit for that one tile (it is a toggle against the descriptor's base
// attribute, not an absolute flip). tile is a raw OBJ tile-sheet index. Four identities alias an
// earlier one's layout (the _ALT enumerators in SpriteId); each carries its own full copy, so every
// id stands alone.
//
// The table and enum are generated from the disassembly by tools/asm_parser/parse_sprites.py; edit
// the values there and regenerate, not here. The three-level layout, the escape encoding, and the
// renderer's walk are specified in docs/contracts/sprites.md.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <kirpich/sprite_id.h>

#include "data/bounded_vec.h"

namespace kirpich {

// The most parts any one sprite composes to (the Buran shuttle). The generator asserts the corpus
// maximum is exactly this, so the inline part storage never overflows.
inline constexpr std::size_t kMaxSpriteParts = 28;

// One drawn tile of a sprite: its pixel offset from the composed origin, the x-flip toggle, and the
// raw OBJ tile index. Not a ROM byte layout - the ROM serialization lives in the test fixture.
struct SpritePart {
    std::uint8_t y;      // pixels down from the composed origin (0..56, a multiple of 8)
    std::uint8_t x;      // pixels right from the composed origin
    bool         xflip;  // toggles the OAM x-flip bit for this tile
    std::uint8_t tile;   // OBJ tile-sheet index

    friend constexpr bool operator==(const SpritePart&, const SpritePart&) = default;
};

// One composed sprite: its identity, the two signed offsets the renderer applies at draw time, and
// its parts in draw order.
struct Sprite {
    SpriteId                                id;
    std::int8_t                             offset_y;  // signed; the renderer adds it to the draw position
    std::int8_t                             offset_x;
    BoundedVec<SpritePart, kMaxSpriteParts> parts;

    friend constexpr bool operator==(const Sprite&, const Sprite&) = default;
};

// kSprites, generated at namespace scope: one row per identity, in SpriteId index order.
#include "generated/sprites_data.inc"

// Every row sits at the position its .id names, so indexing by SpriteId is exact.
static_assert([] {
    for (std::size_t i = 0; i < kSprites.size(); ++i) {
        if (static_cast<std::size_t>(kSprites[i].id) != i) {
            return false;
        }
    }
    return true;
}(), "kSprites rows must be in SpriteId index order, one row per id");

// The composed sprite for an identity. `id` must be a valid SpriteId (the enum has no gaps).
[[nodiscard]] constexpr const Sprite& getSprite(SpriteId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kSprites.size() && "SpriteId out of range");
    return kSprites[index];
}

}  // namespace kirpich
