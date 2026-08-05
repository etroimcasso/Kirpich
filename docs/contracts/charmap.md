# Contract — Charmap

Reverse-derived behavioral contract for Kirpich's character map: the table that turns text into the
tile indices the tile sheet stores each glyph at. Every value here is transcribed from the
`kaspermeerts/tetris` disassembly (upstream `b95c668`). The source is `charmap.asm`, included at
`tetris.asm:2`; its 47 `charmap "<seq>", $HH` lines are the authority the tests check against.

The map has two kinds of consumer in the original, and both reach the same 47 entries:

- **Static text** — `db "..."` rows the assembler encodes character-by-character through the map
  (the pause overlay, the Type-B scoreboard, the licence screen). Example: `db "pause"`
  (`tetris.asm:4575`) assembles to the five tiles `$19 $0A $1E $1C $0E`.
- **In-code character literals** — single characters used directly in instructions, which the
  assembler resolves to their tile byte: `ld [hl], "♥"` (`tetris.asm:1678`), `ld a, "…"`
  (`tetris.asm:3815`), `cp a, "…"` (`tetris.asm:4091`), `ld b, "×"` (`tetris.asm:4027`).

---

## The table

47 entries, source order (`charmap.asm:1-47`). Sequences are stored as their exact upstream UTF-8
bytes.

| Sequence | Tile | `charmap.asm` | Notes |
|---|---|---|---|
| `"0"`–`"9"` | `$00`–`$09` | lines 1–10 | Digit-identity: the digit's value **is** its tile index |
| `"a"`–`"z"` | `$0A`–`$23` | lines 11–36 | Contiguous |
| `"."` | `$24` | line 37 | |
| `"-"` | `$25` | line 38 | |
| `"×"` (U+00D7) | `$26` | line 39 | Multiplication sign; upstream comment "This is a multiplication sign" |
| `"♥"` (U+2665) | `$27` | line 40 | Heart |
| `"⋯"` (U+22EF) | `$29` | line 41 | Midline ellipsis; upstream comment "TODO ew". **`$28` is skipped** |
| `" "` (space) | `$2F` | line 42 | |
| `"©"` (U+00A9) | `$33` | line 43 | Copyright sign |
| `"…"` (U+2026) | `$60` | line 44 | Horizontal ellipsis (distinct from the midline `⋯` above) |
| `"”"` (U+201D) | `$9B` | line 45 | Right double quotation mark, alone |
| `","` | `$9C` | line 46 | |
| `".”"` (U+002E U+201D) | `$9D` | line 47 | **Two code points → one tile** (the ligature) |

### Digit-identity is a guarantee

`"0"`–`"9"` map to exactly `$00`–`$09`, in order. This is not a coincidence of the tile sheet — the
score renderer relies on it: a binary-coded-decimal digit is used directly as a tile index. The
port asserts it three ways (the parser's structural check, the full-corpus fixture sweep, and a
dedicated identity test), and downstream score rendering may depend on it.

### The value set is non-contiguous

Tile indices `$28`, `$2A`–`$2E`, `$30`–`$32`, `$34`–`$5F`, `$61`–`$9A` are **not** charmap entries.
They are glyphs the tile sheet holds that the text system never names — so they get no C++
representation here (no sentinel, no "unknown" entry). The gaps are real and intentional.

The clearest illustration is the copyright line `db "   ©", $30, $31, $32, $31, " ", $34, $35, $36,
$37, $38, $39, "     "` (`tetris.asm:6996`): the `"©"` goes through the charmap (to `$33`), but the
year digits `1989` are written as **raw** tile bytes `$31 $39 $38 $39` — a *second*, different digit
glyph set at `$30`–`$39`, not the charmap's `"0"`–`"9"` at `$00`–`$09`.

### The ligature

`".”"` is a single map entry that is two Unicode code points (`.` then the right double quotation
mark) and encodes to the single tile `$9D` — distinct from the two tiles `"."` (`$24`) and `"”"`
(`$9B`) would produce separately. It exists because the licence screen ends a sentence with `.”`
rendered as one closing glyph: `db "by alexey pazhitnov.”"` (`tetris.asm:7002`) ends in `$9D`, not
`$24 $9B`.

## Encoding semantics — greedy longest match, all-or-nothing

The assembler encodes a text string by scanning left to right and, at each position, consuming the
**longest** map sequence that matches there. For this corpus only the `".”"` ligature needs the
precedence (it must win over `"."`), but the rule is general.

- `encodeCharmapText("by alexey pazhitnov.”")` ends in a single `$9D`.
- A character with no map entry is a hard failure: the original assembler errors, and the port's
  encoder returns "no encoding" for the whole string rather than emitting partial output or skipping
  the character. Uppercase letters, `!`, and any other unmapped byte all fail this way.
- Empty text encodes to an empty result (success).

Exact-sequence lookup (`getCharmapTile`) is a whole-sequence match with no prefix logic — it is the
shape the in-code single-character literals want.

---

## Tested by

`tests/test_charmap.cpp` — a full 47-row sweep of the engine table against the parser-emitted
fixture (`tests/fixtures/charmap_expected.h`), the digit- and letter-identity assertions, exact
lookup across the whole corpus plus misses, the longest-match ligature cases, two known upstream
strings (`"pause"` at `tetris.asm:4575`, `" 0 × 40   "` at `tetris.asm:6491`), the all-or-nothing
rejection of unmapped input, and the empty-input case. The parser's own structural checks
(`tools/asm_parser/test_parse_charmap.py`) guard the transcription against upstream changes — count,
uniqueness, the digit/letter/ligature anchors, and ASCII-purity of the emitted table.
