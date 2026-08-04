# ROM extractor — design

Kirpich ships with no graphics. They are derived from the Game Boy Tetris ROM, so they are
never distributed; the player supplies them from a copy of the game they own, and this tool
is what reads them out of it.

**Status: designed, not built.** The flow that calls it is in place
(`src/assets/first_start.{h,cpp}`); `extractFromRom` is the seam it plugs into and currently
reports that extraction is unavailable rather than pretending to have written anything.

## Where it sits

Extraction is not a separate step the player performs. It happens inside the game, on first
launch, in `main()` before the engine is constructed:

1. `checkRequired()` — are the graphics present under the asset root?
2. If not, `promptForRom()` — the platform's native file-selection dialog. The player points
   at their own ROM.
3. `extractFromRom(path)` — **this tool**. Reads the ROM, decodes the tile data, writes the
   PNGs into `assets/gfx/default/`.
4. Startup continues into normal engine construction and asset loading — the same code path
   every later launch takes.

Nothing about this requires engine involvement, a restart, or any state that exists before
assets are loaded. The dev-populate scripts (`scripts/setup-dev-assets.*`) write the same
files to the same place, so a developer's daily run exercises the same load path a player's
install does.

## What it reads

The ROM is identified before anything is decoded, and a mismatch stops the run with a clear
message rather than producing subtly wrong output:

| | |
|---|---|
| Expected SHA1 | `74591cc9501af93873f9a5d3eb12da12c0723bbc` |
| Size | 32,768 bytes, no header |

Four graphics, at these offsets:

| Asset | Offset | Tiles | Format |
|---|---|---|---|
| `font` | `0x415F` | 39 | 1bpp |
| `copyrightandtitlescreen` | `0x42D7` | 119 | 2bpp |
| `configandgameplay` | `0x323F` | 197 | 2bpp |
| `multiplayerandburan` | `0x55AC` | 207 | 2bpp |

Each tile is 8×8. A 2bpp tile is 16 bytes — two bitplanes interleaved per row, low plane
first, the two bits of a pixel combining into a sample value of 0–3. A 1bpp tile is 8 bytes,
one plane, one bit per pixel. **The font is the only 1bpp asset and needs its own decode
path**; treating it as 2bpp silently produces garbage at twice the height.

## What it writes

Greyscale PNG, bit depth 2 for the 2bpp assets and 1 for the font, tiles laid out 16 per
row, into `assets/gfx/default/` at the logical paths listed in `src/assets/presence.h`.

The engine loads PNG, so PNG is what both population routes produce — no intermediate
`.2bpp` format and no conversion step at load time.

Python 3 standard library only, matching the rest of the port's tooling. PNG output is a
small `zlib`-based writer; there is no third-party imaging dependency.

## Provenance

The disassembly ships `dump_gfx.py`, which is what produced its committed PNGs, and the
offsets above were verified against it and against the ROM. **It is not vendored and its
code is not copied** — the upstream repository carries no license. What is used from it is
facts: four offsets and the Game Boy's bitplane layout, neither of which is copyrightable.
Upstream also imports PyPNG, which this tool does not.

## Open

- **ROM byte spans for the audio subsystem.** The sound driver and song data are one
  contiguous span (`0x6480`–`0x7FFF`, 7,040 bytes, copied verbatim with no decode) that the
  virtual machine consumes. Where it is written and in what container is settled alongside
  the audio work; nothing is built for it yet.
- **Re-extraction.** The first implementation writes everything on every run. Skipping files
  that already match is a later refinement.
- **Headered or modified ROMs.** The SHA1 check refuses them. Whether to detect a header and
  offer to strip it, rather than simply refusing, is a question for implementation time.
