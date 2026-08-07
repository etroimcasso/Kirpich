# ROM extractor

Kirpich ships with no graphics. They are derived from the Game Boy Tetris ROM, so they are
never distributed; the player supplies them from a copy of the game they own, and the
extractor is what reads them out of it.

**Status: built, for graphics.** The extractor is part of the game itself —
`src/assets/extract.{h,cpp}`, called by the first-start flow — not a separate program in this
directory. This directory holds its design record; the audio byte-span half remains open (below).

## Where it sits

Extraction is not a separate step the player performs. It happens inside the game, on first
launch, in `main()` before the engine is constructed:

1. `checkRequired()` — are the graphics present under the asset root?
2. If not, `promptForRom()` — the platform's native file-selection dialog. The player points
   at their own ROM.
3. `extractFromRom(path)` — reads the ROM, decodes the tile data, writes the PNGs into
   `assets/gfx/default/`.
4. Startup continues into normal engine construction and asset loading — the same code path
   every later launch takes.

Nothing about this requires engine involvement, a restart, or any state that exists before
assets are loaded. The dev-populate scripts (`scripts/setup-dev-assets.*`) write the same
files to the same place, so a developer's daily run exercises the same load path a player's
install does.

## What it reads

The ROM is identified before anything is decoded, and a mismatch stops the run with a clear
message rather than producing subtly wrong output. There is no header detection or stripping:
anything that is not exactly this ROM is refused, with nothing written.

| | |
|---|---|
| Expected SHA1 | `74591cc9501af93873f9a5d3eb12da12c0723bbc` |
| Size | 32,768 bytes, no header |

Four graphics, at these offsets:

| Asset | Offset | Tiles | Format |
|---|---|---|---|
| `font` | `0x415F` | 39 | 1bpp |
| `copyrightandtitlescreen` | `0x4297` | 119 | 2bpp |
| `configandgameplay` | `0x323F` | 197 | 2bpp |
| `multiplayerandburan` | `0x55AC` | 207 | 2bpp |

Each tile is 8×8. A 2bpp tile is 16 bytes — two bitplanes interleaved per row, low plane
first, the two bits of a pixel combining into a sample value of 0–3. A 1bpp tile is 8 bytes,
one plane, one bit per pixel. **The font is the only 1bpp asset and gets its own decode
path**; treating it as 2bpp silently produces garbage at twice the height.

## What it writes

Greyscale PNG, bit depth 2 for the 2bpp assets and 1 for the font, tiles laid out 16 per
row, into `assets/gfx/default/` at the paths `checkRequired()` names in `src/assets/presence.cpp`.
Every run rewrites all four files, and all four are decoded in memory before the first one is
written, so a refusal or failure leaves nothing half-populated.

The engine loads PNG, so PNG is what both population routes produce — no intermediate
`.2bpp` format and no conversion step at load time. The PNG serialization is the port's own
(`src/assets/png_writer.{h,cpp}`): greyscale, non-interlaced, uncompressed deflate blocks —
the files are a few KiB, so compression buys nothing. The extraction facts live in the
generated table `kTileGraphics` (`src/data/tile_graphics.h`); the full behavioral
specification is [`../../docs/contracts/tile-graphics.md`](../../docs/contracts/tile-graphics.md).

## Provenance

The disassembly ships `dump_gfx.py`, which is what produced its committed PNGs, and the
offsets above were verified against it and against the ROM — the table generator refuses to
emit unless decoding the ROM at each offset reproduces the disassembly's committed PNGs
pixel-for-pixel. **`dump_gfx.py` is not vendored and its code is not copied** — the upstream
repository carries no license. What is used from it is facts: four offsets and the Game Boy's
bitplane layout, neither of which is copyrightable. Upstream also imports PyPNG, which this
port does not.

## Open

- **ROM byte spans for the audio subsystem.** The sound driver and song data are one
  contiguous span (`0x6480`–`0x7FFF`, 7,040 bytes, copied verbatim with no decode) that the
  virtual machine consumes. The same extractor grows that output when the audio work fixes
  where it is written and in what container; nothing is built for it yet.
