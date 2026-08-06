# Engine documentation

Guide for working with Kirpich's C++ surfaces — building it, running it, changing how it
behaves, and building on top of what is already there. Each area has its own page; the
index is below.

These pages describe the surface as it exists: what a type holds, what a function returns,
what it throws, where the backing data lives, and what to edit to change behavior. They are
written for someone modifying or extending Kirpich, not for someone checking it against the
original game — behavioral specifications reverse-derived from the Game Boy version live
separately in [`../contracts/`](../contracts/), and the design rationale behind a given
feature lives in [`../features/`](../features/).

Kirpich is a native reimplementation, not an emulator. It runs as ordinary C++ on top of
the [Retro++](https://github.com/etroimcasso/GBCPP-Engine) engine, which supplies the
platform layer, run loop, renderer, audio, and the virtual machine that hosts the handful
of routines that need one. Where a page says "the engine", it means Retro++; where it says
"the port" or "Kirpich", it means the code in this repository.

## Pages

| Page | Covers |
|---|---|
| [assets.md](assets.md) | How the game gets its graphics: the required-asset manifest, the presence check, the first-start ROM selection flow, the asset root, and the packaging gate that keeps ROM-derived bytes out of a distributable. |
| [build.md](build.md) | Targets and how they fit together, the engine submodule, build options, and how to build, run, and test. |
| [core-enums.md](core-enums.md) | The fundamental value types — game state, game type, music type, the serial types, and the piece byte — where they live, which are generated from the disassembly, and how to regenerate and change them. |
| [charmap.md](charmap.md) | The character map — how text becomes tile indices: the entry type, the exact-sequence lookup and greedy-longest-match encoder, where the table lives, and how to regenerate it. |
| [sprite-grids.md](sprite-grids.md) | The sprite layout grids — five shared (y, x) offset frames the renderer walks to place a composite sprite's tiles, the `PieceKind` enum, where they live, and how to regenerate them. |

Pages group into subdirectories once there are enough of them to warrant it — for now the
surface is small enough that a flat list is easier to scan.

## Status

Kirpich is early. What exists today is the build, the engine wiring, the asset pipeline,
and the first of the data-layer types; the state, systems, and rendering layers are not
written yet. Pages appear as their surfaces do, so an area missing from the index above is
an area that does not exist yet rather than one that is undocumented.

One consequence worth stating plainly: the extractor that reads graphics out of a ROM is
designed but not implemented, so a fresh clone cannot yet produce a running game from a ROM
alone. [assets.md](assets.md) covers what to do in the meantime.
