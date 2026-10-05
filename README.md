# Kirpich

<p align="center"><img src="docs/media/gameplay-pal-1.png" width="560" alt="Kirpich — a round in progress, in the hardware greyscale"></p>

A native reimplementation of **Tetris** for the Game Boy (DMG), running as ordinary desktop software on Windows, macOS, and Linux. Kirpich is built on the [Polyrhythm engine](https://github.com/RetroPlusPlus/Polyrhythm) and reproduces the original cartridge's observable behavior — the same game, given the same inputs — without emulating the hardware and without translating the assembly. The player's own cartridge supplies the graphics and the sound.

*Kirpich* (кирпич) is Russian for "brick".

## Features

- **The complete single-player game.** A-Type and B-Type, every menu and screen, pause, game over,
  the end-of-round scoreboard, and the ending performance a highest-difficulty B-Type win earns.
- **Both hidden endings.** Clearing B-Type from beneath five rows of garbage launches the Buran;
  reaching 100,000 points in A-Type launches a rocket, one of three sized by the final score.
- **A third game type.** C-Type is a marathon played over a rising floor. A count of drops sits on the
  panel: every drop takes one off it, every line cleared puts one back, and when it reaches zero the
  whole stack shifts up a row and a fresh line of garbage arrives underneath. One line per drop is
  breaking even. It has its own screen, its own starting level, and its own table of high scores, and
  it sits behind a settings switch that is off by default — a new player meets the cartridge's own two
  modes first.
- **The original audio.** The cartridge's sound driver runs unmodified on the engine's emulated
  audio unit. Music and sound effects are produced by the original code, not recreated.
- **Eighty color palettes**, selectable in-game with a live preview — the hardware greyscale,
  the original handheld's green, the eight color schemes Windows 3.1 shipped in its Control Panel,
  and forty-eight that keep a real color in their darkest shade instead of bottoming out at black,
  many of them crossing from one hue to another on the way up. The darkest shade is what the well's
  walls and every locked block are drawn in, so it is most of what you look at.
- **An optional ghost piece** showing where the falling piece will land. Disabled by default, so
  the game plays exactly as the cartridge does until the player opts in.
- **Persistent high scores.** A qualifying round is ranked, named on the original letter-wheel
  entry screen, and kept across launches — the cartridge lost its tables at power-off.
- **Statistics, if you want them.** Kirpich keeps a record of itself from the first launch: rounds
  played, time at the well, pieces dropped, score, lines and how they were cleared, each of the seven
  shapes, and the longest single round — all of it per difficulty combination and kept across
  launches. A settings switch decides whether you ever see it. Turn it on and the title screen grows
  a fourth item leading to five branches: the whole game's totals, and one for each mode with a
  picker that moves the figures under it. Because the recording never waited for the switch, turning
  it on a month from now shows you a history that was already there. Attract-mode rounds are never
  counted, and quitting in the middle of a round still records the round.
- **Achievements.** Thirty-six of them across nine themed sections — the score and line ladders, both
  hidden endings, the harder B-Type and C-Type combinations, and a few that only turn up if you play
  every mode the game has. Each is drawn from the game's own art and shaded to show whether it is
  earned; opening one gives its description, the date it was unlocked and how many hours in you were.
  A round that earns something announces it on the way out. The idea, and the sense of what is worth
  rewarding in this game, comes from [RetroAchievements](https://retroachievements.org). Nothing is
  taken from their implementation: every condition is written against the statistics this port
  already records for itself, and the names, the descriptions and the badge art are original. Kirpich is not
  affiliated with RetroAchievements and does not connect to their service.
- **Attract mode.** Left idle, the title screen plays the cartridge's two recorded demonstration
  rounds, alternating between them.
- **Preserved behavior throughout**, including the original's quirks: the reset chord
  (Start + Select + B + A) keeps the high-score tables and clears everything else, a multi-line
  clear duplicates its top row, and the unused stereo panning data remains unused. These are
  reproduced deliberately rather than corrected; see [`docs/DESIGN.md`](docs/DESIGN.md).
- **A settings screen** for fullscreen (Alt+Enter / Cmd+Enter also toggles it), window scaling, the
  palette selection, the ghost piece, the extra game types, the statistics, and a fixes screen that
  offers the cartridge's own quirks back — off by default, fidelity until you ask otherwise. A page of
  its own holds the four resets: the high scores, the statistics, the achievements, or all three, each
  asking before it acts and each leaving the other two alone. Settings persist alongside the score
  tables and apply before the window opens.

<p align="center">
  <img src="docs/media/title-with-stats/title-pal-2.png" width="270" alt="Title screen — the original handheld's green">
  <img src="docs/media/title-with-stats/title-pal-67.png" width="270" alt="Title screen — melon">
  <img src="docs/media/title-with-stats/title-pal-12.png" width="270" alt="Title screen — sunset">
  <img src="docs/media/title-with-stats/title-pal-73.png" width="270" alt="Title screen — harbor dawn">
  <img src="docs/media/title-with-stats/title-pal-24.png" width="270" alt="Title screen — aurora">
  <img src="docs/media/title-with-stats/title-pal-66.png" width="270" alt="Title screen — kingfisher">
  <img src="docs/media/title-with-stats/title-pal-71.png" width="270" alt="Title screen — poison">
  <img src="docs/media/title-with-stats/title-pal-49.png" width="270" alt="Title screen — synthwave">
  <img src="docs/media/title-with-stats/title-pal-30.png" width="270" alt="Title screen — plasma power saver">
  <img src="docs/media/title-with-stats/title-pal-7.png" width="270" alt="Title screen — sea">
  <img src="docs/media/title-with-stats/title-pal-14.png" width="270" alt="Title screen — forest">
  <img src="docs/media/title-with-stats/title-pal-61.png" width="270" alt="Title screen — lichen">
</p>
<p align="center"><em>Twelve of the eighty palettes: the original green, melon, sunset, harbor dawn, aurora, kingfisher, poison, synthwave, plasma power saver, sea, forest, and lichen.</em></p>

<p align="center">
  <img src="docs/media/gameplay-pal-2.png" width="320" alt="Gameplay, ghost piece disabled">
  <img src="docs/media/gameplay-ghost-pal-2.png" width="320" alt="Gameplay, ghost piece enabled — the landing shadow visible at the base of the well">
</p>
<p align="center"><em>The same frame with the ghost piece off and on.</em></p>

<p align="center"><img src="docs/media/stats-pal-1.png" width="320" alt="The statistics menu — all-time totals, one branch per mode, and the achievements"></p>
<p align="center"><em>The statistics menu: the whole game's totals, a branch for each mode, and the achievements.</em></p>

## Download

Binaries for all five targets are on the [latest release](https://github.com/etroimcasso/Kirpich/releases/latest):

| Platform | Artifact |
|---|---|
| macOS (Apple Silicon) | `Kirpich-macOS-arm64.dmg` — signed and notarized |
| Windows x64 / ARM64 | `Kirpich-windows-{x64,arm64}.zip` |
| Linux x64 / ARM64 | `Kirpich-linux-{x64,arm64}.zip` |

On first launch, Kirpich asks for a Tetris ROM and extracts the graphics and sound from it. That is
the entire setup; subsequent launches start directly.

### Where your files live

Everything Kirpich keeps for you — high scores, settings, the extracted assets, and the log — lives
in one per-user directory, independent of where the application itself sits:

| Platform | Location |
|---|---|
| macOS | `~/Library/Application Support/Kirpich/Kirpich/` |
| Windows | `%APPDATA%\Kirpich\Kirpich\` |
| Linux | `$XDG_DATA_HOME/Kirpich/Kirpich/` (typically `~/.local/share/Kirpich/Kirpich/`) |

Moving or updating the application leaves your scores and settings in place. Deleting this directory
returns Kirpich to a first launch: the ROM prompt reappears and the high-score tables start empty.

## Content and licensing posture

Kirpich distributes no playable copyrighted content: no ROM data, no extracted assets, nothing in
any build artifact. The graphics and sound-driver bytes are extracted locally from a Tetris
(World, Rev 1) ROM the player legitimately owns, into the player's own user directory, where they
remain. `.gitignore` bans ROM extensions and extracted content tree-wide, and the packaging step
verifies the shipped artifact carries neither. The screenshots above depict the game running
against the author's own cartridge and are included for identification. See
[`docs/features/asset-acquisition.md`](docs/features/asset-acquisition.md).

## Roadmap

- **Two-player** — the link-cable protocol and its screens, pending the engine's network substrate.
- **DMG display shader** — the original LCD's optical character: greenish tint, ghosting, dot grid.

---

## Architecture

The game logic is ordinary C++: the cartridge's data tables are `constexpr` arrays verified against
the ROM, its RAM layouts are structs, its code paths are functions. Nothing simulates the Game Boy's
PPU, memory mapper, or interrupt hardware.

Two subsystems are exceptions, and only these two run original machine code on an emulated CPU
inside the engine:

- **Piece randomization.** The original routine folds the DMG's divider register, which ticks
  independently of the program counter; the piece sequence depends on cycle-exact timing and cannot
  be reproduced by re-implementing the arithmetic.
- **Audio.** The ROM's sound driver programs the audio hardware on a cycle-driven cadence;
  faithful chiptune output requires running that driver against an emulated audio unit.

### Built on Polyrhythm

Kirpich is a complete consumer of the [Polyrhythm engine](https://github.com/RetroPlusPlus/Polyrhythm).
These are the engine surfaces it uses:

| Engine surface | How Kirpich uses it |
|---|---|
| Run loop and timing profile — `RunLoop`, `TimingProfile::GameBoy`, separate simulation and render callbacks, interpolation between ticks | The DMG's 59.7275 Hz simulation rate, decoupled from the display's refresh rate |
| Exit handling — `exitAction` / `exitRequest` | The window's close button, the platform's quit, and the game's own quit all pass through one guard, so quitting mid-round still records the round |
| Platform and windowing — `SdlPlatform`, `WindowedHost`, `EngineConfig`, `Window` | The window's title, scale and fullscreen state, presented at the Game Boy's 160 × 144 |
| Injectable clock — `Clock`, `SteadyClock` | Play time and session time for the statistics, testable against a clock the tests control |
| Declarative renderer — `FrameDrawState`, tile and sprite `DrawLayer`s, `ObjectKey` | Every screen, submitted whole each frame; the cartridge's screens through the two background maps and the object layer, the port's own screens as layers built from components |
| Sprite transforms — flips, quarter-turn `Rotation`, `Transform` | The game's own selector arrow, flipped for a left arrow and stood on end for a page arrow; the ghost piece moved down to where the piece would land |
| Sprite geometry queries — `Sprite::maskShape` | The ghost piece's silhouette, taken from the falling piece's own placed sprites |
| Regions and screen-space effects — `Region`, `ShapePoints`, `ColorFill` | The ghost piece, the palette swatch, and the achievements screen's cursor |
| Atlases and palettes — `loadAtlas`, `uploadPalette`, indexed tilesets, object palettes with a see-through entry | The cartridge's art as decoded from the player's ROM, with all eighty color ramps resident at once so switching palettes selects between uploaded handles |
| Asset registry and policies — `assetRoot`, `AssetPolicy::Embed` / `LoadFromPath`, build-time embedding | Kirpich's own routines baked into the binary; the player's extracted assets read from disk |
| Application identity and per-user files — `AppIdentity`, `userDataDir`, `UserFiles` | Extracted assets, saves and the log, in the player's own data directory wherever the binary sits |
| Versioned save store — `SaveStore` documents with schema migration | Settings, controls, top scores, statistics and achievements, each its own document at its own version, migrated forward across releases |
| Conductor, the SM83 virtual machine — `Vm`, routine registration and bindings, `advanceClock` | The piece randomizer and the procedural garbage fill — B-Type's starting rows and C-Type's rising floor — sharing one machine, so the round init's piece draws advance the divider its garbage fill then reads |
| Hosted audio drivers — `AudioLibrary::registerDriver`, `HostedDriverBinding`, `AudioSystem::GB`, `HostedDriver` | The cartridge's own sound driver, running at its original addresses on an emulated audio unit, started by a small routine of Kirpich's own; music and effects are cued through its shared bytes |
| Action-mapped input — `ActionMap`, `ActionSet`, `PadButton`, `ControllerType` | The player's keyboard and controller bindings for the Game Boy's eight buttons, with controller buttons bound by their position on the pad, under the game's own press-edge and auto-repeat logic |
| Build integration — the engine's CMake target, with SDL3 and SameBoy | One codebase shipping on five platform targets |

### Repository layout

| Path | What it is |
|---|---|
| `src/`, `include/kirpich/` | Port source and public headers |
| `tests/` | GoogleTest suite |
| `engine/` | [Polyrhythm](https://github.com/RetroPlusPlus/Polyrhythm) engine submodule — brings SDL3 and SameBoy with it |
| `assets/gfx/default/`, `assets/audio/default/` | Where a development build reads its extracted assets; contents are generated locally and never committed. A player's extraction goes to the per-user data directory beside their save, not here |
| `docs/` | Design context and feature documentation |
| `tools/` | Development tooling |

The [kaspermeerts/tetris](https://github.com/kaspermeerts/tetris) disassembly is the derivation
reference. It is read during development as a sibling checkout outside this repository — it is not a
submodule, and the build never depends on it.

### Building

Requires CMake 3.28+, a C++20 compiler (GCC 13+ / Clang 16+ / AppleClang 15+ / MSVC 19.38+), and
recursive submodules — `engine/` brings SDL3 and SameBoy with it, and a plain clone without
`--recursive` leaves it empty (the build fails at configure time saying so):

```sh
git clone --recursive https://github.com/etroimcasso/Kirpich.git
cd Kirpich
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build
```

The build defaults to a lean Release configuration.

## License

Kirpich is licensed under the [GNU Affero General Public License v3.0](LICENSE) and uses the
[Polyrhythm engine](https://github.com/RetroPlusPlus/Polyrhythm), which is licensed under PolyForm
Noncommercial, with commercial licensing available.

Tetris is a trademark of Tetris Holding, LLC. This project is unaffiliated with and unendorsed by
the trademark holder, distributes no copyrighted content, and requires the user's own ROM.
