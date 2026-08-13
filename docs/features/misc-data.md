# Miscellaneous data

The loose tables and constants that do not belong to a larger data class, collected into one unit: the
sprite objects a few screens draw directly, the cursor coordinate tables the menus position selections
with, the two-player win-screen strings and the "pause" label, and a few scalars. Each is too small to
warrant its own unit, so they ship together in one header.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `OamObject` | `src/data/misc.h` | one directly-drawn sprite object: `{ y, x, tile, xflip }` |
| `SpriteCoordinate` | `src/data/misc.h` | one cursor position: `{ y, x }` |
| Four `k…FaceObjects` / `kPushStartObjects` arrays | `src/data/misc.h` | the raw sprite-object tables (25 objects) |
| Six `k…CursorCoordinates` / `kMusicTypeSpriteCoordinates` arrays | `src/data/misc.h` | the menu cursor coordinate tables (42 pairs) |
| `kDeuceText`, `kMarioWinsText`, `kLuigiWinsText`, `kAdvantageText`, `kPauseText` | `src/data/misc.h` | the five strings |
| `kDemoRecordingEnabledMagic`, `kCompletedRowCheckFirstRow`, `kCompletedRowCheckRowCount` | `src/data/misc.h` | the three constants |

The behavioral specification — the consumer sites for each table, the letter tilesets, the music-type
index math, and the completed-row scan quirk, all with source line anchors — is in
[`../contracts/misc.md`](../contracts/misc.md).

## Decisions

**Directly-drawn objects are their own type, not `SpriteId`.** A few screens copy fixed sprite objects
straight into the object buffer instead of going through the composed sprites. Those objects carry a raw
gameplay-tileset tile number, so they are an `OamObject` with a plain `tile` field, kept separate from
the `SpriteId` identity space the composed-sprite path uses. The attribute byte only ever selects
x-flip, so the struct carries a `bool xflip` rather than the whole byte.

**The cursor tables name their menu.** Each of the six coordinate tables belongs to a specific menu —
the Type-A and Type-B level pickers, the Type-B and two-player start-height pickers, and the music-type
picker. The three the disassembly leaves unnamed take a role name (`kTypeALevelCursorCoordinates`,
`kTypeBLevelCursorCoordinates`, `kTypeBStartHeightCursorCoordinates`); all six are found by following
the code that positions each menu's cursor, not by a label search.

**The win strings stay raw; "pause" is character-map text.** The four win-screen strings use the
gameplay tile sheet, where a tile number can differ from the same value's character-map glyph, so they
are plain `std::uint8_t` arrays. "pause" is assembled through the character map, so it is a `CharTile`
array encoded with the same character-map table the rest of the text uses.

**Coordinates read as numbers.** `y` and `x` are pixel positions, so the tables write them as plain
decimal; the tile field stays a hex tile-sheet index.

## Keeping it honest

`src/data/misc.h`'s tables and constants are generated from the disassembly by
`tools/asm_parser/parse_misc.py`, which stops with a source citation if a sprite attribute byte sets a
bit other than x-flip, a table's size disagrees with its record count, a win string's length disagrees
with its text, the gameplay letter tileset is inconsistent across the strings and the PUSH-START tiles,
the three demo-recording sites disagree on the sentinel value, or the completed-row scan constants do
not resolve against the playing-field geometry. The fixture holds each table's real bytes independently
of the typed surface, and `tests/test_misc.cpp` re-derives every object, pair, and string from those
bytes — so a defect in the header fails the sweep. See [`../engine/misc.md`](../engine/misc.md) for how
to use and regenerate it.

## Not here yet

The code that reads these tables — drawing the win screens and PUSH-START prompt, positioning the menu
cursors, printing the pause label, and running the completed-row scan — is the gameplay and rendering
work, and it builds on this data. This unit provides the tables, strings, and constants those build on,
and the fixture to check them against.
