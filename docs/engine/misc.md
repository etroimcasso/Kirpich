# Miscellaneous data

The loose tables and constants that do not belong to a larger data class: the raw sprite-object
tables a few screens draw directly, the cursor coordinate tables the menus position selections with,
the two-player win-screen strings and the "pause" label, and a handful of constants. They live
together in one header because each is too small to warrant its own.

Everything is in `namespace kirpich`, header-only, included as `"data/misc.h"` (the `src/` tree is on
the library's include path). There is no `.cpp`; these are data the state and rendering code read.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/misc.h` | The `OamObject` and `SpriteCoordinate` structs, the table arrays, the string arrays, the three constants, and the `musicTypeSpriteCoordinate` accessor | Structs and the accessor are hand-written; the arrays and constants are included from the generated file below. |
| `src/data/generated/misc_data.inc` | The table arrays, string arrays, and constants, at namespace scope | **Generated — do not hand-edit.** |
| `tests/fixtures/misc_expected.h` | The raw ROM bytes and scalar values, for the test sweep | **Generated — do not hand-edit.** |

## The types

`OamObject` is one sprite object copied straight into the object buffer:

```cpp
struct OamObject {
    std::uint8_t y;      // OAM Y coordinate
    std::uint8_t x;      // OAM X coordinate
    std::uint8_t tile;   // gameplay-tileset VRAM tile index
    bool         xflip;  // OAM horizontal flip
};
```

`SpriteCoordinate` is one on-screen cursor position:

```cpp
struct SpriteCoordinate {
    std::uint8_t y;  // OAM Y coordinate
    std::uint8_t x;  // OAM X coordinate
};
```

Both have a defaulted `==`. `OamObject` is `sizeof 4` and `SpriteCoordinate` is `sizeof 2` — each its
exact ROM record width.

## Using it

```cpp
#include "data/misc.h"

#include <kirpich/char_tile.h>
#include <kirpich/music_type.h>

using kirpich::MusicType;

// Raw sprite-object tables (OamObject spans). tile is a gameplay-tileset index, not a SpriteId.
for (const kirpich::OamObject& o : kirpich::kPushStartObjects) {
    place(o.x, o.y, o.tile, o.xflip);
}

// Cursor coordinate tables, indexed by the selected value.
kirpich::SpriteCoordinate cursor = kirpich::kTypeALevelCursorCoordinates[level];  // level 0..9

// The music-type cursor is looked up through the accessor, which maps the music-type value to its
// table index (the music-type value is the cursor tile number, not a 0-based index).
kirpich::SpriteCoordinate musicCursor = kirpich::musicTypeSpriteCoordinate(MusicType::MUSIC_B);

// Win-screen strings are raw gameplay-tileset tile numbers; "pause" is character-map glyphs.
std::span<const std::uint8_t> mario = kirpich::kMarioWinsText;   // 11 tile numbers
std::span<const kirpich::CharTile> pause = kirpich::kPauseText;  // 5 CharTile glyphs
```

### The tables

| Array | Type | Count | Indexed by |
|---|---|---|---|
| `kMarioLuigiFaceObjects` | `OamObject` | 8 | — (drawn as a block) |
| `kMarioFaceObjects`, `kLuigiFaceObjects` | `OamObject` | 4 each | — |
| `kPushStartObjects` | `OamObject` | 9 | — |
| `kTypeALevelCursorCoordinates`, `kTypeBLevelCursorCoordinates` | `SpriteCoordinate` | 10 each | level 0–9 |
| `kTypeBStartHeightCursorCoordinates`, `kMarioStartHeightCursorCoordinates`, `kLuigiStartHeightCursorCoordinates` | `SpriteCoordinate` | 6 each | start height 0–5 |
| `kMusicTypeSpriteCoordinates` | `SpriteCoordinate` | 4 | via `musicTypeSpriteCoordinate` |
| `kDeuceText` | `std::uint8_t` | 6 | — |
| `kMarioWinsText`, `kLuigiWinsText` | `std::uint8_t` | 11 each | — |
| `kAdvantageText` | `std::uint8_t` | 9 | — |
| `kPauseText` | `CharTile` | 5 | — |

The level and start-height tables are indexed directly by the selected value. The music-type table is
not: the music-type value is the cursor's tile number, so `musicTypeSpriteCoordinate(MusicType)` maps
the value to the table index for you.

### The constants

- `kDemoRecordingEnabledMagic` (`0xFF`) — the value that marks demo recording as active.
- `kCompletedRowCheckFirstRow` (`2`) and `kCompletedRowCheckRowCount` (`16`) — the completed-row scan
  starts at field row 2 and checks 16 of the 18 rows, so it skips the top two rows.

## Gotchas

- **The win-screen strings are not character-map text.** `kMarioWinsText` and the other three win
  strings hold raw gameplay-tileset tile numbers; the same byte value can be a different glyph in the
  character map. Only `kPauseText` is character-map text, which is why it is a `CharTile` array. Do not
  run a win string through the character-map decoder.
- **`OamObject::tile` is not a `SpriteId`.** These objects are drawn directly, not through the composed
  sprites in `src/data/sprites.h`. The tile number indexes the gameplay tile sheet.

## Regenerating the data

`misc_data.inc` and the fixture are produced from the disassembly by the parser. Regenerate after
repinning the upstream source:

```sh
python3 tools/asm_parser/parse_misc.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/misc_data.inc \
  --fixture-out tests/fixtures/misc_expected.h
```

The parser stops with a source citation if a sprite-object attribute byte sets a bit other than
x-flip, a table's byte count disagrees with its record count, a win-string byte count disagrees with
its text, the gameplay letter tileset is inconsistent, the three demo-recording sites disagree on the
sentinel value, or the completed-row scan constants do not resolve against the playing-field geometry.
Python 3 (standard library only); it is a development tool and is never needed to build or test
Kirpich.

## Changing it

The values are fixed by the original game and are not tuning knobs; the generated files are
overwritten on the next parser run, so never hand-edit them. To change a struct field or add the
accessor for another table, edit `src/data/misc.h`. The behavioral specification, with source line
anchors, is in [`../contracts/misc.md`](../contracts/misc.md).

## Testing

`tests/test_misc.cpp` sweeps every table in full against the raw fixture: all 25 sprite objects
re-derived from their bytes, all 42 coordinate pairs across the six tables, the four win strings, and
"pause" checked against both the fixture and the port's own character-map encoder. It also pins the
music-type index math over all four selections, the two record sizes, and the corpus corners. The
parser has its own tests (`tools/asm_parser/test_parse_misc.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_misc`).
