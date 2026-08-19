# Kirpich

A native C++ reimplementation of **Tetris** for the Game Boy (DMG), built on the
[Retro++ engine](https://github.com/etroimcasso/GBCPP-Engine). The goal is behavioral
fidelity — the same observable behavior as the original cartridge given the same inputs and
RNG state — running as ordinary native code on Windows, macOS, and Linux. Not an emulator,
and not a mechanical assembly translation: idiomatic modern C++ against a portable engine
layer.

*Kirpich* (кирпич) is Russian for "brick".

## Status

**Kirpich is playable.** A solo round runs from the title screen through the menus into play and out
the other side — you steer the piece, see the next one waiting, clear lines, keep score, and reach the
game-over screen or, on a Type B win, the scoreboard and the dance the hardest level earns. The game's
own sound driver runs on the engine's emulated audio unit, so the music and effects are the
cartridge's.

Both halves of the picture draw: the backgrounds — screens, menus, the playing field with its walls
and panel, the blocks as they stack — and the objects, which are the piece you are steering, the
preview, every menu cursor, and the ending's performers. The port keeps the two screen buffers the
hardware keeps, so the effects that live between them work: the field wipe sweeps a row at a time, the
line-clear flash flashes, and a Type B round starts under garbage you can see.

What remains is two-player play, the attract-mode demo, the rocket and Buran launch scenes, top-score
entry, the paused screen, the score and level readouts, the palette effects, and the display filters.

**The data.** Every table the cartridge reads is ported to typed, `constexpr` C++ and checked against the
ROM: the character map and the 22 static background screens, the composed sprites and their on-screen
object lists, the gravity and scoring tables, the piece list, the recorded demo inputs, the music
sequences and sound-effect banks, and the tile graphics — together with the first-run tool that extracts
a player's own cartridge into the images the engine loads.

**The state.** Every structure the running game keeps in memory is a plain C++ type: the work-RAM
globals, the main-loop bookkeeping, the 32×32 playing field, the sprite-render slots, the link-cable and
attract-demo blocks, and the high-score tables. A persistence layer carries high scores across launches —
a capability the original lacks, where the tables survive only until the console is switched off.

**The game logic.** Built on those: the piece randomizer (run on the engine's emulated CPU, since the
sequence depends on cycle-exact timing), the input layer with its press-edge detection and auto-repeat,
the per-frame state dispatcher every state runs under, the piece mechanics (spawn, gravity, rotation,
wall shift, collision, and locking), the line-clear pipeline (detection, flash, compaction, and the
row-by-row field wipe), scoring (the live award, the end-of-round count-up, and level-up), the full
pre-game flow — the copyright and title screens and the game-type, music, and difficulty menus — and the
round itself: the shared init both game types and the attract demo enter through, the per-frame gameplay
loop that composes the piece, line-clear, and scoring systems, the pause, and the game-over chain. A
Type B round adds the garbage it starts buried under and the win chain it ends on — the scoreboard, and
the dance the hardest level earns first.

**The sound.** The game's original sound driver runs as a resident machine on the engine's audio unit —
the cartridge's own code, at the cartridge's own addresses — and gameplay asks it for music and effects
the way the original did, by leaving a sound's number in a memory mailbox it reads once a frame. Nothing
about how a song sounds is reimplemented.

The table below is kept honest as components land.

| Component | Status |
|---|---|
| Data tables | **complete** — every graphics, tilemap, sprite, timing, scoring, audio, and demo table, ROM-verified in full |
| ROM asset extractor | **complete** — first-run extraction of a player's own cartridge into the engine's load format |
| Game state | **complete** — every work-RAM and high-RAM structure the running game keeps |
| High-score persistence | **complete** — durable top-score tables across launches |
| Piece randomizer | **complete** — the divider-fold RNG, run on the engine's emulated CPU for cycle-exact fidelity |
| Input | **complete** — per-frame snapshot, press-edge detection, and auto-repeat |
| State dispatcher | **complete** — the per-frame jump table and soft-reset chord every game state runs under |
| Piece system | **complete** — spawn, gravity, rotation, wall shift, collision, and locking |
| Line clears | **complete** — detection, flash, compaction, and the field wipe |
| Scoring | **complete** — the live line-clear award, the end-of-round count-up, and level-up |
| Pre-game screens | **complete** — the copyright and title screens and the game-type, music, and difficulty menus |
| In-round gameplay states | **complete** — the shared round init, the per-frame gameplay loop, the pause, and the game-over chain |
| Type B round | **complete** — the starting garbage, and the win chain: the scoreboard and the dance the hardest level earns |
| Audio | **complete** — the game's own sound driver hosted on the engine's emulated audio unit, driven by a per-frame cue mailbox |
| Attract-mode demo | not started — the recorded inputs and piece list are ported |
| Background rendering | **complete** — every screen's backdrop, the two screen buffers the hardware keeps, and the wipe and flash that live between them |
| Sprite rendering | **complete** — the falling piece, the preview, the menu cursors, and the ending's performers |
| Frame loop | **complete** — the entry point is a host: it ticks the game and submits a frame at the original's rate |
| Display effects | not started — the original's palette fades, the paused screen, the score and level readouts, and the scaling and filter options |

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

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE), matching
the Retro++ engine's open-source license (the engine itself is dual-licensed AGPL-3.0 /
commercial). The upstream disassembly is published without a license.

Tetris is a trademark of Tetris Holding, LLC. This project is unaffiliated with and unendorsed by
the trademark holder, ships no copyrighted content, and requires the user's own ROM.
