# Sprite layout grids

The geometry layer of the composite-sprite system: five shared grids of (y, x) pixel offsets that the
sprite renderer walks to place a multi-tile sprite's cells. The same five frames serve every kind of
composite sprite in the game — the piece sprites, Mario and Luigi, the dancers, the Buran shuttle, the
rockets, the score digits, and the menu labels — so they are shared geometry, not per-sprite or
per-rotation data. Porting them first gives the tile-list records and the sprite renderer a typed
surface to reference by name.

The same work introduces `PieceKind`, the named enum for the seven tetromino shapes, whose ordering
the sprite machinery fixes.

## What they are

| Surface | Where | Shape |
|---|---|---|
| `SpriteGridOffset` | `src/data/sprite_grids.h` | 2-byte struct, `{ y, x }` |
| `kSpriteGrid4x4` | `src/data/sprite_grids.h` | `std::array<SpriteGridOffset, 16>` |
| `kSpriteGrid1x8` | `src/data/sprite_grids.h` | `std::array<SpriteGridOffset, 8>` |
| `kSpriteGrid7x2` | `src/data/sprite_grids.h` | `std::array<SpriteGridOffset, 14>` |
| `kSpriteGrid8x4Notched` | `src/data/sprite_grids.h` | `std::array<SpriteGridOffset, 28>` |
| `kSpriteGrid3x3` | `src/data/sprite_grids.h` | `std::array<SpriteGridOffset, 9>` |
| `PieceKind` | `include/kirpich/piece_kind.h` | `enum class : uint8_t`, `L`..`T` |

The five grids total 150 bytes (75 offset pairs). The exact values and their sources are pinned in
[`../contracts/sprite-grids.md`](../contracts/sprite-grids.md).

## Decisions

**The grids are named for their geometry, not their addresses.** The disassembly labels them by ROM
address (`Matrix_31A9` and so on) inside a section it never named. The port names each for its shape —
`kSpriteGrid4x4`, `1x8`, `7x2`, `8x4Notched`, `3x3` — which is what a reader consuming one actually
needs to know.

**An offset is a two-byte struct, Y first.** Each element is a plain `{ y, x }` pair of bytes, matching
the ROM's storage exactly (`sizeof(SpriteGridOffset) == 2`, no packing). Y comes first because the
renderer consumes the Y offset first, in OAM byte order.

**No lookup accessor.** Upstream sprites reference a grid by raw address; there is no index or ID space
to key on. Port consumers reference the arrays by name, so there is no `getSpriteGrid(...)` — adding
one would invent structure the original does not have.

**Header-only.** The grids are 150 bytes of `constexpr` data behind a single struct with no accessor
body, so they live entirely in `sprite_grids.h` with no `.cpp`.

**`PieceKind` is `L, J, I, O, S, Z, T`.** The order is not chosen — a piece's kind is used directly as
the index into the game's sprite list, whose first entries are grouped four-to-a-shape in exactly this
order. `Piece::kind()` returns a `PieceKind` instead of a bare index. There is no "no piece" value.

## Keeping them honest

The grid arrays and their test fixture are generated from the disassembly by
`tools/asm_parser/parse_sprite_grids.py`, which checks the source's structure as it reads — the five
labels in order, each grid's byte count against the address deltas encoded in its label, and every
byte a multiple of 8 within range — and stops with a citation rather than emitting a wrong file. So a
change to the original, or a transcription slip, shows up as a failing parse or a failing test instead
of a silent divergence. `PieceKind` has no upstream symbol table to transcribe, so it is hand-written
and the contract is its authority. See [`../engine/sprite-grids.md`](../engine/sprite-grids.md) for how
to regenerate and change them.
