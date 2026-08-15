# Contract — Playing-field state

Reverse-derived behavioral contract for `PlayingFieldState` (`src/state/playing_field_state.h`): the
board the original game keeps at `$C800` — the 32×32 background-map shadow that is the authoritative copy
of the field — plus the 10-byte multiplayer attack-staging row at `$C400`, ported as one C++ struct.
Every address, use site, and adjudication below is from `tetris/tetris.asm` (upstream `b95c668`); both
windows are anonymous gaps in `tetris/wram.asm` (`$C400` and `$C800` fall inside the `ds $D000 - $C400`
reservation and are reached only by literal addresses in the code). The line anchors are the authority
the tests check against.

`PlayingFieldState` is the single authoritative copy of the board. The original keeps a second copy in
video RAM and mirrors the two as it writes; that mirroring is engine mechanism (below), not state. The
struct carries the whole 32×32 grid, not only the visible 18×10 field, because the game writes across the
entire page: the walls span the full height, the floor sits below the field, a multiplayer round parks
garbage beneath the floor, and startup clears every row.

---

## Window geometry

The board is the background-map shadow at `$C800`: a 32×32 tile grid over the `$C800`–`$CC00` window
(`($CC00 − $C800) / $20 = 32` rows of `$20` = 32 cells, `sizeof == $400`). `board[row][col]` is the cell
at `$C800 + row·$20 + col`.

The visible field is the top-left interior: `kPlayingFieldRows` × `kPlayingFieldCols` = 18 × 10 cells,
its top-left cell at `$C802` = `board[0][kPlayingFieldOriginCol]` (`kPlayingFieldOriginCol` = 2, from
`$C802 − $C800`). `FillPlayingFieldAndWipe` walks exactly this extent — `ld hl, $C802`, `ld c, 18`
(height), inner `ld b, 10` (width), row stride `$0020` (`tetris.asm:5044-5058`) — which fixes the field
origin and dimensions. `fieldCell(fieldRow, fieldCol)` returns `board[fieldRow][2 + fieldCol]`.

The landmarks outside the visible field are closed-form relations, pinned by the tests rather than named
by constants:

| Landmark | Address(es) | Board cell | Anchor |
|---|---|---|---|
| Field top-left | `$C802` | `board[0][2]` | `tetris.asm:5045` |
| Wall columns | `$C801` / `$C80C` | col 1 / col 12, all 32 rows | `tetris.asm:545-548`, `Call_26A9` `6255-6264` |
| Floor row | `$CA41` | `board[18][1]`, 12 cells | `tetris.asm:549-555` |
| Floor-restore row | `$CA42` | `board[18][2]`, 10 cells | `tetris.asm:1544-1550` |
| Line-clear scan start | `$C842` | `board[2][2]` = `fieldCell(2,0)` | `tetris.asm:5310` |
| Field bottom row | `$CA22` | `board[17][2]` = `fieldCell(17,0)` | `tetris.asm:1344`, wipe `1341` |
| Rows-30/31 clear base | `$CBC2` | `board[30][2]` | `tetris.asm:5061-5075` |

## The video-RAM mirror is mechanism, not state

The original keeps the field in two places: this `$C8xx` shadow and the background map in video RAM at
`$98xx`. The two are one fixed offset apart — `wram − vram == $3000` — and the code moves between them by
adding `$30` to the address high byte. `_LookupTile` computes a `$98xx` address and `DetectCollision`
adds `$30` to read the shadow (`tetris.asm:6042-6047`); `LockPieceIntoBackground` writes the tile to the
`$98xx` map during HBlank and again to the `$C8xx` shadow (`6083-6098`); the 18 wipe routines copy each
field row from the shadow to the map (the wipe schedule). The port inverts the arrangement: the struct's
`board` is the one authoritative copy, and presenting it into the engine's background map is the render
bridge's job. The `+$3000` relation, the `+$30` high-byte adds, `_LookupTile`'s divide-by-8, and the
HBlank gating are DMG memory-map mechanism the port does not replicate; the contract records the relation
and the wipe fixture pins it row-by-row (`wram − vram == $3000` on all 18 wipe triples).

---

## The attack staging row

`$C400` is a single field-width staging row for a multiplayer attack — the row inserted under the
opponent's stack when lines are sent. It is a complete, closed mechanism reached at exactly three sites
(census refCount 3):

- **Build** (`tetris.asm:1025-1031`): `ld hl, $C400`, `ld b, 10`, `ld a, $28`, `ldi` loop — ten
  `kAttackRowBrickTile` (`$28`) bricks. This is the one hand-entered wire scalar in the unit.
- **Punch the hole** (`tetris.asm:1651-1667`): the `.wrap` walk indexes `$C400 + (wPieceList[$FF] mod
  10)` and writes a `" "` (space) hole; the mod-10 wrap keeps every access inside `+0…+9`. The piece-list
  index is a `wPieceList` (`$C300`) cross-reference to the engine-state unit.
- **Insert** (`tetris.asm:1908-1959`): shifts the opponent's field up from `$C822` (`board[1][2]`) and
  copies the `$C400` row into each vacated bottom row, then arms the wipe (`hWipeCounter = $02`).

The port models the row as `attackRow`, `std::array<uint8_t, kPlayingFieldCols>` (10 cells, matching the
shipped census window `$C400`–`$C40A`). The build/hole/insert logic is multiplayer gameplay that ports
with that code.

---

## Per-byte census resolution

The unit adds no parser work. The work-RAM census (`tests/fixtures/wram_expected.h`) already carries
every byte both windows reach; each resolves to exactly one owner here:

- **`[$C400, $C800)`** — exactly one census row, `$C400` (refCount 3, the three staging sites above).
  Every access lands on `attackRow[0..9]`.
- **`[$C800, $CC00)`** — exactly 32 census rows, each decomposing to `row = (addr − $C800) >> 5 <
  kBoardRows`, `col = (addr − $C800) & $1F < kBoardCols`; all 32 land on the board grid. Corner
  refCounts: `$C802` 6, `$C822` 3, `$C842` 2, `$C9A2` 3, `$CA22` 3, `$CA41` 1, `$CA42` 2, `$CBC2` 1.
- **`[$CC00, $D000)`** — no board census row; the only address is `$CFFF`, the stack top / boot marker
  the audio-state unit owns.

### Garbage anchors within the grid

The garbage table stamps into these board rows (each at column 2, the field origin):

| Anchor | Address | Board row | Relation |
|---|---|---|---|
| Type B fill base | `$CA02` | 16 | field bottom minus one |
| Demo-garbage stamp | `$C9C2` | 14 | `kPlayingFieldRows − kTypeBDemoGarbageRows` = 18 − 4 |
| Multiplayer buffer | `$C9A2` | 13 | — |

The empty cell is `kGarbageEmptyTile` (`$2F` = `CharTile::SPACE`), shared with the garbage table and the
character map; the unit does not re-mint it.

---

## This unit owns zero HRAM

Every high-RAM byte the board mechanisms touch is call-transient and already adjudicated by an earlier
unit — recorded here as a cross-note, given no field:

- `$9B` (collision result), `$B1` (stack-height scratch), `$B2`–`$B5` (the `_LookupTile` interface) are
  renderer/collision mechanism adjudicated with the sprite-renderer state.
- `hWipeCounter` is a `GameFlowState` field (the game-state-machine unit); the attack insert and the
  fills arm it, but it is not board state.

---

## Boot semantics

Boot is all-zero. The startup clear wipes `$C000`–`$CFFF` (the WRAM0 clear), so a default-constructed
`PlayingFieldState` — the whole board and the staging row all-zero — is the boot state, and `reset()`
returns a live instance to it. The `$2F` space-fill that makes the field playable, the `$8E` walls, and
the `$8E` floor are title-screen setup (`GameState_06`, `tetris.asm:538-555`), not boot; the floor
restore writes `$80` (`1545`) and is round mechanism. Those fills are gameplay/title work that runs on
the board in later phases, not initial state.

---

## Mechanism inventory (all deferred; none is state)

Every routine that reads or writes the board, with anchors — gameplay, presentation, or multiplayer work
that ports with its own phase:

| Mechanism | Anchors | Ports with |
|---|---|---|
| Title-init clear + walls + floor | `:538-555`, `Call_26A9` `:6255-6264` | game-flow / title |
| Collision read (`DetectCollision`, `+$30`) | `:6030-6065` | piece / collision |
| Piece lock dual-write (`LockPieceIntoBackground`) | `:6068-6106` | piece lock |
| Completed-row scan (`$C842` start, 16 of 18 rows) | `:5301-5345` | line-clear pipeline |
| Compaction + top-row clear + `$C7` termination | `:5498-5550` | line-clear pipeline |
| The 18 wipe row copies | wipe schedule | line-clear / game-over |
| `FillPlayingFieldAndWipe` | `:5039-5059` | line-clear |
| Danger-music stack scan (threshold 12 rows) | `Call_B9B` `:1727-1754` | audio / danger |
| Overlay streaming (`LoadPlayingFieldTilemap`, scoreboard, dancers, `$FF`→wipe) | `:4621-4638`, `:4722-4724`, `:6434-6457` | presentation |
| Game-over prints | `Call_1F7D` `$C843`/`$C983`, `:4932-4942` | presentation |
| Top-score staging prints (`$C9A4`/`$C9AC`) | adjudicated with the high-score state | high-score display |
| Multiplayer send / shift-below-floor / receive / floor restore | `:1321-1386`, `:1457-1500`, `:1543-1550` | multiplayer |
| Attack build / hole / insert | `:1025-1031`, `:1651-1667`, `:1908-1959` | multiplayer |
| Rows-30/31 clear (`Call_1FF2`) | `:5061-5075` | gameplay entry |

### Preserved quirks

Two upstream quirks are recorded verbatim and port with the code that carries them:

- **The line-clear scan starts at the third field row** (`$C842` = `board[2][2]`) and checks only 16 of
  the 18 rows (`ld b, 16`, `tetris.asm:5310-5311`, source comment "Start at the third row!? Why? Bug").
  The completed-row-check first-row/count pair (in the misc data) pins it data-side.
- **The compaction walks one row above the window base** — its up-loop terminates when the address high
  byte reaches `$C7`, a row above `$C800` (`tetris.asm:5531-5533`).

---

## The surface

- **Parser-emitted** (reused, no delta): `tests/fixtures/wram_expected.h` (the `$C400` and `$C8xx`
  census rows and the layout gap) and `tests/fixtures/playing_field_expected.h` (the 18 wipe triples).
  This is the fifth state unit with no parser work.
- **Hand-written port-design:** `src/state/playing_field_state.h` (the `PlayingFieldState` struct, the
  board/field/attack constants, `fieldCell`, and `reset()`), the tests, and this contract.

There is no behavioral code in this unit: collision, locking, the scan and compaction, the wipe, the
fills, the overlay streaming, and the multiplayer attack machinery are later phases built on this board.

---

## Tested by

`tests/test_playing_field_state.cpp` — the census-window resolution sweep (the lone `$C400` staging row,
the 32 board rows all landing on the grid, no census row above `$CC00` except `$CFFF`, and the corner
refCount pins); the geometry sweep against the wipe fixture (`wram − vram == $3000` and the struct's
own row math reproduces every fixture address); the `fieldCell` mapping over the full field extent with
the scan-quirk, field-bottom, and garbage-anchor landmark pins; the struct-shape pins (`attackRow`
width, `sizeof board`, defaulted `==`, all-zero boot); the reset-to-boot test; and the wire-value pins
(`kAttackRowBrickTile`, the empty-cell tie to `CharTile::SPACE`, the field origin column). The two
shipped census guards elsewhere (`tests/test_audio_state.cpp`'s whole-WRAM ownership guard, which already
draws the `$C400`–`$C40A` and `$C800`–`$CC00` windows) stay unchanged; this test refines the per-byte
resolution.
