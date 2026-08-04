# Asset Acquisition

**Date:** 2026-05-14; revised 2026-05-15 (single asset path, no pack system); revised
2026-08-03 (first-start ROM selection replaces the manual tool step)
**Status:** Presence check and first-start flow implemented; extraction is scheduled work

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
- **User runtime — first-start ROM selection, in-app.** On a launch that finds the asset paths
  empty, the game itself asks for the ROM: a native file-selection dialog, extraction inside the
  application, the identical layout written to the identical paths, and then the game proceeds.
  There is no manual tool step in the player's experience. The extraction code may additionally
  exist as a standalone command-line program for power users — decided at implementation time —
  but the first-start flow is the canonical path.

The engine reads those paths in both cases — there is no development/production branch in the load
path, so a developer's daily run exercises exactly the code a user will.

### Loading through engine surfaces

Tile graphics load through the engine's asset-root and image-loading surfaces; ROM byte spans load
from their fixed input path. No registry, no manifest, no fallback chain.

**No port-side loading wrapper** — settled 2026-08-03 against the engine as it stands. The engine
already models this exact case: its load-from-path policy exists for content that may never be
baked into a binary, image atlases default to it, and the asset root is a runtime base resolved
once at startup and overridable — the project tree during development, the extracted-asset
directory for an installed game. The port sets that root and addresses assets by logical path;
there is nothing left for a wrapper to do. The port-side asset module exists only for the presence
check, which is a different job.

### First-start sequencing — port-side only, no engine modification

When the game starts and required content is absent, it does not stop with an error telling the
player to go and run something. It asks for the ROM and gets on with it — the same first-start
model players already know from Ship of Harkinian and the Zelda 64 recompilation.

An engine-side bootstrap surface was considered and **rejected**: the engine is a library the
port's `main()` drives, and asset loading happens only when the port asks for it, so nothing about
this needs engine involvement, a restart, or any pre-asset engine state. The sequence is plain
port-side control flow:

1. `main()` checks the asset paths for content (`kirpich::assets::checkRequired`).
2. Anything missing → run the flow: show the platform's file-selection dialog, extract from the
   ROM the player chooses, write the layout (`kirpich::assets::ensureAssetsPresent`).
3. Proceed into normal engine construction and asset loading — the same code path every later
   launch takes.

The dialog is SDL's (`SDL_ShowOpenFileDialog`), which gives each platform its own native picker.
The port calls it directly; the engine neither provides nor needs a surface for it.

**Never a silent failure, never placeholder content, never a bundled fallback asset, and never a
bare error message pointing at a tool the player has to go and find.** Text shown during the flow
names what is missing and states plainly that the ROM is only read — never copied, moved, or
altered.

Until the extractor is implemented, the flow runs to the extraction step and reports honestly that
it cannot proceed, rather than reporting success and then failing to load. Manual placement per the
documented layout is the interim route.

### The distributable ships empty asset directories

The distributable build target empties `assets/gfx/default/` and the byte-span input directory
before packaging, retaining the `.gitkeep` placeholders so the structure ships. A packaging check
fails if any byte of development-populated content appears in the artifact.

### Extraction tool — design pinned, implementation later

Full design in `../../tools/rom_extractor/README.md` — offsets, tile formats, output format,
provenance. **Pinned design:**

- Invoked by the first-start flow with the ROM path the player chose. A standalone command-line
  entry point is optional and secondary.
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

- `src/assets/presence.{h,cpp}` — the required-asset manifest and the presence check. Not a
  loader: no decode, no renderer, no window, so it runs before anything is constructed.
- `src/assets/first_start.{h,cpp}` — the first-start flow: the file dialog, the extractor seam,
  and the sequencing around them.
- `scripts/setup-dev-assets.sh` — POSIX shell; run once after cloning.
- `scripts/setup-dev-assets.ps1` — Windows equivalent; the same source→destination table.
- `scripts/check-distributable-clean.sh` — fails if anything but `.gitkeep` is in the asset
  directories; the packaging gate.
- `tools/rom_extractor/` — extraction tool design and (later) source.
- `tests/fixtures/tiny_probe.png` — an 8×8 2-bit greyscale PNG authored for this repository and
  derived from nothing, so the load path is tested on every platform with no copyrighted byte and
  no skipped test.
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
