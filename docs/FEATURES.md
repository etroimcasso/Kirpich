# Features

Status registry — one row per feature. `features/CHANGELOG.md` records every status transition
chronologically; this file holds current state.

**Legend:** ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected

---

## Infrastructure

| Feature | Status | Doc |
|---|---|---|
| Repository scaffolding and ignore rules | ✅ | — |
| Build system | ✅ | [`features/build-system.md`](features/build-system.md) |
| Test harness | ✅ | — |
| Retro++ engine adoption | ✅ | [`features/engine-adoption.md`](features/engine-adoption.md) |
| Logging | ✅ | — (spdlog used directly; no wrapper) |
| Platform abstraction facade | ❌ | — (superseded by engine adoption 2026-08-03) |
| Asset acquisition | ✅ | [`features/asset-acquisition.md`](features/asset-acquisition.md) — graphics and the sound driver image |
| ROM extraction tool | ✅ | [`features/tile-graphics.md`](features/tile-graphics.md) — graphics; audio byte spans ride the audio backend |
| Continuous integration | ⬜ | [`features/ci.md`](features/ci.md) |
| Distributable build | ⬜ | [`features/distributable-build.md`](features/distributable-build.md) |

## Data

Constant tables and graphics data derived from the ROM.

| Feature | Status | Doc |
|---|---|---|
| Core enums (piece, game type, music type, game state, serial) | ✅ | [`features/core-enums.md`](features/core-enums.md) |
| Character map | ✅ | [`features/charmap.md`](features/charmap.md) |
| Sprite layout grids (+ PieceKind) | ✅ | [`features/sprite-grids.md`](features/sprite-grids.md) — grid surface folded into the composed sprites; PieceKind moved there |
| Gravity / frames-per-drop table | ✅ | [`features/gravity.md`](features/gravity.md) |
| Scoring tables | ✅ | [`features/scoring.md`](features/scoring.md) |
| Playing-field wipe patterns | ✅ | [`features/playing-field.md`](features/playing-field.md) |
| Tile graphics | ✅ | [`features/tile-graphics.md`](features/tile-graphics.md) |
| Tilemaps | ✅ | [`features/tilemaps.md`](features/tilemaps.md) |
| Sprite rotation tables | ✅ | [`features/sprite-oam-rotations.md`](features/sprite-oam-rotations.md) |
| Sprite scene lists | ✅ | [`features/sprite-scenes.md`](features/sprite-scenes.md) |
| Garbage fill | ✅ | [`features/garbage-init.md`](features/garbage-init.md) — the demo table and the fill's constants, plus the fill itself: the per-cell pick on the VM, the one-gap-per-row rule, the row extents, and the seam the round init calls |
| Music data | ✅ | [`features/music-data.md`](features/music-data.md) |
| Sound-effect data | ✅ | [`features/sfx-data.md`](features/sfx-data.md) |
| Demo data | ✅ | [`features/demo-data.md`](features/demo-data.md) |
| Miscellaneous data | ✅ | [`features/misc-data.md`](features/misc-data.md) |

## State

Mutable game state, mirroring the original's RAM layout as ordinary C++ structs.

| Feature | Status | Doc |
|---|---|---|
| Global game state | ✅ | [`features/engine-state.md`](features/engine-state.md) |
| Playing-field shadow state | ✅ | [`features/playing-field-state.md`](features/playing-field-state.md) |
| Game-state-machine state | ✅ | [`features/game-state-machine-state.md`](features/game-state-machine-state.md) |
| Audio state | ✅ | [`features/audio-state.md`](features/audio-state.md) |
| Sprite renderer state | ✅ | [`features/sprite-renderer-state.md`](features/sprite-renderer-state.md) |
| Serial / multiplayer state | ✅ | [`features/serial-multiplayer-state.md`](features/serial-multiplayer-state.md) |
| Demo state | ✅ | [`features/demo-state.md`](features/demo-state.md) |
| High-score state | ✅ | [`features/high-score-state.md`](features/high-score-state.md) |

## Systems

Game logic.

| Feature | Status | Doc |
|---|---|---|
| Randomization | ✅ | [`features/piece-random.md`](features/piece-random.md) — draw core + `pickRandomPiece`; the solo per-piece draw reuses this core when the piece system lands |
| Input | ✅ | [`features/input-layer.md`](features/input-layer.md) — per-frame joypad snapshot + held/pressed edge, shared key-repeat core, default bindings; the consumer sites read it as they land |
| Game-state dispatcher | ✅ | [`features/dispatcher.md`](features/dispatcher.md) — the dispatch table + frame beats + soft-reset chord + the `GameContext` aggregate; ships stub handlers, the real handlers land with their systems |
| Piece system | ✅ | [`features/piece-system.md`](features/piece-system.md) — spawn / drop / rotate-shift / collide / lock as free functions, plus the `AudioCues` cue mailbox; the gameplay and line-clear handlers compose these when they land |
| Line-clear logic | ✅ | [`features/line-clear.md`](features/line-clear.md) — scan / tally, flash cadence, stack compaction, and the row-by-row field wipe as free functions; the gameplay handlers and the frame's vertical-blank tick compose these when they land |
| Scoring | ✅ | [`features/scoring-system.md`](features/scoring-system.md) — the Type A live line-clear award, the Type B results count-up, the Type A level-up (wired into the field wipe), the scoreboard row printer, and the scoring reset as free functions; the gameplay and results handlers compose these when they land |
| Number readouts | ✅ | [`features/readouts.md`](features/readouts.md) — the score, the level, the line count and the Type B start height drawn into the stats panel, and the second background map the paused screen displays |
| Chiptune audio backend | ✅ | [`features/audio-engine.md`](features/audio-engine.md) — the game's original sound driver hosted as a resident machine, and the per-frame tick that drains the `AudioCues` mailbox into it; the port makes sound |
| Anti-channel-stealing option | ⬜ | [`features/anti-channel-stealing.md`](features/anti-channel-stealing.md) |
| Title / config / menu screens | ✅ | [`features/menu-screens.md`](features/menu-screens.md), [`features/title-screens.md`](features/title-screens.md) — the whole pre-game flow (copyright, title, config, and the game-type / music-type / difficulty selectors) is delivered as per-state handlers. The demo launch and the two-player serial paths are seams later units fill |
| Gameplay session | ✅ | [`features/gameplay.md`](features/gameplay.md) — the shared round init (both game types and the attract demo), the twelve-step gameplay frame, the pause (shared with two-player), the game-over chain and its rocket endings, and the Type B results re-arm, as per-state handlers. The garbage fill, the demo input, and the soft reset are seams later units fill |
| Type-B gameplay | ✅ | [`features/type-b-ending.md`](features/type-b-ending.md) — a Type B round is delivered across three units: the shared round init and frame (`features/gameplay.md`), the starting garbage (`features/garbage-init.md`), and the win chain here — the scoreboard, and the dance the hardest level earns first, as per-state handlers. The Buran launch a height-5 win enters is `features/launch-scenes.md` |
| Launch scenes | ✅ | [`features/launch-scenes.md`](features/launch-scenes.md) — the two bonus endings, as per-state handlers: the Buran a Type B win from garbage height 5 earns, and the rocket a 100 000-point Type A game earns. Both build their pad on the second background map, hold, ignite, and fly the vehicle off the top of the screen before handing back to a screen that already exists; the Buran prints a congratulations message on the way out |
| Demo playback | ✅ | [`features/demo-playback.md`](features/demo-playback.md) — the title screen's attract demos: the launch and the Type A / Type B alternation, the run-length replay that substitutes recorded input for the player's, the restore that hands the buttons back, and the two ways a demo ends |
| Demo recording (dead-but-present) | ✅ | [`features/demo-playback.md`](features/demo-playback.md) — ported with playback, and dead exactly as it is in the original: the recorder runs every gameplay frame behind a flag nothing sets, because the routine that arms it has no caller |
| Serial protocol | ⬜ | `features/serial-protocol.md` |
| Multiplayer | ⬜ | `features/multiplayer.md` |
| Heart Mode (easter egg) | ⬜ | `features/heart-mode.md` |
| Victory / defeat screens | ⬜ | `features/victory-defeat-screens.md` |
| High-score recording | ✅ | [`features/high-score-recording.md`](features/high-score-recording.md) — a finished round's score compared against the three stored for its difficulty, inserted if it beat one, staged for the difficulty screen, and named on the letter-wheel entry screen; the tables are written to disk when a name is submitted |
| Boot path and init quirks | ✅ | [`features/boot.md`](features/boot.md) — the cold boot and the soft reset the four-button chord runs, which differ only in whether the top-score tables are cleared; the startup ordering that lets a launch keep a player's saved scores; and the preserved routine-copy overrun, carried as an equivalence |
| CPU virtualization | 🟫 | [`features/cpu-fidelity.md`](features/cpu-fidelity.md) — provided by the engine; no port-side integration |

## Rendering

| Feature | Status | Doc |
|---|---|---|
| Background rendering | ✅ | [`features/background-rendering.md`](features/background-rendering.md) — the displayed map bridged to the engine's tile path and submitted as one layer, with the tile-art regime it resolves against. Both background maps are carried, so the field wipe sweeps, the line-clear flash flashes, and pausing shows the paused screen. The screen does not blank while a tilemap loads, as the hardware's does |
| Sprite renderer | ✅ | [`features/sprite-renderer.md`](features/sprite-renderer.md) — the walk that fills the object buffer, restored at the sites the handlers make it from, plus the bridge that submits it as a layer |
| Tilemap loader | ✅ | [`features/background-rendering.md`](features/background-rendering.md) — the backdrop stamp (`loadScreenTilemap`) and the tile-art selection, restored at every solo screen. The window form of the original's loader serves only the second background map and lands with whichever unit models it |
| Sprite-copy routine | ⬜ | `features/dma.md` |
| Frame tick handler | ⬜ | `features/vblank.md` |
| Settings screen | ✅ | [`features/settings.md`](features/settings.md) — fullscreen, window size, twelve colour palettes, a guarded erase of the high scores, and a guarded quit; reached from the title screen and from a paused round, with the choices saved beside the top scores |
| Presentation framework | ⬜ | `features/presentation-framework.md` |
| Integer-scale output | ⬜ | `features/presentation-integer-scale.md` |
| Free-aspect output | ⬜ | `features/presentation-free-aspect.md` |
| Pixel-art upscaling shaders | ⬜ | `features/presentation-pixel-art-shaders.md` |
| DMG display shader | ⬜ | `features/presentation-dmg-shader.md` |

## Entry and integration

| Feature | Status | Doc |
|---|---|---|
| Entry point and platform init | ✅ | [`features/background-rendering.md`](features/background-rendering.md) — the boot host: engine config, asset check, platform and renderer, the shared virtual machine, the art upload, every handler installed, and the startup that leaves the machine at the copyright screen ([`features/boot.md`](features/boot.md)) |
| Bootstrap and main loop | ✅ | [`features/background-rendering.md`](features/background-rendering.md) — the two run-loop callbacks (advance the divider and run a game frame; compose and submit). The machine itself starts from the ported startup routine — see [`features/boot.md`](features/boot.md) |
| Golden-frame test harness | ⬜ | `features/golden-frame-harness.md` |
| Demo-replay integration test | ⬜ | `features/demo-replay-test.md` |
| Tick scheduling | 🟫 | — (the engine run loop is the scheduler) |
