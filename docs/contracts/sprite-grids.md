# Contract — Sprite layout grids

Reverse-derived behavioral contract for Kirpich's sprite layout grids: the five shared (y, x)
pixel-offset frames the sprite renderer walks, and the `PieceKind` enum whose ordering the same
sprite machinery fixes. Every value here is transcribed from the `kaspermeerts/tetris` disassembly
(upstream `b95c668`); the line anchors below are the authority the tests check against.

The grids live in one contiguous, unnamed section — `SECTION "TODO Name", ROM0[$31A9]`
(`tetris.asm:6900`) — that the disassembler never gave a real name. They are **shared geometry**, not
piece-specific data: the same frame is reused across piece sprites, characters, the shuttle, rockets,
digits, and menu labels. They hold no rotation data and no tetromino shapes — those live in the
per-sprite tile lists, ported separately.

---

## The five grids

Each grid is a run of `db` bytes forming (y, x) offset pairs. Every byte is a multiple of 8 (one 8×8
tile) in the range `$00`–`$38`. Pairs are atomic (Y first, then X — the renderer consumes the Y
offset first, `tetris.asm:6778`, matching OAM byte order).

| C++ array | Upstream label | Anchor | Pairs | Geometry |
|---|---|---|---|---|
| `kSpriteGrid4x4` | `Matrix_31A9::` | `tetris.asm:6902` | 16 | 4 rows × 4 cols, row-major |
| `kSpriteGrid1x8` | `Matrix_31C9::` | `tetris.asm:6908` | 8 | 1 row × 8 cols |
| `kSpriteGrid7x2` | `Matrix_31D9::` | `tetris.asm:6911` | 14 | 7 rows × 2 cols |
| `kSpriteGrid8x4Notched` | `Matrix_31F5::` | `tetris.asm:6920` | 28 | 8 rows, notched (see below) |
| `kSpriteGrid3x3` | `Matrix_322D::` | `tetris.asm:6930` | 9 | 3 rows × 3 cols |

The section ends at `GameplayTiles::` (`tetris.asm:6935`), which follows `Matrix_322D` immediately.

**Regular geometry.** Four grids are pure row-major, 8 pixels per step:

- `kSpriteGrid4x4[i]  == { .y = 8·(i/4), .x = 8·(i%4) }`
- `kSpriteGrid1x8[i]  == { .y = 0,       .x = 8·i }`
- `kSpriteGrid7x2[i]  == { .y = 8·(i/2), .x = 8·(i%2) }`
- `kSpriteGrid3x3[i]  == { .y = 8·(i/3), .x = 8·(i%3) }`

**Notched geometry** (`kSpriteGrid8x4Notched`). The first two rows hold only 2 pairs each and begin
at `x = $08` (the `x = $00` column is absent — the notch); rows 2–7 are full 4-wide rows at
`x = $00`–`$18`:

- rows 0–1: `[2·r + c] == { .y = 8·r, .x = 8·(c+1) }` for `c ∈ 0..1`
- rows 2–7: `[4 + 4·(r-2) + c] == { .y = 8·r, .x = 8·c }` for `c ∈ 0..3`

Boundary pairs (hand-traced against `tetris.asm:6902-6934`):

| Entry | (y, x) |
|---|---|
| `kSpriteGrid4x4[5]` | `($08, $08)` |
| `kSpriteGrid4x4[15]` | `($18, $18)` |
| `kSpriteGrid1x8[7]` | `($00, $38)` |
| `kSpriteGrid7x2[13]` | `($30, $08)` |
| `kSpriteGrid8x4Notched[0]` | `($00, $08)` — the notch |
| `kSpriteGrid8x4Notched[3]` | `($08, $10)` |
| `kSpriteGrid8x4Notched[27]` | `($38, $18)` |
| `kSpriteGrid3x3[4]` | `($08, $08)` |

## How the renderer consumes a grid (consumer context)

This is background for the consumers; the renderer behavior itself ports at the sprite-renderer
feature and is not implemented here. `_RenderSprites` (`tetris.asm:6687-6856`) resolves a sprite spec
to a tile-list record and that record's first word to one of these grids, then walks the tile list
and the grid in lockstep — one (y, x) pair consumed per drawn cell, added to the sprite's origin (and
the record's signed base offsets) to place each OAM entry. X/Y flip flags make the placement
subtractive. Because a tile list can terminate before the grid is exhausted, **each grid is a
*maximal* frame** — a 2×2 sprite walks only the first cells of a 7×2 grid.

**Tile-list escape bytes** (data-format notes; the escapes live in the tile lists, not in these
grids, but they govern how the grid is walked):

- `$FF` — end of the tile list (`tetris.asm:6755`).
- `$FE` — skip this cell; the (y, x) pair is still consumed from the grid (`tetris.asm:6772-6773`,
  `.skipObject` advances the grid pointer by two).
- `$FD` — X-flip escape: toggles the OAM X-flip flag, and the next byte is the tile
  (`tetris.asm:6757-6764`).

## Consumers

Referenced by ~95 tile-list records of every kind — the 28 piece sprites, Mario/Luigi, the dancers,
the Buran shuttle, the rockets, the digits, and the A/B/C-Type + Off menu labels. Port consumers (the
tile-list records and the sprite renderer) reference the arrays by symbol; there is no index or ID
space, so the grids carry **no lookup accessor**.

---

## `PieceKind` — `include/kirpich/piece_kind.h`

`enum class PieceKind : uint8_t { L=0, J=1, I=2, O=3, S=4, Z=5, T=6 }`. The seven tetromino shapes,
in the order the game assigns them.

**Derivation.** A piece's identity byte is `kind·4 + rotation` (see the `Piece` section of
[`core-enums.md`](core-enums.md)); the kind is the high bits. That kind value is used **directly as
the sprite index** into `SpriteList` (`tetris.asm:6858`), whose first 28 entries are grouped
four-to-a-shape and commented with the shape name (`tetris.asm:6859-6865`):

```
dw $2C20, $2C24, $2C28, $2C2C ; 00 L
dw $2C30, $2C34, $2C38, $2C3C ; 04 J
dw $2C40, $2C44, $2C48, $2C4C ; 08 I
dw $2C50, $2C54, $2C58, $2C5C ; 0C O
dw $2C60, $2C64, $2C68, $2C6C ; 10 S
dw $2C70, $2C74, $2C78, $2C7C ; 14 Z
dw $2C80, $2C84, $2C88, $2C8C ; 18 T
```

The grouping comments were corroborated against the underlying tile grids: index 0 (`Sprite_2C20`)
resolves to a tile list drawing an L rotation; index 4 (`Sprite_2C30`) a J rotation. Ordering:
**L, J, I, O, S, Z, T** for values 0–6. **No sentinel** — every valid piece byte (`raw` 0–27)
decodes to one of these seven kinds.

`Piece::kind()` (`include/kirpich/piece.h`) returns `PieceKind(raw >> 2)`; `rotation()` is unchanged.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_sprite_grids.py`, `--all`):
  `src/data/generated/sprite_grids_data.inc` (the five arrays) and
  `tests/fixtures/sprite_grids_expected.h` (the same five, independent). Regenerate after any upstream
  repin; do not hand-edit.
- **Hand-written port-design:** `src/data/sprite_grids.h` (the `SpriteGridOffset` type and its
  `static_assert`s), `include/kirpich/piece_kind.h` (`PieceKind` — no symbol table exists upstream to
  transcribe, so it is hand-authored and this document is its authority).

### Transcription asserts

`parse_sprite_grids.py` hard-errors (with a `file:line` citation) on any of: the section anchor
missing before `Matrix_31A9`; any of the five labels missing, out of order, or duplicated; a
non-`db`/blank/comment line between a label and the next; a byte-count mismatch against the
address-encoded deltas (`$31C9-$31A9=32`, `$31D9-$31C9=16`, `$31F5-$31D9=28`, `$322D-$31F5=56`, and
`Matrix_322D=18`); an odd byte count; or any byte that is not a multiple of 8 or exceeds `$38`.

---

## Tested by

`tests/test_sprite_grids.cpp` — the full 75-pair sweep of the engine arrays against the fixture,
the array shapes and total byte count, each grid's closed-form geometry (regular and notched), the
seven `PieceKind` values, and the `Piece::kind()` decode across `raw = 0..27`. The parser's own
structural checks (`tools/asm_parser/test_parse_sprite_grids.py`) guard the scan against upstream
changes.
