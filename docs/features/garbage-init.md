# Garbage-fill tables

A Type B game starts with the bottom of the field pre-filled with "garbage" — rows of blocks broken
by gaps that the player digs out to win. This unit ports the data behind that: the fixed table the
attract-mode demo stamps, and the constants the procedural fill and its three start paths read. It is
data only — nothing here fills a row or reads the random source; that is gameplay logic and ports
later.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `kTypeBDemoGarbage` | `src/data/garbage.h` | `array<array<uint8_t, kPlayingFieldCols>, 4>` |
| `kTypeBDemoGarbageRows` | `src/data/garbage.h` | `uint8_t` = 4 |
| `kTypeBGarbageRowsPerHeight` | `src/data/garbage.h` | `uint8_t` = 2 |
| `kMultiplayerRoundStartGarbageRows` | `src/data/garbage.h` | `uint8_t` = 6 |
| `kGarbageBlockTileBase` | `src/data/garbage.h` | `uint8_t` = `0x80` |
| `kGarbageBlockTileCount` | `src/data/garbage.h` | `uint8_t` = 8 |
| `kGarbageEmptyTile` | `src/data/garbage.h` | `uint8_t` = `0x2F` |

The demo garbage is a fixed 4 × 10 table: the demo replays recorded inputs, so its garbage cannot be
random. Each cell is a raw tile index — the empty tile or one of the eight block tiles — and every row
leaves at least one gap. The constants describe how the *procedural* fill is parameterized: two rows
of garbage per Type B start height, six rows at each multiplayer round start, and the block-tile range
it draws from. The exact write addresses and the fill's mechanism are pinned in
[`../contracts/garbage-init.md`](../contracts/garbage-init.md).

## Decisions

**Cells are raw bytes, not an enum.** A garbage cell is a tile index in the same space the tilemaps
use — a range of block tiles plus the space glyph — with no upstream names, so there is nothing to
mint an enum from. The cells stay `uint8_t`, following the tilemaps precedent. The one identity worth
pinning — that the empty tile *is* the character map's space — is asserted in a test rather than by
typing a named value into the table.

**The table is the composition; the stamp is not ported.** What survives the platform change is the
grid of tiles. The routine that copies it into the field (its destination address, its buffer
mirroring, its row stride) is the Game Boy's memory map and its drawing loop — mechanism the port does
not reproduce — so it is recorded in the contract with line anchors and ports with the demo player,
not here.

**Width is the field's width, not a new constant.** The table is 10 wide because the playing field is
10 wide. It reuses `kPlayingFieldCols` rather than minting a second width constant that would have to
be kept in step with it.

**Header-only, no struct, no translation unit.** The unit is six constants and one grid, so it is a
single header including the generated file at namespace scope. There is no `.cpp`.

## Keeping it honest

The constants, the grid, and the test fixture are generated from the disassembly by
`tools/asm_parser/parse_garbage.py`, which reads the table and the fill's call sites by their shape —
the demo stamp's destination and counts, the three `call InitGarbage` sites and their offsets, and the
fill's own mechanism instructions — and stops with a citation if any of them has moved, rather than
emitting a wrong file. It checks that the table is 4 rows of 10 cells with every cell in range and
every row holding a gap, that the demo stamp's row and column counts agree with the table, that there
are exactly one Type B and two (equal) multiplayer start sites, and that the fill still reads the DIV
register twice, masks to the block range, forces a gap at the rightmost cell, and skips the buffer
write in multiplayer. The fixture holds the raw 40 bytes independent of the composed grid, so the test
sweep compares the grid against source values rather than against itself. See
[`../engine/garbage-init.md`](../engine/garbage-init.md) for how to regenerate it.

## Not here yet

The procedural fill — the random block/gap pick, the ensure-one-gap rule, the buffer mirroring, and
the loop that walks up the field — is timing- and RNG-dependent gameplay logic and ports with that
code. The demo stamp ports with the demo player. The multiplayer garbage *attack* (sending lines
between players) is serial-protocol behavior and is separate again. The contract records all of it so
those systems have a specification to build against; this unit gives them the table and the constants
to build on.
