# Contract — Composed sprites

Reverse-derived behavioral contract for Kirpich's composed OAM sprites: every multi-tile sprite the
game can draw, resolved into a self-contained part list, plus the `PieceKind` enum the same sprite
machinery fixes the ordering of. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors below are the authority the tests check against.

The game builds a sprite from three levels the renderer walks together. This unit ports the first
three (the identity space, the records, and the tile lists resolved against the grids) into one
composed record per identity; the renderer itself ports later and is described here only as the
context that gives the data its meaning.

---

## The three levels

| Level | Where | Count | Shape |
|---|---|---|---|
| `SpriteList` identity table | `tetris.asm:6858` | 94 words | `dw` record addresses; four repeat an earlier target |
| `Sprite_*` records | `sprites.asm` (`SECTION` at `sprites.asm:1`, ROM `$2C20`) | 90 | `{ dw SpriteTiles pointer, db y-offset, db x-offset }` |
| `SpriteTiles_*` tile lists | `sprites.asm` | 90 | `{ dw Matrix pointer, tile stream }` |
| `Matrix_*` grids | `tetris.asm:6902`–`6934` | 5 | runs of (y, x) pixel-offset pairs |

Each record points at one tile list and each tile list is pointed at by one record — the mapping is
exactly 1:1 (every list referenced once, none shared, none orphaned), so a record and its tile list
merge into one composed sprite. The 94 identities map onto 90 distinct records because four
`SpriteList` entries repeat an earlier address.

### `SpriteList` — the identity space

`SpriteList` (`tetris.asm:6858`) is 94 `dw` entries, index `$00`–`$5D`. The index is what the game
passes in a sprite descriptor, and what the score screen passes to choose a rocket ending. The port
mints an enumerator per index (`SpriteId`, `include/kirpich/sprite_id.h`); the disassembly has no
constants file for these, so the names come from the `SpriteList` comments (`tetris.asm:6859`–`6898`),
with animation pairs suffixed `_1`/`_2`.

Four entries repeat an earlier target, so their composed geometry is identical to another id's; the
port copies the geometry into both so every id is self-contained, and names the repeat `_ALT`:

| Index | Enumerator | Repeats | Anchor |
|---|---|---|---|
| `$2D` | `JUMPING_LARGE_MARIO_2_ALT` | `$2CCC` (index `$2B`) | `tetris.asm:6870` |
| `$42` | `CRYING_SMALL_LUIGI_2_ALT` | `$2D04` (index `$3F`) | `tetris.asm:6881` |
| `$43` | `VIOLINIST_1_ALT` | `$2D08` (index `$44`) | `tetris.asm:6882` |
| `$5B` | `ROCKET_S_ALT` | `$3112` (index `$5A`) | `tetris.asm:6897` |

### Records — offsets

A record is `{ dw SpriteTiles_*, db off_y, db off_x }` (`sprites.asm:3`–`5` for the first). The two
offsets are signed bytes the renderer applies to the draw position; across the corpus `off_y` is in
`[-$28, $00]` and `off_x` in `[-$18, $00]`.

### Tile lists — the escape encoding

A tile list is `{ dw Matrix_*, db stream }` (`sprites.asm:315`–`321` for the first). The stream is a
byte sequence the renderer walks against the grid, one grid pair consumed per drawn tile:

- **`$FF`** — ends the list (`tetris.asm:6755`). Every list ends in exactly one; it appears nowhere
  else in a stream.
- **`$FE`** — consumes one grid pair and emits no tile, leaving a gap (`tetris.asm:6766`–`6773`,
  `.skipObject` advances the grid pointer by two and draws nothing).
- **`$FD`** — sets the OAM x-flip bit for the one real tile byte that follows, and consumes no grid
  pair of its own (`tetris.asm:6757`–`6764`). The flip is a **toggle** against the descriptor's base
  attribute (`xor a, $20`, `tetris.asm:6760`), not an absolute flip. The byte after `$FD` is always a
  real tile (`< $FD`).
- **any byte `< $FD`** — a real tile: it consumes one grid pair and draws at that pair's (y, x).

No real tile byte in the corpus is `>= $FD`, so the encoding is unambiguous. Across the 90 lists the
streams total 877 bytes — 462 real tiles, 52 `$FD`, 273 `$FE`, and 90 `$FF`. The most parts any one
sprite composes to is 28 (the Buran, `SpriteTiles_30CB`, `sprites.asm:828`).

### The five grids

Each grid is a run of (y, x) pairs; every byte is a multiple of 8 (one 8×8 tile) in `$00`–`$38`,
Y first (the renderer reads the Y offset first, `tetris.asm:6778`).

| Upstream label | Anchor | Pairs | Geometry |
|---|---|---|---|
| `Matrix_31A9` | `tetris.asm:6902` | 16 | 4 rows × 4 cols, row-major |
| `Matrix_31C9` | `tetris.asm:6908` | 8 | 1 row × 8 cols |
| `Matrix_31D9` | `tetris.asm:6911` | 14 | 7 rows × 2 cols |
| `Matrix_31F5` | `tetris.asm:6920` | 28 | 8 rows, notched (below) |
| `Matrix_322D` | `tetris.asm:6930` | 9 | 3 rows × 3 cols |

The section ends at `GameplayTiles::` (`tetris.asm:6935`), which follows `Matrix_322D`. Four grids
are pure row-major, 8 pixels per step:

- `Matrix_31A9[i] == { 8·(i/4), 8·(i%4) }`
- `Matrix_31C9[i] == { 0, 8·i }`
- `Matrix_31D9[i] == { 8·(i/2), 8·(i%2) }`
- `Matrix_322D[i] == { 8·(i/3), 8·(i%3) }`

**Notched** (`Matrix_31F5`): the first two rows hold 2 pairs each and begin at `x = $08` (the
`x = $00` column is absent — the notch); rows 2–7 are full 4-wide rows at `x = $00`–`$18`:

- rows 0–1: `[2·r + c] == { 8·r, 8·(c+1) }` for `c ∈ 0..1`
- rows 2–7: `[4 + 4·(r-2) + c] == { 8·r, 8·c }` for `c ∈ 0..3`

The five grids hold 75 pairs (150 bytes) in total.

## Composition

For a given identity: its `SpriteList` target selects a record; the record gives the tile list and
the two offsets; the tile list gives the grid and the stream; walking the stream against the grid
yields the parts. Each part is a grid `(y, x)`, the x-flip toggle, and the raw tile byte. The record
offsets stay separate from the part coordinates — the renderer sums the descriptor position, the
record offset, and the grid pair through a carry chain (`add` / `adc`, `tetris.asm:6783`–`6786` for
Y, `:6808`–`6811` for X), so the three terms are not pre-folded.

Worked examples (hand-traced against the source):

| Identity | Offsets | Parts |
|---|---|---|
| `L_0` (`$00`) | `(-17, -16)` | 8 leading `$FE` skips, then `{16,0,·,$84} {16,8,·,$84} {16,16,·,$84}`, a skip, `{24,0,·,$84}` |
| `DIGIT_0` (`$20`) | `(0, 0)` | one part `{0,0,·,$00}` |
| `BURAN` (`$2C`) | `(-32, -16)` | 28 parts; part 0 `{0,8,·,$C0}` (the notched grid head) |
| `CRYING_LARGE_MARIO_1` (`$2E`) | `(-16, -16)` | part 2 is `{0,16,flip,$05}` (the mid-row `$FD`) |
| `ROCKET_S` (`$5A`) | `(-16, -8)` | `{0,0,·,$A8} {0,8,flip,$A8} {8,0,·,$A9} {8,8,flip,$A9} …` |

## Piece rotations

The first 28 identities are the four rotations of each tetromino, in `PieceKind` order
(`tetris.asm:6859`–`6865`):

```
dw $2C20, $2C24, $2C28, $2C2C ; 00 L
dw $2C30, $2C34, $2C38, $2C3C ; 04 J
dw $2C40, $2C44, $2C48, $2C4C ; 08 I
dw $2C50, $2C54, $2C58, $2C5C ; 0C O
dw $2C60, $2C64, $2C68, $2C6C ; 10 S
dw $2C70, $2C74, $2C78, $2C7C ; 14 Z
dw $2C80, $2C84, $2C88, $2C8C ; 18 T
```

A piece's identity byte is `kind·4 + rotation` (see the `Piece` section of
[`core-enums.md`](core-enums.md)), and that byte is used **directly as the `SpriteList` index**
(`tetris.asm:6728`). So `Piece::of(kind, rotation).raw` equals the `SpriteId` value of the matching
rotation sprite, and the sprite at index `kind·4 + rotation` is that piece rotation.

### `PieceKind` — `include/kirpich/piece_kind.h`

`enum class PieceKind : uint8_t { L=0, J=1, I=2, O=3, S=4, Z=5, T=6 }`. The order is not chosen — the
kind is the sprite-list index of the piece's sprites, grouped four-to-a-shape in this order. The
grouping comments were corroborated against the tile grids: index 0 (`Sprite_2C20`) draws an L
rotation, index 4 (`Sprite_2C30`) a J. There is no "no piece" value; every valid piece byte (`raw`
`0..27`) decodes to one of the seven. `Piece::kind()` (`include/kirpich/piece.h`) returns
`PieceKind(raw >> 2)`.

## Rocket-tier link

The three ranked rockets `ROCKET_L` / `ROCKET_M` / `ROCKET_S` (`$58` / `$59` / `$5A`) are the sprites
the Type-A game-over screen shows by score. The bonus-ending ladder selects them
(`GameState_0D`, `tetris.asm:6894`–`6896`); the scoring surface stores those tiers as `SpriteId`
values (see [`scoring.md`](scoring.md)).

## Renderer walk (consumer context)

`_RenderSprites` (`tetris.asm:6687`–`6856`) copies a 7-byte descriptor from `$FF86` (`:6719`–`6726`):
a visibility byte (`$80` renders the sprite hidden by forcing Y past the bottom of the screen,
`:6696`, `:6829`–`6833`), Y, X, the sprite index, two OR-merged attribute bytes, and the base OAM
flags. It doubles the index with `rlca` (`:6729`, upstream-commented "Bug?", harmless for the valid
`$00`–`$5D` range) to look up the record, then walks the tile stream and grid. Y/x flip flags on the
descriptor (bits 6 and 5) make the placement subtractive (`:6788`–`6796`, `:6813`–`6821`). The final
OAM attribute byte is the descriptor's attributes OR-merged with the stream's `$FD` toggle
(`:6841`–`6850`).

No background-priority bit appears anywhere in the sprite data; the only data-side attribute effect
is the `$FD` x-flip toggle. Descriptor fields, the flip math, and the index doubling belong to the
renderer and are not ported by this unit.

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_sprites.py`, `--all`): `include/kirpich/sprite_id.h`
  (the 94-enumerator enum), `src/data/generated/sprites_data.inc` (the composed `kSprites` table), and
  `tests/fixtures/sprites_expected.h` (the raw serialization — targets, record rows, concatenated
  streams, grid pairs — independent of the composed surface). Regenerate after any upstream repin; do
  not hand-edit.
- **Hand-written port-design:** `src/data/sprites.h` (`SpritePart`, `Sprite`, `getSprite`),
  `src/data/bounded_vec.h` (the fixed-capacity part container), and `include/kirpich/piece_kind.h`
  (`PieceKind` — no upstream symbol table exists to transcribe, so this document is its authority).

### Transcription asserts

`parse_sprites.py` hard-errors (with a `file:line` citation) on any of: a record or tile-list label
whose address suffix disagrees with its computed section address; a record that is not
`{ dw SpriteTiles_*, db, db }` or whose offset is outside the audited range; a tile list not
referenced 1:1; a stream that does not end in a single `$FF`, a `$FF` before the end, a `$FD` not
followed by a real tile, or a tile byte `>= $FD`; a stream that consumes more grid pairs than its
grid holds; a grid label out of order, an odd byte count, or a byte that is not a multiple of 8 or
exceeds `$38`; a `SpriteList` that is not 94 entries, a shared-target set other than the four
expected, or a target with no record; and the corpus totals (90 records, 90 lists, 877 stream bytes,
462 tiles, 52 `$FD`, 273 `$FE`, 90 `$FF`, max 28 parts, 75 grid pairs) not matching.

## Tested by

`tests/test_sprites.cpp` re-composes every identity from the raw fixture (the escape state machine
and grid walk, test-side) and compares it field-for-field against `kSprites` for all 94 ids; it also
pins the `SpriteId` boundary values, the signed offset decode, the four aliased identities' equality
by value, the piece-rotation ↔ `Piece` encoding, hand-picked x-flip resolutions, the five grids'
closed-form geometry, and the rocket-tier `SpriteId` link. The parser's own structural checks live in
`tools/asm_parser/test_parse_sprites.py`.
