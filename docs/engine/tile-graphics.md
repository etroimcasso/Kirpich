# Tile graphics

Kirpich's graphics are four greyscale PNGs derived from the Game Boy Tetris ROM. This page covers
the code that derives them: the extraction table naming which ROM bytes are which asset, the
extractor that turns a player's ROM into the files under `assets/gfx/default/`, and the PNG
serialization it writes with. For the flow that decides *when* extraction runs — the presence
check and the first-start prompt — see [assets.md](assets.md).

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/tile_graphics.h` | `TileGraphic`, `TileGraphicFormat`, `bytesPerTile`, and the `kTileGraphics` table | Hand-written (the types); the table is included from the generated file. |
| `src/data/generated/tile_graphics_data.inc` | The four extraction rows | **Generated — do not hand-edit.** |
| `src/assets/extract.h` / `.cpp` | `extractFromRom`, `decodeTileGraphic`, `sha1Hex` | Hand-written. |
| `src/assets/png_writer.h` / `.cpp` | `writeGreyscalePng` | Hand-written. Only `extract.cpp` consumes it. |
| `tests/fixtures/tile_graphics_expected.h` | The rows plus dimensions and content hashes, for the test sweep | **Generated — do not hand-edit.** |

The data table is in `namespace kirpich`; the extraction functions are in
`namespace kirpich::assets`.

## Using it

```cpp
#include "assets/extract.h"

const kirpich::assets::ExtractionResult result =
    kirpich::assets::extractFromRom(romPath);
if (!result.succeeded) {
    // result.message names the reason and states that nothing was written.
}
```

`extractFromRom` reads the file at `romPath`, refuses anything that is not the expected ROM
(exactly 32,768 bytes, SHA-1 `74591cc9501af93873f9a5d3eb12da12c0723bbc`) **before writing
anything**, then decodes all four blocks in memory and writes the four PNGs into
`assets/gfx/default/` under the current asset root — the same paths the presence check requires.
Every run rewrites all four files. `message` is player-facing either way: on success it lists what
was written and where; on refusal it names the reason and the expected ROM.

The pieces it is built from are public and individually testable:

- `decodeTileGraphic(rom, graphic)` decodes one table row out of the full ROM into row-major
  palette indices (the exact pixel content of that asset's PNG).
- `sha1Hex(bytes)` is the identity gate's hash — implemented in the port so a 32 KiB
  identity check does not pull a cryptography dependency into the stack.
- `writeGreyscalePng(indices, width, height, bitDepth)` serializes index samples to a greyscale
  PNG (bit depth 1 or 2, non-interlaced, uncompressed deflate blocks — the files are a few KiB, so
  compression buys nothing). The samples round-trip unscaled through the engine's loader.

The extraction table itself:

```cpp
#include "data/tile_graphics.h"

for (const kirpich::TileGraphic& g : kirpich::kTileGraphics) {
    // g.fileName, g.romOffset, g.tileCount, g.format
}
```

**The table drives extraction only — never loading.** Code that loads these graphics through the
engine's path-based load calls spells the full path as a string literal at the call site
(see [assets.md](assets.md) for the rule and why).

## Regenerating the table

The table and the test fixture are produced by the parser, which needs the disassembly checkout
*and* the ROM — it proves the two agree with the committed reference PNGs before it will emit
anything:

```sh
python3 tools/asm_parser/parse_tile_graphics.py \
  --source-root ../tetris \
  --rom "../rom/Tetris (World) (Rev 1).gb" \
  --all \
  --inc-out     src/data/generated/tile_graphics_data.inc \
  --fixture-out tests/fixtures/tile_graphics_expected.h
```

It reads the four `dump_tiles` facts out of the disassembly's own dumper, decodes the ROM at each
(offset, count, format), and stops with a citation unless every decode reproduces the
corresponding committed PNG pixel-for-pixel — so a wrong offset, a wrong format, or a wrong ROM
cannot emit a file. Python 3 (standard library only); a development tool, never needed to build or
test Kirpich.

## Provenance

The disassembly ships `dump_gfx.py`, the script that produced its committed reference PNGs; the
offsets and formats in the table were verified against it and against the ROM, and the parser's
cross-check above is what enforces that agreement on every regeneration. **`dump_gfx.py` is not
vendored and none of its code is copied** — the upstream repository carries no license. What is
taken from it is facts: four offsets and the Game Boy's bitplane layout, neither of which is
copyrightable. Upstream's dumper imports PyPNG; this port does not — the PNG serialization is the
port's own (`src/assets/png_writer.{h,cpp}`).

## Changing it

The offsets, counts, and formats are facts about the ROM, not tuning knobs. If an upstream repin
ever moved them, regenerate; the parser's cross-check is what says whether the new values are
right. The generated files are overwritten on the next run, so never hand-edit them.

To change what the *files* look like — resolution, layout, format — you would be changing the
contract both population routes share; read
[`../contracts/tile-graphics.md`](../contracts/tile-graphics.md) first, because the development
copy route ships upstream's PNGs and the extractor must keep producing identical content.

One current limit worth knowing: the engine decodes sub-byte greyscale rows at a byte-aligned
stride, so PNGs whose width is not a whole number of bytes at their bit depth do not round-trip
through it. Every asset here is 128 px wide and unaffected.

## Testing

`tests/test_tile_graphics.cpp` sweeps the table against the fixture, checks the closed-form
geometry, decodes the real ROM and compares content hashes, runs the extractor end-to-end into a
scratch asset root (through the presence check and the engine's own loader), verifies both
refusals write nothing, round-trips the PNG serialization, and pins the SHA-1 against the FIPS 180
vectors. The tests that read the ROM resolve it from the provisioning path
(`$HOME/ci-assets/kirpich/tetris.gb`; `C:\ci-assets\kirpich\tetris.gb` on Windows) or the
development sibling (`../rom/`), and **fail loudly when neither exists** — a missing ROM is a
provisioning failure, never a skip. The parser has its own tests
(`tools/asm_parser/test_parse_tile_graphics.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_tile_graphics`).
