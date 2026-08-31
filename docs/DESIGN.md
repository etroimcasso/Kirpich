# Kirpich — Design Context

Design decisions locked 2026-05-13, with a major revision on 2026-08-03 when the project adopted
the Polyrhythm engine as its platform boundary (§12). This document is the authoritative record of
project intent, the behavior-preservation contract, and the constraints every implementation
decision is measured against.

## 1. What this project is

A behavior-preserving native C++ port of the Game Boy (DMG) release of Tetris, built as a consumer
of the [Polyrhythm engine](https://github.com/RetroPlusPlus/Polyrhythm). The port reads the
[kaspermeerts/tetris](https://github.com/kaspermeerts/tetris) disassembly for intent, mechanics,
and data, and writes idiomatic modern C++ against the engine's surface — run loop, renderer, input,
audio, and CPU virtualization host. SDL3 with the SDL_GPU backend arrives transitively inside the
engine.

Two behaviors depend on cycle-exact execution of the original machine code and cannot be
reproduced by re-implementation: gameplay randomization and the original music/sound driver. Both
run on the engine's ROM-less SM83 virtual machine host (§10).

The shipped artifact is a game binary plus tooling that runs on macOS, Linux, and Windows and
reproduces the original ROM's observable behavior given the same inputs.

## 2. What this project is NOT

- **Not an emulator.** No full-system emulation, no PPU state machine, no memory-bank registers,
  no joypad / serial / timer interrupt emulation, no save-RAM banking, no memory mapper, no boot
  ROM, no save-state serialization. Emulators preserve hardware-level fidelity by simulating every
  chip; this port runs the game by native reimplementation. The narrow exception is the two
  routines named in §10 — a surgical tool, not a system emulator. The run loop, input, rendering,
  and all gameplay logic run as ordinary native C++.
- **Not a mechanical assembly-to-C++ translation.** Data tables become `constexpr` arrays; RAM
  structures become C++ structs; code paths become idiomatic functions; hardware-register effects
  are expressed at engine and renderer level. The virtualized routines do execute the original
  ROM bytes — but as extracted byte spans handed to the engine's VM, not as translated C++.
- **Not an emulator-adjacent project.** The disassembly is the behavioral contract, not the
  implementation target.

## 3. Upstream and pin

| Field | Value |
|---|---|
| Upstream repo | `https://github.com/kaspermeerts/tetris.git` |
| Pinned commit | `b95c66859339f5523e80213de5857eefc1c7703f` |
| Upstream license | None — the disassembly is published without a license file |
| Original ROM | `Tetris (JUE) (V1.1) [!].gb` — SHA1 `74591cc9501af93873f9a5d3eb12da12c0723bbc` |

The pinned disassembly checkout lives as a sibling directory outside this repository and is treated
as a read-only reference. It is never modified by port work, and the build never depends on it.

## 4. Hardware scope

Fixed by the ROM's actual properties — non-negotiable.

| Field | Value |
|---|---|
| Console | DMG (original Game Boy) |
| Memory mapper | None — 32 KB ROM, fixed banks 0 and 1, no bank switching |
| ROM size | 32 KB |
| Save RAM | None |
| Real-time clock | None |
| Super Game Boy code | None |
| Audio | Standard 4-channel sound unit (two square, wave, noise) |
| Display | 160 × 144, 4-shade greyscale |

This is the simplest Game Boy port surface there is — no paging, no battery-backed save, no clock,
no alternate-hardware code paths. Hardware complexity is bounded.

## 5. Behavior-preservation premise

**The port must produce the same observable behavior as the original ROM for any given input
sequence and RNG state.** This is the governing design rule. Every implementation decision is
evaluated against it. When something in the disassembly looks suboptimal, accidental, or buggy,
the default is to preserve it. Improvements are out of scope.

Concretely: a frame-by-frame capture of the port playing the same demo on the same seed must be
indistinguishable from the same capture taken from the original ROM. Sprite positions, tile
updates, scoring, level transitions, audio output, and RNG state evolution all match at the
observable boundary.

Two behaviors are identical by construction rather than by re-implementation:

- **Gameplay randomization.** The routine that picks pieces folds the DMG divider register, which
  ticks at 16384 Hz independently of the program counter. The byte stream the original produces
  depends on that register's exact value at the moment of the read, which depends on cycles since
  power-on. Native re-implementation cannot reproduce it, so the original routine runs on the
  engine's virtual SM83.
- **Music and sound effects.** The sound driver writes audio-channel registers on a cycle-driven
  cadence. Reproducing the output requires both CPU and audio-unit cycle accuracy, so the driver
  runs on the engine's virtual SM83 against its emulated audio unit.

Departures from behavior preservation appear only under the named options in §7. The opt-in Display
and Audio options affect how pixels and samples reach the display and speakers *after* the game has
produced them — never what the game produces. One always-on enhancement sits outside the opt-in set:
the port persists top scores across launches (§7, Persistence). It changes only what survives a power
cycle — never what the game produces for a given input sequence and RNG state within a session — and
the original's own soft-reset top-score survival is preserved in-sim.

## 6. Quirks preserved

Each of these is a documented behavior of the original and is non-negotiably part of the port's
correctness contract.

| Quirk | Description |
|---|---|
| Boot's routine-copy overrun | Init copies the sprite-transfer routine into high memory two bytes longer than the routine is. The routine itself does not over-transfer — the hardware fixes that transfer's length. The two extra bytes land on the game-type and music-type selection bytes, which Init overwrites a few instructions later on every path, so nothing can read them. Preserved as an equivalence, with the reachability argument in `contracts/boot.md` §8. |
| Top two rows never cleared | Line-clear logic checks 16 of 18 rows. The top two playfield rows are never cleared — invisible in normal play, observable in edge cases. Deliberate; preserved. |
| Multi-line clear duplicates top row | When several lines clear at once, the top row of the playfield is duplicated. Likely an oversight in the original; preserved. |
| Stereo panning preserved-as-broken | The ROM carries stereo panning data for the music, but the panning is non-functional in the shipped game. The data and the broken behavior both ship as-is; the virtualized driver runs the same code paths the original did. |
| Demo recording gated off | The ROM contains demo-recording code reachable only under a build flag never enabled in shipping cartridges. Preserved as dead-but-present code. |
| Bank write during init | Init performs a ROM-bank write even though the cartridge has no mapper. A no-op on hardware; preserved as a no-op. |

If further preserved behaviors surface, this section is amended in the same session as the
discovery.

## 7. Options (user opt-in, off by default)

These are the port's departures from the original beyond the display and speaker boundary. The
Display and Audio entries are user-facing options, opt-in and off by default, and compose at runtime —
none is mutually exclusive with any other within its category. The Persistence entry is the one
exception: it is always-on, not a toggle.

### Display

| Option | Description |
|---|---|
| Integer scale | Render the 160×144 framebuffer at integer multiples (×1, ×2, ×3, …). Preserves pixel-perfect geometry. |
| Free-aspect output | Non-integer scaling and stretching to fit arbitrary window sizes. Cohabits with integer scale; one is active at a time. |
| DMG display shader | Reproduces the original LCD's optical character — greenish tint, ghosting, dot grid. DMG only, since the ROM is DMG only. |

**CRT shaders are explicitly not offered.** The Game Boy was never displayed on a CRT.

**Pixel-art upscaling shaders are not offered either** — the HQ, EQ, xBRZ and ScaleNx families. The
engine already presents the 160×144 framebuffer at integer multiples with no interpolation, which is
what keeps the art crisp; those families exist to smooth pixel art, which is the opposite of what
this game wants.

Composition order: render 160×144 → integer or free-aspect scale → DMG shader → present.

### Audio

| Option | Description |
|---|---|
| Anti-channel-stealing | Off by default, which preserves the original's channel stealing byte-for-byte — exactly how the ROM mixes music and effects across four channels. When enabled, music and effects are hosted as separate driver instances so an effect never costs the music a voice. See [`features/anti-channel-stealing.md`](features/anti-channel-stealing.md). |

### Persistence (always-on, added 2026-08-14)

The original keeps top scores in RAM only: a power cycle clears them, while a soft reset deliberately
skips the clear so they survive it and nothing else. The port persists top scores across launches,
always on — there is no toggle. This strictly extends the original's soft-reset survival; the in-sim
top-score tables behave byte-for-byte as the original's, and the soft-reset survival is itself
preserved.

| Aspect | Value |
|---|---|
| What persists | The two top-score tables only (Type B and Type A). In-session score-entry state does not persist. |
| Storage | The engine's durable save store — one named, schema-versioned document, written atomically to a per-user directory. No file I/O is written port-side. |
| Format | The exact top-score table image (1890 bytes), schema version 1. |
| Save identity | `Kirpich` / `Kirpich` — the per-user directory is `<platform data dir>/Kirpich/Kirpich/`. Locked permanently (a changed identity strands players' existing saves). |
| Corrupt-save policy | A corrupt or wrong-length save is never treated as absent and never silently overwritten: the game runs with no saved scores and leaves the damaged file in place until a new top score is earned. |

See [`features/high-score-state.md`](features/high-score-state.md) and
[`contracts/high-score-state.md`](contracts/high-score-state.md).

## 8. Technical decisions

Each is a one-way door — changing one means revisiting this document, not just an implementation
file.

| Decision | Value |
|---|---|
| Approach | Modern C++ reimplementation consuming the Polyrhythm engine. Read the disassembly for intent, mechanics, and data; write idiomatic C++ against the `retropp::` surface. Not mechanical translation. |
| Platform layer | The Polyrhythm engine, consumed as a git submodule via `add_subdirectory(engine)` + `retropp::engine`. SDL3 with the SDL_GPU backend is engine-internal; the port declares no SDL3 of its own (a second provider of `SDL3::SDL3` is a configure-time error). Direct SDL calls are permitted where the engine has no opinion; the first-start ROM picker (`SDL_ShowOpenFileDialog` in `src/assets/first_start.cpp`) is the only one today. |
| C++ standard | C++20 |
| Build | CMake 3.28+ with Ninja |
| Tests | GoogleTest |
| Logging | spdlog, used directly — no wrapper. The engine exposes no logging surface, and a facade adds ceremony without value at this size. |
| CPU virtualization | The engine's VM host (`retropp::Vm`), SameBoy-backed and ROM-less: routines are registered as extracted byte spans or engine presets and called as typed C++ functions. No game ROM is ever loaded into an emulator. |
| Simulation rate | True DMG frame rate — 70,224 CPU cycles at 4,194,304 Hz, i.e. 59.7275 Hz — via the engine's Game Boy timing profile. |
| Threading | Single-threaded main loop on the platform thread. Virtual-machine calls are synchronous from the game's perspective. |
| Hardware target | DMG |
| Hardware-register variables | **None in port code.** `rLCDC`, `rSCX`, `rSCY`, `rIE`, `rIF`, `rNR10`–`rNR52` and friends do not exist as variables anywhere under `src/`. Their effects are expressed at engine and renderer level. Registers and addresses appear only inside the engine's VM boundary — in a routine's byte-span registration, never at a call site. |
| Audio | Chiptune only. The engine's audio system hosts the ROM's sound driver on its internal VM and produces PCM into the engine mixer. No audio-file replacement backend. |
| Asset posture | Single canonical path. `assets/gfx/default/` for tile graphics; the byte spans the VM needs load from their own fixed path. No swappable packs, no manifest, no discovery or fallback chain. |
| License posture | AGPL-3.0 (`LICENSE` at the repo root; amended 2026-08-12 from the earlier all-rights-reserved interim posture), matching the Polyrhythm engine's open-source license — the whole distributed build is one AGPL combined work. The engine itself is dual-licensed (AGPL-3.0 / commercial, per its `LICENSING.md`); the upstream disassembly is published without a license. |
| Repository posture | Standalone repository. The disassembly is a sibling checkout outside the tree; this is not a fork of it. |

## 9. Asset posture

The engine loads its tile graphics from one canonical path: `assets/gfx/default/`. The directory
is committed via `.gitkeep`; its contents are gitignored. The virtual machine consumes ROM byte
spans for the routines it hosts — the randomization routine, the sound driver, and the song and
effect data — from a separate fixed path. Those bytes come from the same user-side acquisition
route: extracted on the user's machine at runtime, never shipped in the binary.

**Development:** a checked-in setup script copies tile graphics from the sibling disassembly
checkout into `assets/gfx/default/` and the required byte spans into the engine input path. A
developer runs it once after cloning. The engine then loads from those paths exactly as a user's
installation will — there is no development/production branch in the load path.

**Shipping:** the distributable build target empties the asset content before packaging. The
directory structure persists as empty targets in the shipped artifact, and a packaging check fails
if any development-populated byte appears in the output.

**User runtime:** the extraction tool ships with the game, runs on the user's machine against
their legitimately owned ROM, and writes both the tile graphics and the byte spans to the same
paths the development script populates. Manual placement is also supported using the documented
layout. Starting with assets missing produces a clear error pointing at either route.

Full design: [`features/asset-acquisition.md`](features/asset-acquisition.md).

## 10. Virtualized routines and audio architecture

| Routine | Throttling | Why it is virtualized |
|---|---|---|
| Piece randomization | Runs at host speed — the output byte stream is identical at any clock rate, because the CPU-cycle to divider-tick ratio is preserved | A byte-identical piece sequence requires cycle accuracy and the divider-register tick model. Re-implementation cannot reproduce it. |
| Music and sound driver | Throttled to 4.194304 MHz; the audio sink consumes output at the device's real-time sample rate | Chiptune fidelity requires CPU and audio-unit cycle accuracy plus correct alignment to the output sink rate. |

**Backend.** [SameBoy](https://sameboy.github.io) (MIT-licensed), providing cycle-accurate SM83
and audio-unit emulation. It is owned by the engine — the port never touches it directly.
Alternatives considered and rejected: a hand-written minimal SM83 interpreter (would require
writing audio-unit emulation too, doubling the surface and the correctness risk); QEMU (no
first-class SM83 support — its Z80 support does not transfer, since the instruction sets differ);
other emulator cores (licensing and extraction-effort tradeoffs against SameBoy's permissive
license and library-friendly C API).

**Randomization.** The engine ships a preset implementing the dual-seed divider-folding algorithm
this ROM uses; the port registers it with no binding boilerplate. The selection logic around the
raw random byte — the re-roll against previous pieces and the masking — is deterministic given
the byte stream and is written as native C++.

**Audio.** The sound driver registers with the engine's audio system, which hosts it on an
internal VM at the correct cadence and produces PCM into the engine mixer and sink. Throttling and
the alignment between driver-write rate, sample-production rate, and output-device rate are engine
concerns. The port's job is registering the driver and its data spans. Details in
[`features/audio-engine.md`](features/audio-engine.md).

## 11. Hard prohibitions

Non-negotiable. Every implementation decision respects these without exception.

1. **No full-system emulation outside the VM.** The main loop, rendering, input, and game logic
   run as native C++. The VM exists only for the routines named in §10; extending it to others
   requires amending this document.
2. **No hardware-register variables in native port code.** Registers do not appear as variables or
   memory-mapped abstractions in any file under `src/`. Their effects are expressed at engine and
   renderer level — a vertical-blank interrupt becomes the engine's simulation-tick callback, a
   display-control bit becomes renderer draw state. Registers exist only inside the engine's VM
   boundary, in a routine's registration binding, never at a call site.
3. **No PPU state machine anywhere.** Pixel output is the renderer's responsibility; the renderer
   does not simulate the DMG's pixel pipeline. The VM's own display hardware stays dormant — it is
   invoked for CPU and audio routines only.
4. **The audio state machine lives only inside the VM.** No code outside it emulates the sound
   channels. The port never implements its own four-channel synthesizer.
5. **No bank-switching logic.** This ROM has no mapper. Bank-switching code in the disassembly is
   either dead code preserved as a no-op or a single-bank assumption that does not translate into
   a feature.
6. **No copyrighted content in shipped artifacts.** Binary, tooling, source, and documentation
   only. The byte spans the VM consumes arrive by user-runtime extraction, never in the binary.
7. **Never modify the upstream disassembly.** It is a read-only reference.
8. **No decompiler or transpiler output in native code.** Any C++ originating from a mechanical
   translation pass is rejected. Native code is written by hand from reading the disassembly for
   intent. The virtualized routines are different: they execute the original bytes.

## 12. Engine adoption (2026-08-03)

The port's platform boundary is the Polyrhythm engine, consumed as a submodule pinned at `5e63115`.
This superseded a direct-SDL3 platform facade, a port-local SameBoy integration, and a hand-built
presentation pipeline — all of which duplicated, less well, what the engine already ships and
tests.

| Superseded | Replaced by |
|---|---|
| Direct SDL3 and a port-local platform facade | `retropp::SdlPlatform` / `WindowedHost` / `Renderer`; the port declares no SDL3 |
| Port-local SameBoy VM integration | `retropp::Vm` and its preset catalog |
| Port-local audio-unit integration | The engine audio system — VM-hosted driver, engine-owned throttling and rate alignment, mixer |
| Flat 60 Hz simulation tick | The engine's Game Boy timing profile — true 59.7275 Hz |
| Hand-built SDL_GPU presentation pipeline | Engine viewport, output-scaling, and shader stages |
| A logging facade | Direct spdlog |
| SameBoy fetched by the port | The engine's own nested submodule |

Unchanged by the adoption: behavior preservation (§5), the quirks list (§6), chiptune-only audio,
the single asset path, anti-channel-stealing as a shipped option (§7), asset posture (§9), the
hard prohibitions (§11), the upstream reference and its pin (§3), and the data and state layer
scope.

**Consumer-first surfaces.** The engine's rendering, input, and run loop are proven in production
by an existing consumer. Its audio and VM surfaces have engine-side tests and demos but no game
consumer yet — this port is the first. Any gap found there is filed upstream as engine work, not
worked around here. Watch items: the randomization preset's seeding semantics against this ROM's
exact initialization, and driver-registration fit for the ROM's sound code.

Full record: [`features/engine-adoption.md`](features/engine-adoption.md).

## 13. What must be preserved

Summary of non-negotiables, each linking back to the section that owns the detail.

- **Behavior preservation** — same observable outputs given the same inputs (§5).
- **The full quirks list** (§6).
- **The virtualized routine list and the VM backend** (§10). Native re-implementation of either
  routine is forbidden.
- **Anti-channel-stealing ships as an option**, off by default, operating on the chiptune path
  (§7). It is not covered by the chiptune-only or single-asset-path reductions.
- **The SDL_GPU backend** — required by the display shader (§7).
- **All display options compose** — none is mutually exclusive within its category (§7).
- **No copyrighted content ships** — enforced structurally by emptying assets at packaging time
  and by the ROM-extension bans in `.gitignore` (§9).
- **The hard prohibitions** (§11).
- **Two deliberate reductions from the general shape of a project like this:** chiptune-only audio
  (no audio-file replacement backend) and a single asset path (no swappable packs, no manifest, no
  selection). Both locked 2026-05-15. Anti-channel-stealing is *not* one of them — it ships.

Anything conflicting with the above is raised as an amendment to this document, not implemented
silently.
