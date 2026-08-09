# Garbage

A Type B game starts with the bottom of the playing field pre-filled with "garbage" — rows of blocks
broken by gaps that the player clears to win. This page covers the data behind it: the fixed table the
attract-mode demo stamps, and the constants the procedural fill and its start paths read.

The fill *behavior* — the random block-or-gap pick, the guarantee of a gap in every row, and the loop
that writes the rows — belongs to the gameplay code and is not written yet. What lives here is the
demo table and the constants that parameterize the fill.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/garbage.h` | The six constants and `kTypeBDemoGarbage` | Hand-written wrapper; the values are included from the generated file. |
| `src/data/generated/garbage_data.inc` | The six constants and the composed grid | **Generated — do not hand-edit.** |
| `tests/fixtures/garbage_expected.h` | The flat 40 bytes, for the test sweep | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`, included as `"data/garbage.h"` (the `src/` tree is on the
library's include path).

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

The exact write addresses, the demo stamp, and the procedural fill's full mechanism are in
[`../contracts/garbage-init.md`](../contracts/garbage-init.md).

## Testing

`tests/test_garbage.cpp` pins the six constants against the contract, sweeps all 40 grid cells against
the flat fixture, checks that every cell is in range and every row has a gap, shows the empty tile
equals the character map's space glyph, and pins the four corners to concrete bytes. The parser has its
own tests (`tools/asm_parser/test_parse_garbage.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_garbage`).
