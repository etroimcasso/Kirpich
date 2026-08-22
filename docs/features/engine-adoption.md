# Retro++ Engine Adoption

**Date:** 2026-08-03
**Status:** Complete

## Concept

The port consumes the [Retro++ engine](https://github.com/RetroPlusPlus/Engine) as its platform
boundary, replacing the port-local infrastructure built earlier. Retro++ is a shared native engine
for 8- and 16-bit tile-based ports, supplying the run loop, platform and windowing, input,
renderer, audio chain, persistence, and a ROM-less SM83 virtual-machine host.

## Design decisions

- **Consumer checkout, not a fork.** The submodule tracks the engine's own repository, pinned at
  `5e63115`. Engine development happens in the engine's home repository; gaps this port finds are
  filed upstream as engine work rather than worked around here.
- **The port declares no SDL3.** SDL3 and SameBoy arrive as the engine's own nested submodules. A
  second provider of the `SDL3::SDL3` target is a configure-time error, which enforces the boundary
  structurally rather than by convention.
- **The old platform facade was deleted, not adapted.** `src/platform/` duplicated the engine's
  platform, host, and renderer surfaces. It predates the engine integration and carried no
  authority worth preserving.
- **Randomization via an engine preset.** The engine ships the dual-seed divider-folding algorithm
  this ROM uses; the port registers it with no binding boilerplate. Rejected alternative: a
  port-local wrap of the entire piece-selection routine — the selection logic around the raw random
  byte is deterministic and is better written as native C++.
- **Audio via the engine audio system.** The ROM's sound driver registers with the engine, which
  hosts it on an internal VM and owns throttling and output-rate alignment. Rejected alternative: a
  port-local CPU-plus-audio-unit integration.
- **True Game Boy timing.** The engine's Game Boy timing profile — 70,224 cycles at 4,194,304 Hz,
  i.e. 59.7275 Hz — replaces an earlier flat 60 Hz rounding.
- **Direct spdlog, no wrapper.** The engine exposes no logging surface, and a facade adds ceremony
  without value at this size.

## Implementation details

- `.gitmodules` and the `engine/` gitlink at `5e63115`; nested submodules initialized recursively.
- `CMakeLists.txt`: `add_subdirectory(engine)` behind a missing-submodule guard, ahead of the
  dependency configuration.
- `cmake/Dependencies.cmake`: the port's own SDL3 fetch removed; spdlog and GoogleTest remain.
- `src/CMakeLists.txt`: `kirpich-lib` links `retropp::engine` publicly, plus spdlog.
- `src/platform/` deleted.
- `src/main.cpp`: logs the port and engine versions, runs the first-start asset flow, then composes
  the dispatcher, the systems and the render bridge and hands the loop to `WindowedHost`.
- Verified on macOS: clean build with no port-source warnings, smoke suite green, binary runs and
  exits 0.

## Open questions

- **First-consumer surfaces.** The engine's rendering, input, and run loop are proven in
  production. Its audio and VM surfaces have engine-side tests and demos but no game consumer yet —
  this port is the first. Watch items for when that work lands: the randomization preset's seeding
  semantics against this ROM's exact initialization, driver-registration fit for the ROM's sound
  code, and whether the enhanced anti-channel-stealing mode maps onto engine instance routing.
- **CI requirements.** Recursive submodule checkout, synchronized before it is updated — a
  submodule's remote is recorded when it is first cloned and is not re-read from `.gitmodules`, so a
  workspace that predates a move keeps fetching the old one; ClangCL on Windows, since SameBoy's
  Windows shims use `#include_next`, which MSVC rejects; an offscreen video driver for headless Linux
  test steps.
- **Licensing.** Settled: the port is AGPL-3.0, matching the engine, so the distributed build is one
  combined work under that license.
