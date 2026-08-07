# Background tilemaps

The static screens the game draws — full screens, banners, playing-field overlays, window messages,
tower columns, and the congratulations strip — stored as grids of tile indices. This page covers the
data: what each grid holds, where it lives, and how to regenerate it. This unit is data only; loading
a screen to the display and drawing over it belong to the rendering code, which is not written yet.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/tilemaps.h` | The four dimension constants and all 22 grids | Hand-written header; the constants and grids are included from the generated file. |
| `src/data/generated/tilemaps_data.inc` | The four constants and all 22 grids | **Generated — do not hand-edit.** |
| `tests/fixtures/tilemaps_expected.h` | The 22 flat byte arrays, for the test sweep | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`, included as `"data/tilemaps.h"` (the `src/` tree is on the
library's include path). The header includes `"playing_field.h"` for the field overlays' extents.

## Using it

```cpp
#include "data/tilemaps.h"

kirpich::kTilemapScreenCols;   // -> 20   (full-screen width)
kirpich::kTilemapScreenRows;   // -> 18   (full-screen height)
kirpich::kTilemapWindowCols;   // ->  8   (window-block width)
kirpich::kTowerTilemapRows;    // ->  7   (tower-column height)

// Two-dimensional screens are row-major: grid[row][col], row 0 at the top.
kirpich::kTypeAGameplayTilemap[0][0];       // -> 0x2A
kirpich::kScoreboardTilemap[1][3];          // the multiplier glyph in " 0 x 40   "

// The tower columns and the congratulations strip are one-dimensional.
kirpich::kLeftTowerLeftSideTilemap[0];      // -> 0xC2  (top of the column)
kirpich::kCongratulationsTilemap.size();    // -> 16
```

Each grid is a `std::array` of `std::uint8_t` tile indices sized to its screen: the nine full screens
are 20 × 18, the banners 20 × 4/6/4, the two field overlays 10 × 18 (the playing-field shape, so they
reuse `kPlayingFieldCols`/`kPlayingFieldRows`), the three window blocks 8 × 10/7/6, the four tower
columns 7 tall, and the congratulations strip 16 tiles. All are `constexpr`, so they can be read at
compile time; there are no functions and no state.

The grids carry only tile data. The field overlays' loader terminates on a `$FF` byte that is **not**
in the composed grid — it lives only in the byte fixture, as the serialization the original stores.
Text screens are already decoded to tile indices; the `.”` ligature in the credits line is one tile.

## Regenerating the grids

The constants, grids, and test fixture are produced from the disassembly by the parser. Regenerate
after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_tilemaps.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/tilemaps_data.inc \
  --fixture-out tests/fixtures/tilemaps_expected.h
```

The parser reads the character map first, then each screen by its label shape rather than by line
number, and stops with a citation if anything has moved: every screen must have exactly its class
dimensions, the field overlays must end with a lone `$FF`, mixed string/byte rows must still land on
the class width, every text run must resolve through the character map by greedy longest match, the
loaders must still declare the widths and sentinel the grids depend on, and the per-screen byte counts
must sum to 4110. Python 3 (standard library only); it is a development tool and is never needed to
build or test Kirpich.

## Changing it

The screens are fixed by the original and are not tuning knobs — to change one you would change the
source and regenerate, but there is no reason to. The generated files are overwritten on the next run,
so never hand-edit them. If you need to know what a particular tile index draws, that is a property of
the loaded tile sheet (see [tile-graphics.md](tile-graphics.md)); the grids only name which tile goes
in which cell. Screen addresses, the pause map flip, and the cells the game overwrites at runtime are
in [`../contracts/tilemaps.md`](../contracts/tilemaps.md).

## Testing

`tests/test_tilemaps.cpp` sweeps all 22 grids cell for cell against the byte fixture — the nine full
screens, the banners and window blocks, the two field overlays (and that the `$FF` terminator is in
the serialization but not the composed grid), and the tower columns and congratulations strip. It pins
the four constants and the field overlays' extents to the playing-field constants, re-encodes selected
text rows through the character map to confirm they match the composed grids (including the ligature),
and pins a handful of boundary cells to the source. The parser has its own tests
(`tools/asm_parser/test_parse_tilemaps.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_tilemaps`).
