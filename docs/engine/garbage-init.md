# Garbage

A Type B game starts with the bottom of the playing field pre-filled with "garbage" — rows of blocks
broken by gaps that the player clears to win. This page covers both halves: the data (the fixed table
an attract-mode demo stamps, and the constants that parameterize the fill) and the fill itself (the
random block-or-gap pick, the guarantee of a gap in every row, and the walk that writes the rows).

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/garbage.h` | The six constants and `kTypeBDemoGarbage` | Hand-written wrapper; the values are included from the generated file. |
| `src/data/generated/garbage_data.inc` | The six constants and the composed grid | **Generated — do not hand-edit.** |
| `tests/fixtures/garbage_expected.h` | The flat 40 bytes, for the test sweep | **Generated — do not hand-edit.** |
| `src/vm/garbage.asm` | The per-cell pick, as machine code for the VM | Assembled and baked into the binary at build time. |
| `src/vm/garbage_fill.{h,cpp}` | The fill, the demo stamp, the row geometry, and the seam | Hand-written. |

The data is in `namespace kirpich`, included as `"data/garbage.h"`; the fill is in
`namespace kirpich::vm`, included as `"vm/garbage_fill.h"` (the `src/` tree is on the library's
include path).

## Using it

```cpp
#include "data/garbage.h"

kirpich::kTypeBDemoGarbageRows;              // -> 4
kirpich::kTypeBDemoGarbage[0][0];            // -> 0x85  (row 0, column 0)

kirpich::kTypeBGarbageRowsPerHeight;         // -> 2   (rows of garbage per Type B start height)
kirpich::kMultiplayerRoundStartGarbageRows;  // -> 6   (rows at each multiplayer round start)
kirpich::kGarbageBlockTileBase;              // -> 0x80
kirpich::kGarbageBlockTileCount;             // -> 8   (block tiles are 0x80 .. 0x87)
kirpich::kGarbageEmptyTile;                  // -> 0x2F (the character map's space)
```

`kTypeBDemoGarbage` is a `std::array` of `kTypeBDemoGarbageRows` rows, each a `std::array` of
`kPlayingFieldCols` cells. It is row-major — `kTypeBDemoGarbage[row][col]` — with row 0 drawn topmost.
Every cell is a raw tile index: `kGarbageEmptyTile` for a gap, or a block tile in
`[kGarbageBlockTileBase, kGarbageBlockTileBase + kGarbageBlockTileCount)`. Every row holds at least one
gap. All seven names are `constexpr`, so they can be used at compile time.

## Filling a field

The per-cell pick runs on the SM83 VM, because the divider it reads keeps advancing while the pick
runs and which block tile a cell becomes depends on that. Register it and hand the result to the
fill:

```cpp
#include "vm/garbage_fill.h"

auto vm   = retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
auto draw = kirpich::vm::registerPieceRandom(vm);   // the piece randomizer — same vm
auto fold = kirpich::vm::registerGarbageFold(vm);   // the garbage pick — same vm

wiring.initGarbage = kirpich::vm::makeInitGarbageHook(fold);   // the round init's seam
```

**Register both routines on the same `Vm`.** There is one divider, and a round init draws pieces and
then fills garbage in the same frame, so the draws advance the divider the fill reads. Routines
registered on one `Vm` share its machine and keep that relationship; a second `Vm` gives the fill its
own divider and drops it. Nothing in the types enforces this.

Advance the VM one tick's worth of cycles per sim tick so the divider free-runs between fills, exactly
as the piece randomizer needs.

The pieces underneath the seam are usable directly:

```cpp
kirpich::vm::typeBGarbageTopRow(3);        // -> 12  (a height of 3 covers rows 12..17)
kirpich::vm::kDemoGarbageTopRow;           // -> 14
kirpich::vm::kMultiplayerGarbageTopRow;    // -> 8

kirpich::vm::initGarbage(game, fold, kirpich::vm::typeBGarbageTopRow(3));
kirpich::vm::initDemoGarbage(game);        // the fixed table, no VM involved
```

Two things to know about the extent. The fill always runs to the bottom of the field — a top row
chooses where it *starts*, not how many rows it writes — and a multiplayer round start covers ten
rows, not the six its row count suggests. And the fill writes only the board; mirroring the board into
video memory belongs to the render bridge.

One property is deliberately not reproduced: the divider does not advance across the native work
between cells, so a filled field is not the field the original hardware would produce from the same
starting divider. Every structural rule holds — the cell domain, a gap in every row, the row extents —
and nothing in the port depends on the exact sequence. The reasoning is in
[`../contracts/garbage-init.md`](../contracts/garbage-init.md).

## Regenerating the data

The constants, the grid, and the test fixture are produced from the disassembly by the parser.
Regenerate after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_garbage.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/garbage_data.inc \
  --fixture-out tests/fixtures/garbage_expected.h
```

The parser reads the demo table and the fill's call sites by their shape rather than by line number,
and stops with a citation if anything has moved: it checks that the table is 4 rows of 10 cells with
every cell either the empty tile or a block tile and a gap in every row, that the demo stamp's row and
column counts agree with the table, that there are exactly one Type B start and two equal multiplayer
round-start call sites, and that the fill still reads the DIV register twice, masks to the block range,
forces a gap at the rightmost cell, and skips the buffer write in multiplayer. Python 3 (standard
library only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

The table and the constants are fixed by the original and are not tuning knobs — to change them you
would change the source and regenerate, but there is no reason to. The generated files are overwritten
on the next run, so never hand-edit them; edit `src/data/garbage.h` only to change the wrapper (its
includes or its doc comment).

The row geometry is in `src/vm/garbage_fill.h`: `typeBGarbageTopRow` derives the Type B extent from
`kTypeBGarbageRowsPerHeight`, and `kDemoGarbageTopRow` from `kTypeBDemoGarbageRows`, so both follow the
generated constants. `kMultiplayerGarbageStartRow` is the one value written out by hand — the board row
the original's multiplayer call site names — and the tests pin it.

The exact write addresses, the demo stamp, and the procedural fill's full mechanism are in
[`../contracts/garbage-init.md`](../contracts/garbage-init.md).

## Testing

`tests/test_garbage.cpp` pins the six constants against the contract, sweeps all 40 grid cells against
the flat fixture, checks that every cell is in range and every row has a gap, shows the empty tile
equals the character map's space glyph, and pins the four corners to concrete bytes.

`tests/test_garbage_fill.cpp` covers the fill: the pick's parity relation and its cell domain, its
determinism and the intra-call divider advancement, the demo stamp against the table, the Type B and
multiplayer row extents, the one-gap-per-row rule at every column a gap could land in, the divider
shared with the piece randomizer, and both paths of the round-init seam. It constructs a VM, so it
points the asset root at the project tree to resolve `src/vm/garbage.asm`.

The parser has its own tests (`tools/asm_parser/test_parse_garbage.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_garbage`).
