# Renderer Capability Audit — Tetris Observables → Retro++ Surface

**Date:** 2026-08-03
**Status:** Complete
**Inputs:** a full survey of PPU usage in the Tetris disassembly (`kaspermeerts/tetris` @ `b95c668`
— all PPU-register access lives in `tetris.asm`; `sprites.asm` holds sprite data only, and the audio
sources touch no video state); Retro++ public headers at engine `d4a6091`.
**Feeds:** the rendering design, and engine work-item sequencing — of which there is none, see the
verdict.

This document maps every renderer-visible behavior in Game Boy Tetris onto the Retro++ engine's
drawing vocabulary and issues one verdict per behavior region:

- **Covered** — the engine expresses the observable directly.
- **Covered with pattern** — the engine expresses it through a documented composition of existing
  surfaces (the pattern is named).
- **Engine work item** — a genuine capability gap; named in engine terms at the end.

## Ground rule: observables, not mechanisms

The port reproduces what the player *sees*, using the engine's native vocabulary. DMG delivery
mechanisms — status-mode polling before VRAM access, vertical-blank-window tilemap streaming, the
sprite DMA transfer and its HRAM trampoline, scanline polling before disabling the LCD — do not
carry into the port's code in any form. Where a mechanism produces a visible effect, the *effect* is
mapped; where it produces none, the port is not involved.

All source citations are `file:line` into the pinned disassembly.

## The headline finding

Tetris uses the DMG PPU in its plainest possible configuration, and never departs from it:

- **No scrolling, ever.** `rSCX`/`rSCY` are written exactly four times, all zeroes: at init
  (`tetris.asm:282-283`) and in a redundant per-frame re-zero in the frame handler
  (`tetris.asm:254-255`, commented "Unnecessary, but whatever").
- **No window, ever.** `rWY`/`rWX` are zeroed once at init (`tetris.asm:382-383`); no display-control
  value used anywhere enables the window (the observed values are `$D3`, `$DB`, `$93`, plus
  transient init values — bit 5 clear in all).
- **No raster effects, ever.** `rSTAT` is written zero (no status interrupt sources), `rLYC` is never
  touched, and interrupts enable only vertical blank and serial (`tetris.asm:369,409,1300,…`). Every
  `rSTAT`/`rLY` read is VRAM-access-safety polling — mechanism only.
- **One tile-data bank.** Every display-control value sets bit 4, so background tile data always
  reads unsigned from `$8000`; the signed mode is never used.
- **Fixed palettes for the entire game.** `rBGP = %11100100` (identity), `rOBP0 = %11100100`,
  `rOBP1 = %11000100`, each written exactly once, at init (`tetris.asm:296-300`). No palette
  animation, no fades.
- **8×8 sprites only.** No display-control value selects 8×16 (bit 2 clear in `$D3`/`$DB`/`$93`).

The whole game is a static 160×144 tile screen chosen from two prepared background maps, plus a
handful of 8×8 sprites, with all animation expressed as tile-content changes and sprite moves. That
sits squarely inside the engine's shipped vocabulary — an existing engine consumer already exercises
a strict superset of every surface named below, in production.

## Engine vocabulary (reference)

| Surface | What it gives |
|---|---|
| `FrameDrawState` / `DrawLayer` + `TileCell` | Z-ordered tile layers; the whole frame is recomputed and submitted per frame |
| `ViewportResolution::GameBoy` | 160×144 internal viewport preset |
| `Sprite` + `AssetDimensions::GameBoy8x8` | Per-sprite z / palette / transform; 8×8 is the engine default |
| `TransparentIndices::GameBoy` | Color-0 sprite transparency, opt-in per sheet |
| Indexed atlas + `PaletteId` | Runtime palettes; shade→RGBA resolution happens at palette upload |
| Integer / letterbox output blit | The integer-scale and free-aspect options |
| Shader stages | The pixel-art and DMG display shaders |

## Region-by-region mapping

### 1. Screen composition — one static background layer — **Covered**

No scroll, no window, nothing mid-frame (headline finding). Each screen is one full-screen tilemap —
title, config, difficulty selects, gameplay, scoreboards, victory and defeat scenes — loaded via
`LoadTilemap` and its siblings. This maps to a single `DrawLayer` of tile content at
`ViewportResolution::GameBoy`, rebuilt and submitted per tick. The fixed camera is the degenerate
case of the engine's scroll surface, with scroll pinned at zero.

### 2. Two background maps — gameplay `$9800` / alternate `$9C00` — **Covered**

The game keeps two prepared screens resident and flips between them via the map-select bit:

- Standard screens use map `$9800` (the `$D3`/`$93` writes — e.g. `tetris.asm:494,2926`).
- The rocket-launch and victory sequence switches to `$9C00` (`$DB` — "Re-enable LCD, and switch
  tilemap to 9C00", `tetris.asm:2718-2719,2952`).
- **Pause** flips the bit live (`set 3, [rLCDC]`, `tetris.asm:4454-4461`): the pause screen is
  pre-built on the second map, and the current lines-count digits are copied across so it reads
  correctly (`tetris.asm:4464-4475`).
- The multiplayer scoreboard writes score digits into the second map from the frame handler
  (`tetris.asm:246`).

The observable is "the whole screen's content swaps instantly, and some cells mirror live game
values." The port holds both screens as game state and submits whichever is active as the layer's
tile content — a per-frame data choice, needing no special surface. The pause digit-copy becomes
"draw the same digits into the pause screen's cells."

### 3. Sprites — **Covered**

Always 8×8 (headline finding). Sprite entries are produced each frame into a buffer by the game's
own sprite renderer (`_RenderSprites`, `tetris.asm:6687` onward), which walks per-sprite state blocks
and sprite-list tables (`sprites.asm`) and applies a per-sprite attribute byte — the `$FD` sentinel
horizontally flips mirrored halves via `xor $20` (`tetris.asm:6757-6760`). Two object palettes exist
and are fixed. The attribute byte is data-driven, so exact flip, priority, and palette-select usage
is pinned when the sprite data and sprite renderer are ported.

Each emitted entry becomes a `retropp::Sprite` (`GameBoy8x8` is the engine default), with per-sprite
`PaletteId` for the two object palettes, flips expressed as a sprite transform, color-0 holes via
`TransparentIndices::GameBoy`, and entry order mapped to `z`. The game's "build the sprite buffer
every frame" model is exactly the engine's "build draw state every frame" model — the port's sprite
renderer emits `Sprite` records instead of raw entries.

One watch item, not a gap: **if** the sprite data turns out to use the background-over-object
priority bit (a sprite behind background colors 1–3), the mapping is a tile layer above the sprite z
with transparent color 0 — a z-ordering pattern, still on shipped surface. The survey found no
reliance on the DMG's ten-sprites-per-scanline overflow behavior (flicker-as-feature); that is
re-confirmed against the sprite population sites when the renderer is built.

### 4. Playing-field animation — tile content as game state — **Covered**

Everything that moves on the background — piece lock-in, the line-clear flash and collapse, the
playing-field wipe patterns, the demo dancers' background frames, the rocket-launch scenery — is the
game rewriting tilemap cells over successive frames. Streaming during the vertical-blank window is
the mechanism; the frame-by-frame cell values are the observable. The port keeps the playfield and
screen cells as game state and submits them as tile content per tick, and the animation timing ports
along with the game logic that owns it. No renderer surface beyond §1 is involved.

### 5. Transition blanks — LCD off during screen loads — **Covered**

`DisableLCD` (`tetris.asm:6461-6475`) blanks the panel white while bulk tile and tilemap loads run,
and the new screen appears on re-enable. The observable is a brief white screen between scenes,
which maps to a whole-frame white fill — or simply to submitting the new frame. Whether the
hardware's white gap is part of the preserved timing is a per-transition decision for the screen-flow
contracts. Either way it is a data and timing choice on shipped surface.

### 6. DMG shade presentation and display options — **Covered**

The four-shade output is the palette upload's concern: indexed art plus a four-entry palette whose
RGBA values realize the chosen shade ramp. The opt-in display options — integer scale, free-aspect
output, pixel-art upscaling shaders, and the DMG display shader — ride the engine's output blit and
shader-stage surfaces. Nothing in the disassembly's PPU usage constrains them.

### 7. Non-renderer mechanisms — no engine surface required

For completeness: status-mode polling before VRAM writes (`tetris.asm:160-167,4469-4471,…`); the
vertical-blank-gated tilemap streaming; the sprite DMA trampoline and its documented two-byte
over-copy quirk (`DMARoutine`, `tetris.asm:6676-6684` — harmless on hardware, no observable,
preserved as a documented quirk per `DESIGN.md` §6); scanline polling in `DisableLCD`; interrupt
flag manipulation around loads; and the serial-interrupt wiring for multiplayer, which is an input
and timing concern rather than a renderer one. All are mechanisms whose observables are accounted
for above, or that have none.

## Engine work items

**None.** Every renderer-visible behavior in Tetris maps onto the engine's shipped surface; the
survey found no capability gap. Tetris's PPU usage is a small subset of what an existing engine
consumer already exercises in production.

Watch items carried forward to the work that will resolve them, neither of which forecasts engine
work:

- Background-over-object priority-bit usage (§3) — a z-ordering pattern if present.
- Per-transition white-blank preservation (§5) — a contract decision.

## Verdict summary

| # | Behavior region | Verdict |
|---|---|---|
| 1 | Screen composition (static; no scroll, window, or raster effects) | Covered |
| 2 | Two background maps (`$9800`/`$9C00`, pause flip, live digits) | Covered |
| 3 | Sprites (8×8, flips, two fixed palettes) | Covered |
| 4 | Playing-field and tile-content animation | Covered |
| 5 | Transition blanks (LCD off) | Covered |
| 6 | DMG shades and display options | Covered |
| 7 | Non-renderer mechanisms | No renderer surface needed |

Zero engine work items. The renderer side of this port is pure consumption of shipped,
production-proven engine surface.
