# Contract — Tile graphics

Reverse-derived behavioral contract for Kirpich's tile-graphics extraction: which bytes of the Game
Boy Tetris ROM are the game's graphics, how they decode to pixels, and the exact files both
population routes must produce. The authority is upstream's own dumper, `dump_gfx.py` — the script
that produced the four PNGs committed in the disassembly's `gfx/` directory — cross-checked against
the ROM and those PNGs; the tests check against fixture values derived from all three in agreement.

The engine consumes PNG. The ROM stores raw tile bytes. The only conversion anywhere in the port is
ROM → PNG, performed in-app on the player's machine at first start.

---

## The ROM

One ROM is accepted, identified before anything is decoded:

| | |
|---|---|
| Name | Tetris (World) (Rev 1) |
| Size | exactly 32,768 bytes — no header |
| SHA-1 | `74591cc9501af93873f9a5d3eb12da12c0723bbc` |

Anything else — other revisions, other regions, headered or modified dumps — is **refused, with
nothing written**. There is no header detection or stripping and no fuzzy acceptance: a near-miss
ROM would decode to subtly wrong graphics, which is the failure the gate exists to prevent.

## The four blocks

`dump_gfx.py` names four contiguous tile blocks (its `copyrightandtitlescreen` offset is written as
the expression `0x415F + 39 * 8`):

| Asset | ROM offset | Tiles | Format | PNG (bit depth) |
|---|---|---|---|---|
| `font.png` | `0x415F` | 39 | 1bpp | 128 × 24 (1) |
| `copyrightandtitlescreen.png` | `0x4297` | 119 | 2bpp | 128 × 64 (2) |
| `configandgameplay.png` | `0x323F` | 197 | 2bpp | 128 × 104 (2) |
| `multiplayerandburan.png` | `0x55AC` | 207 | 2bpp | 128 × 104 (2) |

The font is the **only 1bpp block**. Decoding it as 2bpp reads twice the bytes and silently
produces garbage at twice the height, so the format is part of the contract, not an inference.

## The decode

Each tile is 8 × 8 pixels.

**2bpp** — 16 bytes per tile, two bitplanes interleaved per row, low plane first. For pixel row
`r` (0–7): `lo = tile[2r]`, `hi = tile[2r+1]`. For column `c` (0 = leftmost), reading bit
`b = 7 − c` of each plane:

```
sample = 3 − (hiBit·2 + loBit)
```

**1bpp** — 8 bytes per tile, one plane: `sample = 1 − bit`.

The **inversion** (`3 −` / `1 −`) is contract, not a rendering choice: the committed upstream PNGs
carry it, the development copy route ships those PNGs verbatim, so an extractor that skipped it
would make the two routes diverge pixel-for-pixel.

## The layout

Tiles lay out **16 per row** (128 px wide); height is `⌈tiles / 16⌉ × 8`. When the last row is
partial, the remainder fills with the block's background value — **1** for 1bpp, **3** for 2bpp,
the same value a raw 0 maps to. The padding is contract for the same reason the inversion is: the
committed PNGs carry it.

## The files

Greyscale PNG, non-interlaced, bit depth 2 (bit depth 1 for the font), written to
`assets/gfx/default/` under the engine's asset root at the exact names the presence check requires
(`src/assets/presence.cpp`). The sample values are the palette indices, unscaled — the engine's
loader reads them back as indices, which is what makes content equality between the two routes
checkable through the engine itself.

Every extraction run rewrites all four files. All four blocks decode in memory before the first
file is written, so a failure partway cannot leave a half-populated install.

## Content pins

The pixels themselves are ROM-derived and are never committed. The fixture pins each block by its
decoded dimensions and the **FNV-1a-64 hash** of its decoded index content (row-major, one byte
per pixel) — enough to detect any drift in offset, format, inversion, padding, or layout, while
committing nothing derived from the ROM's expression.

---

## Generated vs. hand-written

- **Generated** (`tools/asm_parser/parse_tile_graphics.py`, `--all`):
  `src/data/generated/tile_graphics_data.inc` (the four extraction rows) and
  `tests/fixtures/tile_graphics_expected.h` (the same rows plus dimensions and content hashes).
  Regenerate after any upstream repin; do not hand-edit.
- **Hand-written:** `src/data/tile_graphics.h` (the `TileGraphic` type and format enum),
  `src/assets/extract.{h,cpp}` (the identity gate, the decode, the write),
  `src/assets/png_writer.{h,cpp}` (the greyscale PNG serialization).

### Transcription asserts

`parse_tile_graphics.py` hard-errors (with a citation) on any of: a ROM that is not exactly
32,768 bytes with the expected SHA-1; a `dump_gfx.py` whose `dump_tiles` calls are not exactly the
four expected `(name, format)` pairs in order; an offset expression that is not literal integer
arithmetic; a decode window escaping the ROM; or — the decisive one — **a decode at a block's
(offset, count, format) that does not reproduce the committed PNG pixel-for-pixel** (dimensions,
bit depth, and every sample). That last assert is what pins each offset: a wrong offset cannot
produce the committed pixels.

---

## Tested by

`tests/test_tile_graphics.cpp` — the full four-row sweep of table against fixture, the closed-form
geometry (and that every decode window fits the ROM), the decode sweep against the real ROM's
content hashes, the end-to-end extraction (into a scratch asset root, satisfying the presence check
and decoding back through the engine's loader to the fixture's dimensions and hashes), the wrong-ROM
and truncated-ROM refusals, the PNG serialization round-trip, and the SHA-1 against the FIPS 180
test vectors. Tests that read the ROM fail loudly when it is absent — never skip. The parser's own
tests (`tools/asm_parser/test_parse_tile_graphics.py`) guard the scan and the reference decode.
