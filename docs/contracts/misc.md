# Contract — Miscellaneous data

Reverse-derived behavioral contract for Kirpich's miscellaneous data unit: the loose tables and
constants that do not belong to any larger data class. Every value here is transcribed from the
`kaspermeerts/tetris` disassembly (upstream `b95c668`); the line anchors below are the authority the
tests check against.

Four kinds of data collect here:

- **Raw-OAM object tables** — sprite objects a few screens copy straight into the object buffer.
- **Cursor coordinate tables** — the on-screen positions each menu's selection cursor can sit at.
- **Text strings** — the two-player win-screen strings, and the "pause" label.
- **Constants** — the demo-recording sentinel and the completed-row scan's two quirk numbers.

---

## A — Raw-OAM object tables

Four tables of 4-byte `{y, x, tile, attr}` records, copied verbatim into the OAM shadow buffer
(`wOAMBuffer`) rather than drawn through the composed-sprite path. `tile` is a raw gameplay-tileset
VRAM index, **not** a `SpriteId`. Across all 25 objects the `attr` byte is only ever `$00` or `$20`
(bit 5, OAM x-flip); no other attribute bit is set.

| Label | Line | Objects | Copied to | Consumer |
|---|---|---|---|---|
| `MarioLuigiFaceObjects` | :1058 | 8 | OAM slot 32 | two-player start-height screen init, `4*8` bytes via `Call_725` (:1038–1041) |
| `MarioFaceObjects` | :1294 | 4 | OAM slot 32 | `GameState_18` master role, `$10` bytes via `Call_725` (:1266–1280) |
| `LuigiFaceObjects` | :1291 | 4 | OAM slot 32 | same site, slave role |
| `PushStartObjects` | :2346 | 9 | OAM slot 24 | victory-screen "PUSH START", `4*9` bytes (:2332–2340); `HidePushStart` (:2357) zeroes the nine Y bytes |

The face tables are mirrored pairs: each even-indexed object is the left half and the following
odd-indexed object is its x-flipped right half (`attr = $20`). `PushStartObjects` shares one Y row
(`$42`) and never flips; its nine tiles spell P·U·S·H·S·T·A·R·T in the gameplay letter tileset.

## B — Cursor coordinate tables

Six tables of 2-byte `{y, x}` records (ROM byte order: y then x). Each is indexed by a menu selection
value; the cursor positioner multiplies the index by two (`add a`) to stride the pair. Five feed the
generic digit-cursor positioner `UpdateDigitCursor` (:3574; the stride is in `.afterSFX` at :3579);
the sixth feeds the structurally identical `PositionMusicTypeSprite` (:3152–3172).

| Label | Line | Pairs | Index | Consumer site(s) |
|---|---|---|---|---|
| `Data_1615` | :3403 | 10 | `hTypeALevel` 0–9 | Type-A level screen `GameState_10` (:3328–3330), level move (:3382–3384) |
| `Data_16D2` | :3503 | 10 | `hTypeBLevel` 0–9 | Type-B screen `GameState_12` (:3418–3420), level move (:3483–3485) |
| `Data_1741` | :3566 | 6 | `hTypeBStartHeight` 0–5 | Type-B screen (:3422–3424), height move (:3546–3548) |
| `MarioStartHeightCursorCoordinates` | :1198 | 6 | two-player Mario start height 0–5 | `UpdatePlayerStartHeightCursors` (:1206) |
| `LuigiStartHeightCursorCoordinates` | :1202 | 6 | two-player Luigi start height 0–5 | same |
| `MusicTypeSpriteCoordinates` | :3174 | 4 | music-type tile `$1C`–`$1F` | `PositionMusicTypeSprite` (:3152–3172) |

`Data_1615`, `Data_16D2`, and `Data_1741` are disassembler auto-labels (the source names none of the
three); the port names them for their role — `TypeALevelCursorCoordinates`,
`TypeBLevelCursorCoordinates`, `TypeBStartHeightCursorCoordinates`. All six are bounded by tracing
the callers of `UpdateDigitCursor` and `PositionMusicTypeSprite`, not by a label-name search.

The level and start-height tables are indexed directly by the selection value (a clean 0-based
number). The music-type table is not: the music-type value is the cursor's own tile number
(`$1C`–`$1F`), so its index is the value measured from the first music-type tile (`− $1C`), the math
`PositionMusicTypeSprite` performs at :3158.

**`Data_1741` has a trailing `db $00`** (:3570, upstream comment `XXX TODO`). It is dead padding: a
valid index 0–5 reaches a maximum byte offset of 11, so byte 12 is never read. The port carries only
the six real pairs; the trailing byte is asserted at transcription time and not shipped in the typed
surface.

## C — Text strings

Five strings. The four win-screen strings are **raw gameplay-tileset bytes**, not character-map
values: their letter tiles live at `$B0`–`$BE`/`$2D`/`$2E`/`$3D`/`$3E`/`$41`, and where a byte
collides with a character-map slot the glyph differs (`$0E` draws "S" here; the character map maps
"e" → `$0E`). "pause" **is** a character-map string (`db "pause"`, which the assembler resolves
through `charmap.asm`).

| Label | Line | Bytes | Text | Encoding |
|---|---|---|---|---|
| `DeuceText` | :2619 | 6 | "DEUCE!" | raw gameplay tileset |
| `MarioWinsText` | :2622 | 11 | "MARIO WINS!" | raw (`$2F` = space) |
| `LuigiWinsText` | :2627 | 11 | "LUIGI WINS!" | raw |
| `AdvantageText` | :2632 | 9 | "ADVANTAGE" | raw |
| `PauseText` | :4574 | 5 | "pause" | character map → `CharTile` |

The four win strings are drawn by `PrintUnderlinedText` (:2599); `PauseText` by `PrintPauseText`
(:4561) through `PrintCharacter`. The win-screen strings and `PushStartObjects` share one gameplay
letter tileset — a byte always draws the same letter across both (A = `$B5`, U = `$B2`, S = `$0E`,
R = `$BB`, …) — which the transcription cross-checks.

## D — Constants

| Constant | Value | Anchors |
|---|---|---|
| Demo-recording sentinel | `$FF` | set in `StartRecordingDemo` (:627–630); compared in `DemoSimulateJoypad` (:778) and `RecordDemo` (:829) |
| Completed-row scan first row | 2 | `CheckForCompletedRows` `ld hl, $C842` (:5310, upstream comment "Start at the third row!? Why? Bug") |
| Completed-row scan row count | 16 | `CheckForCompletedRows` `ld b, 16` (:5311, "Only check 16 of the 18 rows…") |

The demo-recording sentinel is a single value used at three sites, all in agreement. The recording
path itself (`RecordDemo` / `StartRecordingDemo`) is dead code — nothing in the shipped game sets the
flag — but the value is real data.

The completed-row scan starts at shadow-field address `$C842`. That address is the field's third row:
the field's top row sits at `$C802` with a `$20`-byte row stride (the playing-field geometry, see
[`playing-field.md`](playing-field.md)), so `($C842 − $C802) / $20 = 2`. Combined with the 16-row
count, the scan skips exactly the top two rows and checks the remaining 16 — the top-two-rows-never-
cleared quirk. The `$C842` address itself is provenance only; the port carries the row index (2).

## Deferred to the gameplay logic

Recorded so the unit's scope is unambiguous; none produces a committed constant here:

- The routine-embedded mechanism constants near these tables: the `+ $20` digit-tile base in
  `UpdateDigitCursor` (:3592), the underline tile `$B6` in `PrintUnderlinedText` (:2612), the
  `$98EE` print destination in `PrintPauseText`, and the `hMusicType` cursor-bound logic.
- The bodies of `Call_725`, `HidePushStart`, `PrintUnderlinedText`, `PrintCharacter`, and the
  cursor positioners — presentation/gameplay code.
- The `$C842` shadow-field address (fixture-only, as with every field address).

---

## The surface

- **Parser-emitted** (`tools/asm_parser/parse_misc.py`, `--all`): `src/data/generated/misc_data.inc`
  — the four `OamObject` arrays, the six `SpriteCoordinate` arrays, the five string arrays, and the
  three constants — and `tests/fixtures/misc_expected.h` — the raw ROM bytes and scalar values,
  independent of the typed surface. Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/data/misc.h` (the `OamObject` and `SpriteCoordinate` structs and
  the `musicTypeSpriteCoordinate` accessor), the tests, and this contract.

Byte-equivalence stays under test: `OamObject` is `sizeof 4` and `SpriteCoordinate` is `sizeof 2`,
each its exact ROM record width, and the fixture holds every table's real bytes for the sweep to
re-derive against.

### Transcription asserts

`parse_misc.py` hard-errors (with a `file:line` citation) on any of: an OAM `attr` byte with a bit
outside `$20`; a table whose byte count disagrees with its record count; `Data_1741`'s trailing byte
not being `$00`; a win-string byte count that disagrees with its text, or the gameplay letter tileset
being inconsistent (a byte drawing two different letters); the three demo-recording sites disagreeing
on the sentinel value; or the completed-row scan's start address not resolving to a whole row of the
field, its stride not matching the field's `$20`, or the first-row-plus-count total not equalling the
18 field rows. The character-map table is reused from the charmap transcription; "pause" is encoded
through it by the same greedy longest-match the assembler uses.

---

## Tested by

`tests/test_misc.cpp` — the three constants and every table/string size pinned (with the two
byte-equivalence sizes); the full 25-object OAM sweep re-deriving each object from the raw fixture
bytes and asserting the attribute domain; the full 42-pair coordinate sweep across all six tables;
the music-type accessor's `− $1C` index math over all four selections; the four win strings swept
against the fixture and "pause" checked by double entry (raw fixture bytes and the port's own
character-map encoder); and the corpus corners (first/last of each OAM table, the PUSH-START single-row invariant,
x-flip only on the right-half face objects, boundary coordinate pairs). The parser's own structural
checks (`tools/asm_parser/test_parse_misc.py`) guard the scan against upstream changes.
