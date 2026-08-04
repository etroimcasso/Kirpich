# Asset Acquisition

**Date:** 2026-05-14; revised 2026-05-15 (single asset path, no pack system)
**Status:** In design

How the game gets the graphics and ROM byte spans it needs, without any copyrighted content
entering this repository or a shipped build.

## Concept

The engine loads tile graphics from one canonical path: `assets/gfx/default/`. The virtual machine
consumes ROM byte spans — the randomization routine, the sound driver, and the per-song and
per-effect data — from a separate fixed path. Both are populated either by the extraction tool
(on the user's machine, against a ROM they own) or by manual placement.

**No pack system.** No swappable graphics or audio packs, no pack discovery, no manifest, no
fallback chain. The engine reads the canonical paths or it reports an error.

## Design decisions

### Single path rather than swappable packs

A pack model would bring a registry, a settings UI for pack selection, a manifest schema, and a
fallback resolver. This game has one set of tile graphics and one set of ROM byte spans; that
machinery would carry cost with nothing to spend it on.

**Rejected:** keeping pack infrastructure present but inert ("ships, but only one pack is ever
populated"). Inert infrastructure invites later activation and drifts the project off the decision.
Omitting it entirely is cleaner; re-adding it later, if a real case ever appears, is its own design
exercise.

### Two population routes, one layout

- **Development.** A checked-in setup script (`scripts/setup-dev-assets.{sh,ps1}`) copies graphics
  from the sibling disassembly checkout into `assets/gfx/default/`, and the required ROM byte
  spans into the engine input path. A developer runs it once after cloning. This content is
  ROM-derived and is never shipped.
- **User runtime.** The extraction tool (`tools/rom_extractor/`) ships with the game, runs against
  the user's legitimately owned ROM, and writes the same files to the same paths.

The engine reads those paths in both cases — there is no development/production branch in the load
path, so a developer's daily run exercises exactly the code a user will.

### Loading through engine surfaces

Tile graphics load through the engine's asset-root and image-loading surfaces; ROM byte spans load
from their fixed input path. A thin port-side wrapper is added only if the engine surface turns out
not to cover the need directly. No registry, no manifest, no fallback chain.

### Missing-asset error

When the game starts and either path lacks required content, it reports a clear error pointing at:

1. **Primary** — the extraction tool: *"Run the extractor against your Tetris ROM to populate the
   asset paths."*
2. **Fallback** — manual placement, per the documented layout.

No silent failure, no placeholder content, no bundled fallback assets.

### The distributable ships empty asset directories

The distributable build target empties `assets/gfx/default/` and the byte-span input directory
before packaging, retaining the `.gitkeep` placeholders so the structure ships. A packaging check
fails if any byte of development-populated content appears in the artifact.

### Extraction tool — design pinned, implementation later

**Pinned design:**

- Takes a ROM path on the command line.
- Verifies the SHA1 against the expected value (`74591cc9501af93873f9a5d3eb12da12c0723bbc`) and
  fails fast on a mismatch.
- Extracts every referenced tile-graphics asset at the offsets the disassembly identifies, writing
  to `assets/gfx/default/`.
- Extracts the byte ranges the virtual machine needs — the randomization routine, the sound driver,
  and the song and effect data — writing to the engine input path. Exact ranges are fixed when the
  audio backend is built.
- Python 3 standard library only, matching the rest of the development tooling.

The offset table is derived from the disassembly's binary-include declarations plus the byte ranges
identified when the audio work lands.

## Implementation details

**Files:**

- `scripts/setup-dev-assets.sh` — POSIX shell; run once after cloning.
- `scripts/setup-dev-assets.ps1` — Windows equivalent.
- `tools/rom_extractor/` — extraction tool source and README.
- `assets/gfx/default/.gitkeep` — directory placeholder for tile graphics.

**Constants:**

- Tile-graphics path: `assets/gfx/default/`
- Byte-span input path: fixed when the audio backend is built
- Expected ROM SHA1: `74591cc9501af93873f9a5d3eb12da12c0723bbc`

## Open questions

- **Byte-span path location and format.** Settled alongside the audio backend work.
- **Incremental extraction.** The first implementation re-extracts everything on every run.
  Skipping files that already match their expected hash is a later refinement.
- **Pack model.** Explicitly not planned. If a real case for swappable packs ever appears, it is a
  separate design decision with its own amendment to `../DESIGN.md`.
