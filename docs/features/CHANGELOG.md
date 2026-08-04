# Features Changelog

Chronological log of feature status transitions, newest first. Entries are not edited after they
are written. `../FEATURES.md` holds current status; this file holds history.

**Format:** `<feature> <old status> → <new status>` with a one-line reason. Glyphs match
`../FEATURES.md`: ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected.

---

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
