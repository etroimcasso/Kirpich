# Composed sprites

Every multi-tile sprite the game can draw, ported as one composed record per identity: the falling
piece in each of its rotations, the game-type and music-off labels, the score digits, the Mario and
Luigi victory-and-defeat characters, the Buran shuttle and the rockets with their smoke and exhaust,
and the ending-dance musicians. The original builds each sprite at draw time from three levels — an
identity that selects a record, a record that names a tile list and two offsets, and a tile list that
walks one of five shared pixel grids under a small escape encoding. This unit resolves all three into
a single self-contained list of parts per identity, so the eventual renderer reads a finished
composition rather than re-walking the encoding.

The same work introduces `SpriteId`, the identity space the game passes in a sprite descriptor, and
folds in the five geometry grids and `PieceKind` that the earlier sprite-grids unit carried.

## What they are

| Surface | Where | Shape |
|---|---|---|
| `SpriteId` | `include/kirpich/sprite_id.h` | `enum class : uint8_t`, 94 values `L_0`..`ROCKET_EXHAUST_2` |
| `SpritePart` | `src/data/sprites.h` | `{ y, x, xflip, tile }` |
| `Sprite` | `src/data/sprites.h` | `{ id, offset_y, offset_x, parts }` |
| `kSprites` | `src/data/sprites.h` | `std::array<Sprite, 94>` |
| `getSprite` | `src/data/sprites.h` | `SpriteId → const Sprite&` |
| `BoundedVec<T, N>` | `src/data/bounded_vec.h` | fixed-capacity inline vector |
| `PieceKind` | `include/kirpich/piece_kind.h` | `enum class : uint8_t`, `L`..`T` |

The 94 identities compose from 90 distinct records (four identities repeat another's target). The
exact values and their sources are pinned in [`../contracts/sprites.md`](../contracts/sprites.md).

## Decisions

**Port the composition, not the serialization.** The original resolves an identity through a record,
a tile list, a grid, and a byte-stream escape encoding (`$FF` ends the list, `$FE` leaves a gap, `$FD`
mirrors the next tile). Rather than store those raw levels, the port resolves them once into a list of
parts — each a grid position, an x-flip flag, and a tile — so a consumer never walks the encoding. The
raw serialization is kept only in the test fixture, which re-runs the encoding to prove the
composition.

**One identity space, minted as an enum.** The identity index the game passes in a descriptor has no
constants file upstream, so `SpriteId` is minted from the sprite-list comments — 94 dense values,
`0x00`–`0x5D`, no sentinel. Animation pairs are `_1`/`_2`.

**Aliases duplicated by value.** Four identities point at another's target, so their geometry is
identical. Each carries its own full copy and an `_ALT` name, so every id stands alone; they compare
equal to their partner in parts and offsets but differ in `id`.

**The tile is a raw index.** A part's tile is an OBJ tile-sheet index with no named set in the source,
so it stays a raw `uint8_t`. The pixel positions (`y`, `x`) and the record offsets are plain integers.

**Offsets stay separate from part positions.** The renderer sums the on-screen position, the record
offset, and the grid pair through a carry chain, so the terms are order-sensitive at the byte level.
The composition keeps the record offsets and the part positions apart rather than pre-adding them.

**A fixed-capacity part container.** A sprite has between one and 28 parts, so `parts` is a
`BoundedVec<SpritePart, 28>` — inline storage sized to the largest sprite, constructed from a braced
list, with a live count. `std::array` would pad every sprite to 28 and `std::vector` would rule out
the `constexpr` table.

**Header-only.** The composed table is `constexpr` data behind two plain structs and one accessor, so
it lives entirely in `sprites.h` with no `.cpp`.

**`PieceKind` is `L, J, I, O, S, Z, T`.** The order is not chosen — a piece's kind is used directly as
the sprite-list index, whose first 28 entries are the four rotations of each shape in this order.
`Piece::of(kind, rotation).raw` is exactly the `SpriteId` value of that rotation sprite.

## Keeping them honest

The enum, the composed table, and the test fixture are generated from the disassembly by
`tools/asm_parser/parse_sprites.py`, which resolves all three levels, walks each tile stream to
compose its parts, and checks the structure as it reads — the label addresses, the escape encoding,
the 1:1 record-to-list mapping, the grid geometry, the four expected shared targets, and the corpus
totals — stopping with a citation rather than emitting a wrong file. The C++ test re-composes every
identity from the raw fixture independently and compares it to the table, so a transcription slip or a
composition bug shows up as a failing parse or a failing test instead of a silent divergence.
`PieceKind` has no upstream symbol table to transcribe, so it is hand-written and the contract is its
authority. See [`../engine/sprites.md`](../engine/sprites.md) for how to regenerate and change them.
