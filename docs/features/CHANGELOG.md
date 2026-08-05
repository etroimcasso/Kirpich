# Features Changelog

Chronological log of feature status transitions, newest first. Entries are not edited after they
are written. `../FEATURES.md` holds current status; this file holds history.

**Format:** `<feature> <old status> → <new status>` with a one-line reason. Glyphs match
`../FEATURES.md`: ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected.

---

## 2026-08-05

- **Character map** ⬜ → ✅. The 47-entry `charmap.asm` sequence→tile table ported as a
  `CharmapEntry` table with exact-sequence lookup (`getCharmapTile`) and an RGBDS greedy-longest-match
  text encoder (`encodeCharmapText`) — the `.”` ligature encodes to a single tile, and the digits map
  to their own tile indices as a guarantee the score renderer can rely on. The table and its test
  fixture are generated from the disassembly by `tools/asm_parser/parse_charmap.py` (non-ASCII bytes
  emitted as `\xHH` escapes so it compiles identically everywhere), and the shared parser helpers
  moved to `tools/asm_parser/common.py` with the core-enums parser's output byte-for-byte unchanged.
  Test baseline 17 → 24. See [`charmap.md`](charmap.md), [`../contracts/charmap.md`](../contracts/charmap.md),
  [`../engine/charmap.md`](../engine/charmap.md).

## 2026-08-04

- **Core enums** ⬜ → ✅. The seven core type surfaces (`GameState`, `GameType`, `MusicType`,
  `SerialRole`, `SerialClockMode`, `SerialState`, `Piece`) ported as header-only types in
  `include/kirpich/`; the serial constants and the value fixture are generated from the disassembly
  by `tools/asm_parser/parse_core_enums.py`, the rest hand-written and drift-checked against it.
  Test baseline 9 → 17. See [`core-enums.md`](core-enums.md), [`../contracts/core-enums.md`](../contracts/core-enums.md),
  [`../engine/core-enums.md`](../engine/core-enums.md).
- **Build system** ⬜ → ✅. Feature document written; the CMake project, dependency configuration,
  and target graph build clean and pass the test suite on all five targets. See
  [`build-system.md`](build-system.md).
- **Distributable build** documented; stays ⬜. The shipping gate (the empty-asset clean check) and
  the development/ship asset-root switch are in place; the packaging target and the lean link
  configuration are designed and not yet built. See [`distributable-build.md`](distributable-build.md).

## 2026-08-03

- **Retro++ engine adoption** (new) ⬜ → ✅. Engine consumed as a submodule at `d4a6091` via
  `add_subdirectory(engine)` + `retropp::engine`; build and smoke suite green against it. See
  [`engine-adoption.md`](engine-adoption.md) and `../DESIGN.md` §12.
- **Platform abstraction facade** ⬜ → ❌. Superseded by engine adoption; the port-local SDL3
  facade was deleted, since the engine provides platform, windowing, and rendering and the port
  may not declare its own SDL3.
- **Logging** ⬜ → ✅. Resolved as direct spdlog use — the engine exposes no logging surface and a
  wrapper adds nothing at this size. No separate deliverable.
- **CPU virtualization** ⬜ → 🟫. Dropped as port-side work: the engine owns the VM host and its
  preset routines. See [`cpu-fidelity.md`](cpu-fidelity.md).
- **Tick scheduling** ⬜ → 🟫. Dropped as a separate feature; the engine run loop is the scheduler.

## 2026-06-10

- **Test harness** ⬜ → ✅. GoogleTest wired into CTest via `gtest_discover_tests()`;
  `tests/smoke_test.cpp` adds three cases (arithmetic, string comparison, C++20 designated
  initializers). First non-zero test baseline: 3 passing.

## 2026-05-15

- **Whole-project feature registry seeded.** Data, state, systems, rendering, and integration
  features registered as pending in `../FEATURES.md`.
- **Continuous integration scope expanded** ⬜ → ⬜. Was three targets (macOS, Linux, Windows); now
  five (adds Linux ARM64 and Windows ARM64). See [`ci.md`](ci.md).
- **ROM extraction tool scope reduced** ⬜ → ⬜. Folded into
  [`asset-acquisition.md`](asset-acquisition.md) rather than carrying its own document.
- **CPU virtualization** registered as pending. Prerequisite for randomization and the chiptune
  audio backend.
- **Chiptune audio backend** registered as pending. Replaces the originally-planned native audio
  driver and music engine. Chiptune only — no audio-file replacement backend.
- **Anti-channel-stealing** registered as pending. Ships as a user-toggleable option, off by
  default.
- **Asset acquisition scope reduced** ⬜ → ⬜. Single canonical asset path; no swappable packs, no
  manifest, no fallback chain.
- **Display options scope amended.** All four now explicitly require the SDL_GPU backend; the
  simpler renderer path is insufficient. No status change.
- **Build system** implementation delivered — CMake project and dependency configuration land; the
  binary builds clean on macOS and exits 0. Status stays pending until its feature document is
  written.

## 2026-05-14

- Initial registry seeded — infrastructure features registered as pending.
- All four display options registered as pending; design locked in `../DESIGN.md` §7.
