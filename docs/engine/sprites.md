# Composed sprites

Every multi-tile sprite the game draws, resolved into a self-contained list of parts: the 94
identities in `SpriteId`, one composed `Sprite` per identity, and `PieceKind` for the seven tetromino
shapes. A `Sprite` holds two signed render offsets and its parts; each part is a pixel position, an
x-flip flag, and a tile index. The grids and escape encoding the original walks to build these are
already resolved — nothing here decodes a stream at run time.

## Where each surface lives

| Surface | File | Editing it |
|---|---|---|
| `SpriteId` | `include/kirpich/sprite_id.h` | **Generated — do not hand-edit.** |
| `SpritePart`, `Sprite`, `kMaxSpriteParts` | `src/data/sprites.h` | Hand-written types. |
| `kSprites` | `src/data/sprites.h` | **Generated** (values come from `generated/sprites_data.inc`). |
| `getSprite` | `src/data/sprites.h` | Hand-written accessor. |
| `BoundedVec<T, N>` | `src/data/bounded_vec.h` | Hand-written container. |
| `PieceKind` | `include/kirpich/piece_kind.h` | Hand-written. |

The data types are in `namespace kirpich`; include them as `"data/sprites.h"`. `SpriteId` and
`PieceKind` are in `namespace kirpich` too — `<kirpich/sprite_id.h>`, `<kirpich/piece_kind.h>`.

## The types

```cpp
enum class SpriteId : std::uint8_t { L_0 = 0x00, /* … */ ROCKET_EXHAUST_2 = 0x5D };  // 94 values

struct SpritePart {
    std::uint8_t y;      // pixels down from the composed origin (0..56, a multiple of 8)
    std::uint8_t x;      // pixels right
    bool         xflip;  // toggles the OAM x-flip bit for this tile
    std::uint8_t tile;   // OBJ tile-sheet index
};

struct Sprite {
    SpriteId    id;
    std::int8_t offset_y;  // signed; the renderer adds it to the draw position
    std::int8_t offset_x;
    BoundedVec<SpritePart, kMaxSpriteParts> parts;  // kMaxSpriteParts == 28
};
```

`SpriteId` is a dense space, `0x00`–`0x5D`, with no "none" value — a sprite's visibility is a separate
concern the renderer handles. `SpritePart` and `Sprite` both have a defaulted `operator==`.

## Looking a sprite up

```cpp
#include "data/sprites.h"

const kirpich::Sprite& buran = kirpich::getSprite(kirpich::SpriteId::BURAN);
buran.offset_y;       // -32
buran.parts.size();   // 28
buran.parts[0].tile;  // 0xC0

for (const kirpich::SpritePart& part : buran.parts) {
    // part.y, part.x, part.xflip, part.tile
}
```

`getSprite(id)` returns the composed sprite for an identity; it debug-asserts the id is in range. The
whole table is also directly available as `kirpich::kSprites` (a `std::array<Sprite, 94>`), indexed by
the `SpriteId` value — `kSprites[static_cast<std::size_t>(id)]` is the same row `getSprite` returns.

`parts` is a `BoundedVec` — a fixed-capacity inline vector. Use `size()`, `operator[]`, and range-for
over its live elements; `capacity()` is `kMaxSpriteParts` (28, the largest part count in the data).

## Gotchas

- **`x`, `y` are decimal pixel offsets; `tile` is a raw index in hex.** `y` and `x` are positions on
  the 8-pixel grid (`0`, `8`, `16`, …). `tile` is an OBJ tile-sheet index with no named set, stored
  as its raw byte.
- **`xflip` is a toggle, not an absolute flip.** It flips the tile relative to the descriptor's base
  attribute the renderer supplies, so the same part reads differently under a descriptor that is
  itself flipped.
- **The offsets are not folded into the parts.** `offset_y` / `offset_x` are applied by the renderer
  on top of the part positions and the on-screen position, so a part's `y` / `x` are grid-relative,
  not screen-relative.
- **Four ids share another's geometry.** `JUMPING_LARGE_MARIO_2_ALT`, `CRYING_SMALL_LUIGI_2_ALT`,
  `VIOLINIST_1_ALT`, and `ROCKET_S_ALT` each carry a full copy of the layout of the id they repeat.
  Their `parts` and offsets are equal to their partner's, but `id` differs — so comparing two whole
  `Sprite`s across such a pair is `false`; compare `.parts` (and the offsets) to see the shared
  geometry.

## `PieceKind`

```cpp
#include <kirpich/piece.h>

kirpich::Piece p{0x0A};
p.kind();  // kirpich::PieceKind::I  (0x0A >> 2 == 2)
```

`enum class PieceKind : uint8_t { L=0, J=1, I=2, O=3, S=4, Z=5, T=6 }`. The first 28 `SpriteId` values
are the four rotations of each kind in this order, and a piece's byte (`kind·4 + rotation`) is exactly
the `SpriteId` value of its rotation sprite. There is no "no piece" value.

## Regenerating

`SpriteId`, `kSprites`, and the test fixture come from the disassembly by the parser. Regenerate them
after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_sprites.py \
  --source-root ../tetris \
  --all \
  --enum-out    include/kirpich/sprite_id.h \
  --inc-out     src/data/generated/sprites_data.inc \
  --fixture-out tests/fixtures/sprites_expected.h
```

The parser resolves the identity table, the records, the tile lists, and the grids, walks each tile
stream to compose its parts, and checks the structure as it reads — the label addresses, the escape
encoding, the 1:1 record-to-list mapping, the grid geometry, and the corpus totals — stopping with a
citation if anything has moved rather than emitting a wrong file. Python 3 (standard library only); it
is a development tool and is never needed to build or test Kirpich.

## Changing values

`SpriteId` and `kSprites` are **generated** — to change one, change the source or the parser and
regenerate; a hand edit to `sprite_id.h` or the `.inc` is overwritten on the next run. `getSprite`,
`BoundedVec`, and `PieceKind` are hand-written — edit their files directly.

The exact meaning of every value, with its line in the original, is in
[`../contracts/sprites.md`](../contracts/sprites.md).

## Testing

`tests/test_sprites.cpp` re-composes every identity from the raw fixture and compares it field-for-
field against `kSprites`, and pins the `SpriteId` values, the signed offsets, the aliased identities,
the piece-rotation link, the x-flip resolutions, the grid geometry, and the rocket-tier link. The
parser has its own tests (`tools/asm_parser/test_parse_sprites.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_sprites`).
