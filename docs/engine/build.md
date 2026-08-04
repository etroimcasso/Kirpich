# Build & consume

How Kirpich is put together, what the targets are, and how to build, run, and test it.

## Requirements

- **CMake 3.28+** and a generator; Ninja is what the project is developed against.
- **A C++20 compiler.** The build enforces floors and fails at configure time below them:
  GCC 13, Clang 16, AppleClang 15, MSVC 19.38 (Visual Studio 2022 17.8).
- **Git**, with submodules — the engine is one, and it has its own nested.

`ccache` is used automatically if it is on `PATH` (not on MSVC, which it does not integrate
with).

## First build

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build
```

The submodule step is not optional: configure fails with an explicit message if
`engine/CMakeLists.txt` is absent, because a missing engine otherwise surfaces as a wall of
unrelated errors.

To run the game you also need its graphics, which do not come with the source — see
[assets.md](assets.md). With the disassembly checked out beside this repository:

```sh
scripts/setup-dev-assets.sh
./build/kirpich
```

## Targets

| Target | What it is |
|---|---|
| `kirpich-lib` | Static library holding everything except `main()`. Links `retropp::engine` and `spdlog::spdlog_header_only` publicly, so anything linking it sees both surfaces. |
| `kirpich` | The executable. `main.cpp` plus `kirpich-lib`. |
| `kirpich-tests` | GoogleTest runner, registered with CTest. |

`main()` is deliberately kept out of the library so the tests can link the whole port
without pulling in a second entry point.

### `kirpich::Engine`

The port's top-level object, in `src/engine.{h,cpp}`. One instance owns the game's runtime
state and drives it through the engine's run loop. It is non-copyable and non-movable — a
unique runtime owner rather than a value, so an accidental copy is a compile error.

It is currently a skeleton: default-constructed, holding nothing and doing nothing.
`main()` constructs one to prove the wiring. State, systems, and rendering arrive here as
those layers are written, and this section grows with them.

Test sources are globbed with `CONFIGURE_DEPENDS`, so **adding a `.cpp` under `tests/`
requires no CMake edit** — it is picked up on the next build. Cases register with CTest
individually via `gtest_discover_tests`, which is why `ctest` lists them by name rather than
as one opaque binary.

## Build options

| Option | Default | Effect |
|---|---|---|
| `KIRPICH_DEV_ASSET_ROOT` | `ON` | Defines `KIRPICH_PROJECT_ROOT` to the source directory, which `main()` uses to resolve the asset root to the project tree. Turn it **off for a distributable**: the definition disappears and the engine's default applies — the executable's own directory. |
| `BUILD_TESTING` | `ON` | Standard CTest option. Off skips the `tests/` subdirectory entirely. |

`CMAKE_BUILD_TYPE` defaults to `Release` when neither it nor a multi-config generator sets
one, so an unconfigured build is optimized rather than an unoptimized debug build.

`compile_commands.json` is always exported, for clangd and IDE include resolution.

## Dependencies

| Dependency | How it arrives |
|---|---|
| Retro++ engine | `engine/` git submodule, `add_subdirectory` |
| SDL3 | Transitively, from the engine (`engine/third_party/sdl`) |
| SameBoy | Transitively, from the engine (`engine/third_party/sameboy`) |
| spdlog | `FetchContent`, v1.15.3, header-only |
| GoogleTest | `FetchContent`, v1.15.2, gmock off |

**Kirpich must never declare SDL3 itself.** It arrives through the engine, and a second
provider of the `SDL3::SDL3` target is a configure-time error. The port may *call* SDL
directly where the engine has no opinion — the file dialog in `src/assets/first_start.cpp`
is the one place that does today — but declaring it is a different thing and stays banned.

On Windows, build with **ClangCL**: SameBoy's Windows shims use `#include_next`, which MSVC
rejects.

## Logging

spdlog, used directly — there is no port-side logging wrapper, and the engine provides no
logging surface. Call `spdlog::info` / `warn` / `error` where you need them.

## Layout

```
src/
  main.cpp          entry point: asset root, first-start flow, then the engine
  engine.{h,cpp}    the port's top-level object
  assets/           required-asset manifest, presence check, first-start flow
tests/              GoogleTest cases; fixtures/ holds test assets
tools/              port-time tooling (parsers, the ROM extractor)
scripts/            developer and packaging scripts
assets/gfx/default/ canonical graphics location; contents gitignored
engine/             Retro++ submodule
docs/               these pages, plus features/ and contracts/
```

## Warnings

`-Wall -Wextra -Wpedantic` on GCC and Clang; `/W4 /permissive-` plus `/Zc:__cplusplus` on
MSVC, which otherwise reports `__cplusplus` as `199711L` regardless of the standard in
force.

Port sources build warning-free, and that is the standard to hold. The engine and its
vendored third-party trees produce their own warnings; those are not yours to fix from here.
