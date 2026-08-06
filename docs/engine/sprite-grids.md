# Sprite layout grids

Five shared grids of (y, x) pixel offsets a composite sprite is drawn against, plus `PieceKind`, the
named enum for the seven tetromino shapes. A composite sprite has a tile list and a grid; the renderer
walks them together, adding one offset pair per cell to the sprite's origin to place each tile. The
grids are reused across every kind of composite sprite, so they carry only geometry.

## Where each surface lives

| Surface | File | Editing it |
|---|---|---|
| `SpriteGridOffset` | `src/data/sprite_grids.h` | Hand-written type. |
| `kSpriteGrid4x4` / `1x8` / `7x2` / `8x4Notched` / `3x3` | `src/data/sprite_grids.h` | **Generated — do not hand-edit** (values come from `generated/sprite_grids_data.inc`). |
| `PieceKind` | `include/kirpich/piece_kind.h` | Hand-written. |

The grid arrays and the type are in `namespace kirpich`; include them as `"data/sprite_grids.h"`.
`PieceKind` is in `namespace kirpich` too — `<kirpich/piece_kind.h>`.

## `SpriteGridOffset` and the grids

```cpp
#include "data/sprite_grids.h"

kirpich::SpriteGridOffset o = kirpich::kSpriteGrid4x4[5];  // { .y = 0x08, .x = 0x08 }
o.y;  // pixels down from the sprite origin
o.x;  // pixels right from the sprite origin
kirpich::kSpriteGrid4x4.size();  // 16
```

`SpriteGridOffset` is a two-byte aggregate (`y`, then `x`); `sizeof(SpriteGridOffset) == 2` is asserted
at the definition. The five grids hold 16 / 8 / 14 / 28 / 9 pairs (150 bytes total). All offsets are
multiples of 8 in `0x00`–`0x38`. Four grids are plain row-major; `kSpriteGrid8x4Notched`'s first two
rows are 2 wide and start at `x = 0x08` (the notch). There is no lookup function — reference a grid by
name.

## `PieceKind`

```cpp
#include <kirpich/piece.h>

kirpich::Piece p{0x0A};
p.kind();  // kirpich::PieceKind::I   (0x0A >> 2 == 2)
```

`enum class PieceKind : uint8_t { L=0, J=1, I=2, O=3, S=4, Z=5, T=6 }`. `Piece::kind()` returns a
`PieceKind`; the value order is the order the game assigns sprites to kinds. There is no sentinel — every
valid piece byte (`raw` 0–27) decodes to one of the seven.

## Regenerating the grids

The grid arrays and the test fixture are produced from the disassembly by the parser. Regenerate them
after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_sprite_grids.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/sprite_grids_data.inc \
  --fixture-out tests/fixtures/sprite_grids_expected.h
```

The parser checks the source's structure as it reads — the five grid labels in order, each grid's byte
count against the address deltas in its label, and every byte a multiple of 8 within range — and stops
with a citation if anything has moved, rather than emitting a wrong file. Python 3 (standard library
only); it is a development tool and is never needed to build or test Kirpich.

## Changing values

The grid values are **generated** — to change one, change the source or the parser and regenerate; a
hand edit to `sprite_grids.h` or the `.inc` is overwritten on the next run. `PieceKind` is hand-written:
edit `piece_kind.h`, and update the derivation in the contract if the source ordering itself changed.

The exact meaning of every value, with its line in the original, is in
[`../contracts/sprite-grids.md`](../contracts/sprite-grids.md).

## Testing

`tests/test_sprite_grids.cpp` sweeps the five grids in full against the generated fixture, checks each
grid's closed-form geometry and total byte count, and verifies `PieceKind`'s values and the
`Piece::kind()` decode over `raw = 0..27`. The parser has its own tests
(`tools/asm_parser/test_parse_sprite_grids.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_sprite_grids`).
