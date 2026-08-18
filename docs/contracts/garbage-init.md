# Contract — Garbage init

Reverse-derived behavioral contract for Kirpich's garbage: the fixed table the attract-mode demo
stamps into a Type B field, and the constants the procedural garbage fill and its three start paths
consume. Every value here is transcribed from the `kaspermeerts/tetris` disassembly (upstream
`b95c668`); the line anchors below are the authority the tests check against.

A Type B game starts with the bottom of the playing field pre-filled with **garbage** — rows of block
tiles broken by gaps, which the player digs out from under to win. The garbage is produced two ways.
During the attract-mode demo it must be identical every time (the recorded inputs have to line up), so
the game stamps a fixed 40-byte table. Everywhere else it is filled **procedurally**, using the DIV
register as a random source. This unit ports the one data table and the constants the fill and its
call sites read; the fill routine itself is timing- and RNG-dependent and ports with the gameplay
logic.

---

## The demo garbage table

`TypeBDemoGarbage` (`tetris.asm:4316-4320`) is **4 rows of 10 cells**, one `db` line per row:

| Row | Bytes | Anchor |
|---|---|---|
| 1 | `$85 $2F $82 $86 $83 $2F $2F $80 $82 $85` | `tetris.asm:4317` |
| 2 | `$2F $82 $84 $82 $83 $2F $83 $2F $87 $2F` | `tetris.asm:4318` |
| 3 | `$2F $85 $2F $83 $2F $86 $82 $80 $81 $2F` | `tetris.asm:4319` |
| 4 | `$83 $2F $86 $83 $2F $85 $2F $85 $2F $2F` | `tetris.asm:4320` |

Every cell is either the **empty tile** `$2F` (the character map's space — `charmap.asm:42`) or one of
the **eight block tiles** `$80`–`$87`. Every row contains at least one `$2F`: the data mirrors the
"leave at least one gap per row" rule the procedural fill enforces at runtime, so a Type B field is
always clearable. The table is 10 wide because the playing field is 10 wide (see
[`playing-field.md`](playing-field.md)); it is not a separate width.

## The constants

| Constant | Value | Source | Anchor |
|---|---|---|---|
| `kTypeBDemoGarbageRows` | 4 | `ld c, 4` in the demo stamp (equals the table's `db` row count) | `tetris.asm:4289` |
| `kTypeBGarbageRowsPerHeight` | 2 | the `-2 * $20` offset at the Type B start (two BG-map rows per height level) | `tetris.asm:4230` |
| `kMultiplayerRoundStartGarbageRows` | 6 | `ld a, 6` at both multiplayer round starts | `tetris.asm:1123`, `:1135` |
| `kGarbageBlockTileBase` | `$80` | `or a, $80` in the fill's tile pick (and its `ld a, $80` block arm) | `tetris.asm:4350`, `:4336` |
| `kGarbageBlockTileCount` | 8 | `and a, $07` mask, plus one | `tetris.asm:4349` |
| `kGarbageEmptyTile` | `$2F` | `ld a, " "` resolved through the character map | `tetris.asm:4342` + `charmap.asm:42` |

## The three start paths

The garbage is written by three call sites, each handing the fill (or the demo stamp) a destination,
a row count, and a per-row climb offset.

| Path | Rows | Destination | Offset | Anchor | Notes |
|---|---|---|---|---|---|
| **Type B start** | `hTypeBStartHeight` × 2 (height 1–5; 0 skips) | `$9A02` (field row 16) | `-2 * $20` | `tetris.asm:4228-4232` | The fill climbs `2·(h−1)` rows from the base, then fills down — rows `18 − 2h` .. `17`. |
| **Demo** | fixed 4 rows | `$99C2` (field row 14) | — (fixed table) | `tetris.asm:4222-4225`, `:4286-4314` | When `hDemoNumber` is non-zero the game stamps `TypeBDemoGarbage` instead of filling. The four rows at row 14 are the same extent as a procedural height-2 start. |
| **Multiplayer round start** | 6 | `$C9A2` (WRAM buffer) | `-$20` | first round `:1121-1126`, later rounds `:1133-1138` | MASTER only; writes the WRAM buffer, not the BG map. |

The demo stamp (`InitDemoGarbage::`, `tetris.asm:4286-4314`) copies the table cell by cell to `$99C2`,
10 columns (`ld b, 10`) across 4 rows (`ld c, 4`), dual-writing each cell to the `+$30`-high-byte
buffer mirror, advancing `$0020` bytes per row.

The multiplayer garbage **attack** — the send/receive of lines between players and the on-screen
gauge — is serial and gameplay behavior, not part of this unit; it is cross-referenced here only so
the start-of-round garbage above is not confused with it.

## The procedural fill

`InitGarbage::` (`tetris.asm:4324-4403`) writes `A` rows of garbage upward from `hl`. Its mechanism —
ported later with the gameplay logic, recorded here as the specification:

- **DIV as RNG.** It reads `rDIV` twice (`tetris.asm:4333`, `:4348`). The first read drives a
  deliberately wasteful countdown loop that lands on `$80` (a block) or `$2F` (a gap) with a 50/50
  chance; the source notes this "could have been done much more easily with a BIT test" and that it is
  "completely deterministic as no interrupts fire" during the loop. When a block is chosen, the second
  read picks the tile: `rDIV & $07 | $80`, i.e. one of `$80`–`$87` (`tetris.asm:4349-4350`).
- **Ensure at least one gap.** At the rightmost cell of a row (low nibble `$0B`, `tetris.asm:4359`),
  if nothing empty was placed on that row, the rightmost cell is forced to `$2F`. The source calls out
  the "1 in 512 chance we picked no empty blocks at all for this line."
- **Dual write.** Each cell is written to the BG map and again `$3000` bytes on, to the WRAM buffer
  mirror (`tetris.asm:4377`) — except in multiplayer, where `hIsMultiplayer` skips the second write
  (`tetris.asm:4374-4376`).
- **Row and end.** A row is 10 wide: the end-of-row nibble check is `$0C` (`tetris.asm:4386`) and the
  next row starts `$20 - 10` bytes on (`tetris.asm:4395`). The fill stops at the bottom of the field
  (`h` low nibble `$0A` and `l == $2C`, `tetris.asm:4392`, `:4401`).

---

## How the port implements the fill

The fill is split across the SM83 VM and native code, the same way the piece randomizer is
(see [`piece-random.md`](piece-random.md)).

**On the machine** (`src/vm/garbage.asm`): the per-cell pick — `tetris.asm:4332-4351`, from the first
divider read through the tile choice. Its whole product is one cell's tile in `A`.

**Native** (`src/vm/garbage_fill.{h,cpp}`): everything else — where each cell goes, the
one-gap-per-row rule, the write, the walk up the field, and the stopping condition.

### Why the pick runs on the machine

The countdown between the two divider reads runs a data-dependent number of iterations, and the
divider keeps incrementing while they run. The second read therefore sees a different byte than the
first, and how different depends on how long the countdown ran — so which of the eight block tiles a
cell becomes is a function of the first read as well as the second. Reading a frozen byte twice in
native code would collapse that. Running the pick on the machine reproduces it by construction.

### One machine, one divider

The original has a single divider, and the piece randomizer reads it too
(see [`piece-random.md`](piece-random.md)). Those two readers interleave: a round init draws pieces and
then fills the starting garbage within the same frame, so the draws advance the divider the fill goes
on to read, and the field that comes out depends on how many pieces were drawn before it.

The port keeps that coupling by registering both routines on the **same** VM — routines registered on
one machine share it, so the piece draws move the divider the fill reads, exactly as on the original.
Registering them on separate machines gives each its own divider and silently drops the relationship;
nothing in the types prevents it, so it is a wiring requirement rather than a guarantee. The tests pin
the coupling directly: with both routines on one machine, an extra piece draw before a fill changes
the field the fill produces.

### What the port does not reproduce

The native work between cells — address arithmetic, the one-gap check, the writes — burns no cycles
on the VM, so the divider does not advance across it. On the original it does. Over a whole fill that
difference accumulates, and past the first few cells the two diverge.

**The mechanism is ported; the byte stream is not claimed.** A field this fill produces is a correct
Type B field — every structural rule below holds — but it is not the field the original hardware
would produce from the same starting divider, and nothing in the port depends on it being so.

Reproducing the byte stream exactly would additionally require the divider's value *entering* the
fill to match the original, which depends on the whole preceding frame timeline; the extra precision
inside the fill would have nothing to be exact against.

### What the port does reproduce

- The gap-or-block choice as the first divider read's parity, including the read-of-zero case that
  runs 256 countdown steps rather than none.
- The second read on the block branch only, masked to the eight block tiles.
- The intra-call divider advancement described above.
- Every structural rule: the block-tile domain, one gap per row guaranteed, the row extents per start
  path, and the stopping condition.

## Where the port writes: the board, not video RAM

The original writes each cell twice — once at the pointer it is walking and once `$3000` bytes on
(`tetris.asm:4371`, `:4381`) — except in multiplayer, where `hIsMultiplayer` skips the second
(`:4374-4376`). One of those two addresses is always in the background map and the other is always in
the work-RAM board at `$C800`, which is the copy the port models (see
[`playing-field-state.md`](playing-field-state.md)). The background-map write is presentation and is
left to the render bridge.

| Start path | Original's destination | Board cell the port writes | Field row |
|---|---|---|---|
| Type B start | `$9A02` + the `$3000` mirror | `$CA02` | 16 |
| Demo stamp | `$99C2` + the `$3000` mirror | `$C9C2` | 14 |
| Multiplayer | `$C9A2` (no mirror) | `$C9A2` | 13 |

All three land on the board, at field column 0. So the multiplayer branch is **not** a special case in
the port: the write the original skips there is the one the port does not make anywhere. This is the
one place the port's shape reads differently from the original's, and it is why `hIsMultiplayer` has
no role in the ported fill.

Cells are addressed through the board's own field accessor rather than by address arithmetic; the
three anchors above are the ones `tests/test_playing_field_state.cpp` already pins.

## The row extents

The fill takes a row count, a starting cell, and a per-row climb. It climbs `count - 1` rows from the
start, and then fills downward until it runs off the bottom of the field (`tetris.asm:4324-4330`,
terminating at `:4392`/`:4401`).

**The count chooses where the fill starts, not how many rows it writes.** Nothing in the fill counts
rows: once the climb is done, it writes every row from wherever it landed to the bottom of the field.
The two callers differ in whether that distinction shows.

| Start path | Count | Starting field row | Climb per row | Field rows filled | Rows written |
|---|---|---|---|---|---|
| Type B start | `hTypeBStartHeight` (1-5; 0 does not call) | 16 | 2 | `18 - 2h` .. 17 | `2h` |
| Multiplayer round start | 6 | 13 | 1 | 8 .. 17 | **10** |

For the Type B start the two readings agree: climbing `2(h-1)` rows from row 16 and then filling to the
bottom writes exactly `2h` rows, so the count reads naturally as a height. For the multiplayer round
start they do not: a count of 6 climbing one row at a time from row 13 lands on row 8, and the fill
then runs to row 17 — **ten rows, not six**. A multiplayer round therefore starts its board with the
same garbage extent a Type B height of five produces.

Whether the original intended six is not knowable from the source, and the port does not guess: it
reproduces what the code does. The upstream disassembly is itself unsure what this buffer becomes —
its comment at `tetris.asm:4376-4378` asks whether the multiplayer path writes a buffer that is copied
to the field later — so the consumer side is an open question for the multiplayer work, not this fill.

The original expresses the stopping condition as a pair of address-nibble compares that happen to
match both the background-map and work-RAM address ranges (`h & $0F == $0A`, then `l == $2C`). On the
board it is simply "the last field row has been completed", and that is how the port spells it.

## The one-gap-per-row flag shares a byte

At the rightmost cell of a row, if no gap was placed on that row, the cell is forced to the empty tile
(`tetris.asm:4355-4366`) — the "1 in 512 chance we picked no empty blocks at all for this line" the
source calls out. The original tracks whether a gap has landed in `$FFA0`, setting it on the gap
branch (`:4354`) and clearing it at each row end (`:4388-4389`).

**`$FFA0` carries a second, unrelated meaning at other times.** It is also the completed-row count the
lock and line-clear paths use — see [`game-state-machine-state.md`](game-state-machine-state.md), where
it is the port's `completedRowCount`. The two uses never overlap: the fill runs to completion inside a
single frame, and every caller clears the byte immediately before calling
(`tetris.asm:4207-4208`, `:1121-1122`, `:1133-1134`).

The port keeps them apart. `completedRowCount` stays the field it already is, including the clear the
round init performs; the fill's gap flag is a local that lives only for the duration of the call. The
port derives it from the answer the machine returns — the empty tile means a gap — rather than
carrying a separate flag byte.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_garbage.py`, `--all`):
  `src/data/generated/garbage_data.inc` (the six constants + the composed 4 × 10 grid) and
  `tests/fixtures/garbage_expected.h` (the flat 40 bytes in serialization order, independent of the
  composed grid). Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/data/garbage.h` — includes the generated file at namespace scope.
  Header-only; the cells are raw `uint8_t` (the same tile-index space as the tilemaps), so there is no
  enum, no struct, and no translation unit.

The destination and buffer-mirror addresses, the `$20` row stride, and the fill's nibble checks
describe the DMG memory map and the fill's mechanism — neither of which the port reproduces — so they
are pinned inside the parser and recorded here, but do not appear in the port surface.

### Transcription asserts

`parse_garbage.py` hard-errors (with a `file:line` citation) on any of: `TypeBDemoGarbage` missing,
or not exactly 4 `db` rows of exactly 10 bytes; any cell outside `{$2F} ∪ [$80, $87]`, or any row
with no `$2F`; the demo stamp missing its `$99C2` destination, the `TypeBDemoGarbage` source, `ld c, 4`,
the inner `ld b, 10`, the `add a, $30` buffer switch, or the `$0020` row stride; not exactly three
`call InitGarbage` sites, or a site whose destination is neither the Type B (`$9A02`) nor multiplayer
(`$C9A2`) address; the Type B site missing its `ld a, b` height load or its `-2 * $20` offset; not
exactly two multiplayer sites, or unequal multiplayer row counts; a missing `call InitDemoGarbage`
demo branch; and any of `InitGarbage`'s mechanism anchors — the two `ldh a, [rDIV]` reads, the
`ld a, $80` block arm, the `ld a, " "` empty arm, `and a, $07`, `or a, $80`, the `cp a, $0B` rightmost
check, the `cp a, $0C` row-end nibble, `ldh a, [hIsMultiplayer]`, the `ld de, $3000` buffer offset,
the `ld de, $20 - 10` row wrap, and the `cp a, $0A` / `cp a, $2C` termination pair.

---

## Tested by

`tests/test_garbage.cpp` — the six constants pinned against this contract; the full 40-cell sweep of
the composed grid against the flat fixture; the cell domain and the one-gap-per-row invariant across
the whole table; the empty tile shown to equal the character map's space glyph; and the four corners
pinned to concrete bytes. The parser's own structural checks
(`tools/asm_parser/test_parse_garbage.py`) guard the scan against upstream changes.

`tests/test_garbage_fill.cpp` — the fill: the pick's parity relation against the traced vectors and
its answers shown to lie in the cell domain with both arms reached; determinism across a machine reset
and the intra-call divider advancement; the demo stamp checked cell for cell against the table with
the rest of the board untouched; the Type B extents swept over every height and the multiplayer extent
shown to be ten rows; the one-gap-per-row rule with the forced cell isolated, checked at every column
a gap could land in and again over the real pick; the cell domain across a whole fill; the divider
shared with the piece randomizer (an extra draw before a fill changes the field); and both paths of
the round-init seam.
