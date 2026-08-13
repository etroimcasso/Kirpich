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
| [charmap.md](charmap.md) | The character map — how text becomes named glyphs: the `CharTile` enum, the entry type, the exact-sequence lookup and greedy-longest-match encoder, where the table lives, and how to regenerate it. |
| [sprites.md](sprites.md) | The composed sprites — every multi-tile sprite resolved into a part list, the `SpriteId` identity space, the `PieceKind` enum, where they live, and how to regenerate them. |
| [sprite-scenes.md](sprite-scenes.md) | The sprite objects each scripted scene places on screen — the victory, defeat, dance, and launch scenes, the menu markers, and the falling-piece templates, where they live, and how to regenerate them. |
| [gravity.md](gravity.md) | How fast pieces fall — the per-level drop-interval table, the lookup and its heart-mode shift, the level bounds, where the table lives, and how to regenerate it. |
| [scoring.md](scoring.md) | What points are worth — the line-clear award table and its level multiplier, the soft-drop quirk, the rocket bonus-ending tiers, the level-up rule, where the tables live, and how to regenerate them. |
| [playing-field.md](playing-field.md) | The board's fixed extent (18 × 10) and the wipe schedule that redraws it a row per frame — the geometry constants, the counter→row mapping, where they live, and how to regenerate them. |
| [tile-graphics.md](tile-graphics.md) | The graphics themselves — the extraction table naming which ROM bytes are which asset, the in-app extractor that turns a player's ROM into the PNGs the engine loads, the PNG serialization, and how to regenerate the table. |
| [tilemaps.md](tilemaps.md) | The static screens the game draws — the 22 background tilemap grids (full screens, banners, field overlays, window messages, tower columns, and the congratulations strip), how text rows decode through the character map, where they live, and how to regenerate them. |
| [garbage-init.md](garbage-init.md) | The garbage a Type B game starts under — the fixed demo garbage table and the constants the procedural fill and its start paths use, where they live, and how to regenerate them. |
| [music.md](music.md) | The music data — the `MusicId` identifiers and the addresses that locate the song/channel/section graph, the stereo table, and the note-length tables in the sound driver's ROM image, where they live, and how to regenerate them. |
| [sfx.md](sfx.md) | The sound-effect data — the three effect-ID spaces the game triggers effects by and the register images, ramps, and driver tables the effect routines read, where they live, and how to regenerate them. |
| [demo.md](demo.md) | The attract-mode demo recordings — the two input timelines of held game actions and the shared piece list both demos replay, the action vocabulary they resolve to, where they live, and how to regenerate them. |
| [misc.md](misc.md) | The loose tables and constants — the directly-drawn sprite-object tables, the menu cursor coordinate tables, the win-screen strings and the pause label, and the demo/completed-row constants, where they live, and how to regenerate them. |

Pages group into subdirectories once there are enough of them to warrant it — for now the
surface is small enough that a flat list is easier to scan.

## Status

Kirpich is early. What exists today is the build, the engine wiring, the asset pipeline —
including the extractor that produces the graphics from a player's ROM — and the first of
the data-layer types; the state, systems, and rendering layers are not written yet. Pages
appear as their surfaces do, so an area missing from the index above is an area that does
not exist yet rather than one that is undocumented.
