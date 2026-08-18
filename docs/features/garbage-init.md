# Garbage-fill tables

A Type B game starts with the bottom of the field pre-filled with "garbage" — rows of blocks broken
by gaps that the player digs out to win. This unit ports the data behind that: the fixed table the
attract-mode demo stamps, the constants the procedural fill and its three start paths read, and the
fill itself — the random block-or-gap pick, the guarantee of a gap in every row, and the walk that
writes the rows.

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

## The fill

| Surface | Where | Shape |
|---|---|---|
| `registerGarbageFold` | `src/vm/garbage_fill.h` | `Routine<uint8_t()> (retropp::Vm&)` |
| `initGarbage` | `src/vm/garbage_fill.h` | `void (GameContext&, const function<uint8_t()>&, size_t topRow)` |
| `initDemoGarbage` | `src/vm/garbage_fill.h` | `void (GameContext&)` |
| `makeInitGarbageHook` | `src/vm/garbage_fill.h` | `InitGarbageHook (function<uint8_t()>)` |
| `typeBGarbageTopRow` | `src/vm/garbage_fill.h` | `constexpr size_t (uint8_t height)` |

The per-cell pick lives on the SM83 VM (`src/vm/garbage.asm`); everything around it is native. The
split is the same one the piece randomizer uses, and for the same reason: the divider the pick reads
keeps advancing while the pick runs, so which of the eight block tiles a cell becomes depends on that
advancement. A native read would freeze it.

**Decision — the fill takes the pick as a plain callable.** `initGarbage` accepts a
`std::function<std::uint8_t()>`, not the VM routine handle. A `Routine` converts to one, so production
wiring is unchanged, and the tests can substitute a stub pick to drive the one-gap-per-row rule and the
row extents exactly. The divider cannot be written from the VM's public surface, so without this the
forced-gap path would be unreachable from a test.

**Decision — both routines register on one VM.** The original has a single divider and a round init
draws pieces and then fills garbage in the same frame, so the draws advance the divider the fill reads.
Routines registered on one `Vm` share its machine, which keeps that coupling; a second `Vm` would give
the fill its own divider. Nothing in the types enforces it, so it is stated at the header, in the
contract, and pinned by a test that shows an extra draw before a fill changes the resulting field.

**Decision — the row count sets where the fill starts, not how many rows it writes.** The fill runs to
the bottom of the field regardless. For a Type B start the two readings agree; for a multiplayer round
start they do not, and its count of six covers ten rows. Reproduced as the code has it, recorded in the
contract.

**Accepted divergence.** The native work between cells burns no VM cycles, so the divider does not
advance across it as it does on the original. A filled field is therefore not the field the original
hardware would produce from the same starting divider. Every structural rule holds and nothing depends
on the exact sequence; reproducing it would additionally require the divider entering the fill to
match, which depends on the whole preceding frame timeline.

## Not here yet

The multiplayer garbage *attack* — sending lines between players — is serial-protocol behavior and is
its own unit. What consumes the multiplayer round-start fill is the multiplayer work; this unit ships
and tests the fill, and wires only the Type B seam the round init calls.
