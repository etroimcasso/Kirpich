# Build System

**Date:** 2026-05-15; documented 2026-08-04
**Status:** Delivered — extended as new source lands

The CMake project that compiles the port, its dependencies, and its tests across macOS, Linux,
and Windows. This document records the design decisions behind the build; the how-to-build and
how-to-modify guide lives in [`../engine/build.md`](../engine/build.md).

## Concept

One CMake project (`kirpich`, C++20) produces three targets — a static library holding all port
code except `main()`, the game executable, and a GoogleTest runner — and pulls its dependencies in
two ways: the Retro++ engine as a git submodule, and spdlog and GoogleTest via `FetchContent`.
Ninja is the generator the project is developed against; CMake 3.28 is the floor.

The build is deliberately plain. It leans on CMake and the engine's own build for everything they
already do well, and adds only the few decisions below where a default would have been wrong.

## Design decisions

### The library / executable split

Port code lives in a static library, `kirpich-lib`, with `main.cpp` deliberately excluded. The
executable `kirpich` is `main.cpp` linked against that library, and the test runner links the same
library. Keeping the entry point out of the library is what lets the tests exercise the whole port
without a second `main()` colliding at link time.

`kirpich-lib` links `retropp::engine` **publicly**, so anything that links the library sees the
engine surface transitively. That is the intended boundary: the port is a consumer of the engine,
and the engine is the platform layer for every target that builds on the port.

### Release is the default build type, never an empty one

An unconfigured CMake build leaves `CMAKE_BUILD_TYPE` empty, which produces an unoptimized binary
with no optimization flags at all. The project forces `Release` when neither a build type nor a
multi-config generator has set one, so the default build a contributor gets is optimized rather
than an accidental debug build. A debug build is an explicit, temporary opt-in
(`-DCMAKE_BUILD_TYPE=Debug`), never the default. The extra steps a *shipped* artifact needs on top
of `Release` are a separate concern — see [`distributable-build.md`](distributable-build.md).

### Executables land at the build root

Targets are defined in `src/`, and left alone CMake mirrors that subdirectory into the build tree,
so the game would build to `build/src/kirpich` — a surprising place to look for it. Setting
`CMAKE_RUNTIME_OUTPUT_DIRECTORY` to the build root puts the executable where anyone running a
normal build expects it: `build/kirpich`.

### Compiler floors fail at configure time, with a reason

The port uses C++20 features that older toolchains implement incompletely, so the build checks the
compiler and version at configure time and stops with a named floor rather than failing deep in a
compile with a confusing error: GCC 13, Clang 16, AppleClang 15, MSVC 19.38 (Visual Studio 2022
17.8). A contributor on an old compiler learns why immediately.

### Warning-free port sources are the standard

`-Wall -Wextra -Wpedantic` on GCC and Clang; `/W4 /permissive-` on MSVC, plus `/Zc:__cplusplus`
because MSVC otherwise reports `__cplusplus` as `199711L` regardless of the standard in force. Port
sources build clean under these, and that is the bar held going forward. The engine and its
vendored third-party trees emit their own warnings; those are not the port's to fix.

### The engine is a guarded submodule, and the port never declares SDL3

The engine is brought in with `add_subdirectory(engine)` behind a guard that checks for
`engine/CMakeLists.txt` and stops with an explicit "run `git submodule update --init --recursive`"
message when it is missing — otherwise a fresh clone without submodules fails as a wall of
unrelated errors. SDL3 and SameBoy arrive as the engine's own nested submodules; the port declares
neither. A second provider of the `SDL3::SDL3` target is a configure-time error, which enforces the
consumer boundary structurally rather than by convention.

The one place the port asks the engine's build for something is the SDL dialog subsystem: the
engine leaves it off by default, and the first-start ROM picker needs it, so the port sets
`SDL_DIALOG ON` **before** `add_subdirectory(engine)` so the engine's non-forcing default does not
win. Rationale for the picker itself is in [`asset-acquisition.md`](asset-acquisition.md).

### spdlog and GoogleTest via FetchContent; SDL3 conspicuously not

`cmake/Dependencies.cmake` fetches spdlog (header-only) and GoogleTest (gmock off, shared CRT on
for Windows MSVC compatibility), both pinned to explicit tags. SDL3 is **not** declared there, with
a comment saying why: it comes transitively from the engine, and declaring it here would be the
configure-time double-provider error above.

### Test sources are globbed

`tests/CMakeLists.txt` globs `*.cpp` with `CONFIGURE_DEPENDS`, so adding a test file requires no
CMake edit — it is picked up on the next build. Cases register with CTest individually through
`gtest_discover_tests`, so `ctest` lists them by name rather than as one opaque binary.

### ccache and compile_commands.json, for free where they help

`ccache` is wired as the compiler launcher automatically when it is found on `PATH`, on every
toolchain except MSVC, which it does not integrate with. `compile_commands.json` is always
exported for clangd and IDE include resolution. Neither changes what is built; both make the daily
loop faster or the editor smarter at no cost.

### The development asset-root toggle

`KIRPICH_DEV_ASSET_ROOT` (on by default) defines `KIRPICH_PROJECT_ROOT` to the source directory, so
a development build resolves game assets to the project tree and exercises the shipped load path
against the files the setup script wrote — no copying anything beside the binary. A distributable
build turns it off. The toggle is a build-system control but its behavior belongs to the asset
story; see [`asset-acquisition.md`](asset-acquisition.md) and
[`distributable-build.md`](distributable-build.md).

## Implementation details

**Files:**

- `CMakeLists.txt` — project setup, C++ standard, Release default, executable output directory,
  platform detection, compiler floors, warning flags, ccache, `compile_commands.json`, the engine
  submodule guard and `SDL_DIALOG` opt-in, then `src/` and `tests/`.
- `cmake/Dependencies.cmake` — spdlog and GoogleTest via `FetchContent`; SDL3 deliberately absent.
- `src/CMakeLists.txt` — `kirpich-lib` (static, `main.cpp` excluded), the `kirpich` executable, and
  the `KIRPICH_DEV_ASSET_ROOT` option.
- `tests/CMakeLists.txt` — globbed GoogleTest runner registered with CTest.

**Targets:** `kirpich-lib` (static library), `kirpich` (executable), `kirpich-tests` (test runner).

The build compiles clean and the test suite passes on all five supported targets — Linux x64,
macOS ARM64, Linux ARM64, Windows x64, and Windows ARM64. See [`ci.md`](ci.md).

## Open questions

- **A packaging / install target.** The build produces a runnable binary in the build tree but has
  no `install` or packaging step yet, and no lean-shipping configuration on top of `Release`. That
  is the subject of [`distributable-build.md`](distributable-build.md).
- **A sanitizer configuration.** A `Debug` build with address and undefined-behavior sanitizers is
  worth adding once there is meaningful game code to exercise. Noted in [`ci.md`](ci.md) as well.
