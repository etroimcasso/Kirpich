# Playing-field state

The board the game actually plays on — the 32×32 tile grid the original keeps at `$C800`, the
authoritative copy of the field that collision reads, piece locking writes, line clears scan, and garbage
fills — ported as one hand-written C++ struct, together with the 10-byte staging row a multiplayer attack
is built in. Where the global game state holds the score and the OAM buffer, and the other state blocks
hold the main-loop, sprite, serial, demo, and high-score bytes, this holds the field itself.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `PlayingFieldState` | `src/state/playing_field_state.h` | the 32×32 `board` grid (raw tile cells), the 10-cell `attackRow`, a `fieldCell(row, col)` accessor into the visible field, and `reset()` |

The behavioral specification — the window geometry, the video-RAM mirror relation, the per-byte census
resolution, the boot note, and the full mechanism inventory with source anchors — is in
[`../contracts/playing-field-state.md`](../contracts/playing-field-state.md).

## Decisions

**One header-only struct, a sibling of the other state blocks.** `PlayingFieldState` is a plain struct
wrapping the board grid and the staging row with a `reset()` to the all-zero boot state; there is no
`.cpp`. It sits beside `EngineState` and the other state structs rather than inside them — each state
block is its own type, and combining them into the running game is later wiring.

**The board is the full 32×32 grid, not just the visible 18×10 field.** The game writes across the whole
background-map page: the walls run the full height, the floor sits a row below the field, a multiplayer
round parks incoming garbage in the rows beneath the floor, and startup clears every row. Modelling only
the visible field would strand those live cells, so the board carries all 32 rows of the `$20`-byte
stride. `board[row][col]` maps to `$C800 + row·32 + col`.

**Cells are raw tile indices.** Each cell is a `uint8_t` in the same tile space as the character map, the
static tilemaps, and the garbage table — no enum, matching the tilemap unit's cell choice. The empty cell
is the garbage unit's `kGarbageEmptyTile` (`$2F` = `CharTile::SPACE`), cross-referenced rather than
re-minted.

**The visible field gets one geometry accessor.** `fieldCell(row, col)` returns
`board[row][kPlayingFieldOriginCol + col]` over the field extent the playing-field data unit already
pins (18 × 10, origin column 2). The floor row, wall columns, and other landmarks stay closed-form
relations recorded in the contract and pinned by the tests — not constants.

**The staging row is one field-width array.** `attackRow` is a 10-cell `std::array`, matching the
`$C400` window the multiplayer attack builds ten bricks in and punches a hole in. Its build, hole, and
insert logic is multiplayer gameplay that ports later; the struct carries only the row.

**One hand-entered wire constant.** `kAttackRowBrickTile` (`$28`) is the brick tile the staging row is
built from. It is a single verifiable scalar (the `ld a, $28` at the build site), hand-typed and
test-guarded — the same treatment the music unit's one stereo-table address received. The other stamped
tiles (the `$8E` walls and floor, the `$80` floor-restore) belong to the title and round mechanisms that
port later and stay contract-recorded only.

**The video-RAM mirror is mechanism, not state.** The original keeps a second copy of the field in the
background map and mirrors the two at a fixed `+$3000` offset as it writes. The port's board is the one
authoritative copy; presenting it into the engine's background map is the render bridge's job. The
contract records the relation and the wipe fixture pins it row-by-row.

## Keeping it honest

The struct does not mirror the board's memory image, so its fidelity is checked against the existing
fixtures — this unit adds no parser work, the fifth state unit without any. The work-RAM census
already records every byte both windows reach; the test proves each resolves to exactly one owner (the
staging row, or a cell on the board grid), with the corner refCounts pinned and `$CFFF` excluded above
the board. The playing-field wipe fixture pins the geometry: the struct's own row math reproduces every
wipe address, and the `wram − vram == $3000` mirror relation holds on all 18 triples. An upstream repin
that moved either window surfaces as a failure. See
[`../engine/playing-field-state.md`](../engine/playing-field-state.md) for how to use it.

## Not here yet

The state block is the board; the code that reads and writes it — collision detection, piece locking, the
line-clear scan and compaction, the row-at-a-time wipe, the procedural and demo garbage fills, the
between-round overlay screens that stream tilemaps into the field window, and the whole multiplayer attack
pipeline that builds the staging row and inserts it under the opponent's stack — is gameplay and
presentation work that builds on this struct in later phases. This unit provides the board struct, its
lifecycle, and the existing fixtures to check it against.
