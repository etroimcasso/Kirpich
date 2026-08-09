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

**This whole mechanism ports with the gameplay logic**, against this specification. This unit provides
only the demo table and the constants above.

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
