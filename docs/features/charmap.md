# Character map

How the game turns text into graphics. Every piece of on-screen text in the original — the pause
overlay, the Type-B score line, the licence and credits screens — is stored as a string and encoded
through one small table that maps each character to the tile the tile sheet holds its glyph at. A
few characters are also used directly in code (the heart in heart-mode, the `×` and `…` the score
line draws). Porting the table first means every later text and score routine encodes exactly the
way the original assembler did.

## What it is

| Piece | Where | Shape |
|---|---|---|
| `CharTile` | `include/kirpich/char_tile.h` | `enum class : uint8_t`, 47 named glyphs |
| `CharmapEntry` | `include/kirpich/charmap.h` | `{ std::string_view sequence; CharTile tile; }` |
| The table + lookups | `src/data/charmap.h`, `src/data/charmap.cpp` | 47 rows + three accessors |

Three accessors cover the two consumer shapes:

- `getCharmap()` — the whole 47-row table, for sweeps and iteration.
- `getCharmapTile(sequence)` — the tile for one exact character sequence; the shape in-code
  character literals want.
- `encodeCharmapText(utf8)` — encode a whole string to tile indices the way the assembler encodes
  `db "..."` text.

The exact 47 mappings and their sources are pinned in [`../contracts/charmap.md`](../contracts/charmap.md).

## Decisions

**A 47-row table, not a 256-entry per-character array.** The obvious shape — one tile per character
code — cannot represent this map. Six of the characters are multi-byte (`×`, `♥`, `⋯`, `©`, `…`,
`”`), and one entry is a two-character ligature (`.”`) that encodes to a single tile distinct from
its two characters encoded separately. The table stores each character *sequence* as its exact bytes
and matches on them.

**Sequences are stored as their exact upstream bytes.** The `×` is the multiplication sign, not the
letter x; the `”` is a curly right quote, not a straight one. Keeping the exact bytes means the port
matches text the same way the original did, with no lossy substitution.

**Encoding is greedy longest match, all-or-nothing.** At each position the longest matching sequence
wins, so `.”` is encoded as one tile rather than two — matching the assembler. A character with no
entry fails the whole encode rather than producing partial output, which is exactly what the
original assembler does (it errors on an unmapped character).

**Digit-identity is treated as a guarantee, not a coincidence.** `"0"`–`"9"` map to `$00`–`$09`, so
a decimal digit is its own tile index. The score renderer depends on this, so the port asserts it
explicitly and records it in the contract.

**The tile is a named glyph, not a bare byte.** The charmap is the original's naming table for the
text glyph space — `"a"` names the glyph at `$0A` — so the port mints those names as the `CharTile`
enum, generated from the same source, byte values preserved. Every consumer reads
`CharTile::LETTER_A` instead of `0x0A`; the raw index is one `static_cast` away where the renderer
eventually needs it. Glyphs the tile sheet holds but the charmap never names gain enumerators as
the surfaces that use them are ported.

**The gaps are documented, not modelled.** The tile indices the map skips (`$28` and many others)
are glyphs the tile sheet holds that the text system never names — including a *second* digit glyph
set at `$30`–`$39` the licence screen uses for the copyright year as raw bytes. None of these get a
placeholder entry; they are simply not part of this table.

## Keeping it honest

The 47 rows, the `CharTile` enum, and the test fixture are all read straight out of `charmap.asm`
by `tools/asm_parser/parse_charmap.py`, and the tests sweep the full table against that fixture —
which deliberately keeps raw bytes, so a wrong enumerator value surfaces as a failing test rather
than hiding behind the type. The generated table is written with non-ASCII characters as byte
escapes so it encodes identically on every platform. See
[`../engine/charmap.md`](../engine/charmap.md) for how to regenerate and use it.
