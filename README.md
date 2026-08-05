# Kirpich

A native C++ reimplementation of **Tetris** for the Game Boy (DMG), built on the
[Retro++ engine](https://github.com/etroimcasso/GBCPP-Engine). The goal is behavioral
fidelity — the same observable behavior as the original cartridge given the same inputs and
RNG state — running as ordinary native code on Windows, macOS, and Linux. Not an emulator,
and not a mechanical assembly translation: idiomatic modern C++ against a portable engine
layer.

*Kirpich* (кирпич) is Russian for "brick".

**Status: early.** The build system, test harness, and engine integration are in place, and the
data layer has begun — the core value types are ported. Game systems are not yet written, and the
current binary prints the engine version and exits.

## How it works

The game logic is written as ordinary C++ — data tables become `constexpr` arrays, RAM layouts
become structs, code paths become functions. Nothing simulates the Game Boy's PPU, memory
mapper, or interrupt hardware.

Two things resist that treatment, and only those two run the original machine code on an
emulated CPU inside the engine:

- **Piece randomization.** The original routine folds the DMG's divider register, which ticks
  independently of the program counter. The piece sequence therefore depends on cycle-exact
  timing and cannot be reproduced by re-implementing the arithmetic.
- **Music and sound effects.** The ROM's sound driver writes to the audio hardware on a
  cycle-driven cadence. Reproducing the chiptune output faithfully requires running that driver
  against an emulated audio unit.

Everything else — the run loop, rendering, input, scoring, line clears, menus, multiplayer —
is native code.

## What ships and what doesn't

- **You supply your own ROM.** The graphics and the sound-driver bytes are extracted locally
  from a Tetris ROM you legitimately own; nothing derived from the cartridge is committed here
  or distributed. See [`docs/features/asset-acquisition.md`](docs/features/asset-acquisition.md).
- **No copyrighted content in the repository or in any build artifact.** `.gitignore` bans ROM
  file extensions and extracted asset content tree-wide, and the distributable build empties the
  asset directories before packaging.

## Preserved behavior

The original has a handful of observable quirks — the OAM copy routine transfers two bytes more
than it needs to, the top two playfield rows are never cleared, a multi-line clear duplicates the
top row, the music's stereo panning data is present but non-functional. These are reproduced, not
fixed. Details in [`docs/DESIGN.md`](docs/DESIGN.md).

## Planned options

All off by default, all composable: integer scaling, free-aspect output, pixel-art upscaling
shaders, a DMG-style display shader, and an anti-channel-stealing audio mode that lets music keep
playing underneath sound effects instead of losing a channel to them.

## Repository layout

| Path | What it is |
|---|---|
| `src/`, `include/kirpich/` | Port source and public headers |
| `tests/` | GoogleTest suite |
| `engine/` | [Retro++](https://github.com/etroimcasso/GBCPP-Engine) engine submodule (currently a private repository — see note below) |
| `assets/gfx/default/` | Canonical asset load target; contents are generated locally and never committed |
| `docs/` | Design context and feature documentation |
| `tools/` | Development tooling |

The [kaspermeerts/tetris](https://github.com/kaspermeerts/tetris) disassembly is the derivation
reference. It is read during development as a sibling checkout outside this repository — it is
not a submodule, and the build never depends on it.

> **Note on submodules:** the Retro++ engine repository is private while it stabilizes, so
> third-party clones cannot initialize `engine/` yet and the project will not build externally
> until the engine is published.

## Building

Requires CMake 3.28+, a C++20 compiler (GCC 13+ / Clang 16+ / AppleClang 15+ / MSVC 19.38+),
and recursive submodules:

```sh
git clone --recursive git@github.com:etroimcasso/Kirpich.git
cd Kirpich
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build
```

The build defaults to a lean Release configuration.

## License

Licensing is being finalized; until a license is chosen, all rights are reserved. The Retro++
engine carries its own license, and the upstream disassembly is published without one.

Tetris is a trademark of Tetris Holding, LLC. This project is unaffiliated with and unendorsed by
the trademark holder, ships no copyrighted content, and requires the user's own ROM.
