# Features

Status registry — one row per feature. `features/CHANGELOG.md` records every status transition
chronologically; this file holds current state.

**Legend:** ⬜ pending · 🟡 in progress · ✅ delivered · 🟫 dropped · ❌ rejected

---

## Infrastructure

| Feature | Status | Doc |
|---|---|---|
| Repository scaffolding and ignore rules | ✅ | — |
| Build system | ⬜ | `features/build-system.md` |
| Test harness | ✅ | — |
| Retro++ engine adoption | ✅ | [`features/engine-adoption.md`](features/engine-adoption.md) |
| Logging | ✅ | — (spdlog used directly; no wrapper) |
| Platform abstraction facade | ❌ | — (superseded by engine adoption 2026-08-03) |
| Asset acquisition | ⬜ | [`features/asset-acquisition.md`](features/asset-acquisition.md) |
| ROM extraction tool | ⬜ | (covered by `asset-acquisition.md`) |
| Continuous integration | ⬜ | [`features/ci.md`](features/ci.md) |
| Distributable build | ⬜ | `features/distributable-build.md` |

## Data

Constant tables and graphics data derived from the ROM.

| Feature | Status | Doc |
|---|---|---|
| Core enums (piece, game type, music type, game state, serial) | ⬜ | `features/core-enums.md` |
| Character map | ⬜ | `features/charmap.md` |
| Piece rotation matrices | ⬜ | `features/piece-matrices.md` |
| Gravity / frames-per-drop table | ⬜ | `features/gravity-table.md` |
| Scoring tables | ⬜ | `features/scoring-tables.md` |
| Playing-field wipe patterns | ⬜ | `features/wipes.md` |
| Tile graphics | ⬜ | `features/tile-graphics.md` |
| Tilemaps | ⬜ | `features/tilemaps.md` |
| Sprite rotation tables | ⬜ | `features/sprite-oam-rotations.md` |
| Sprite tile data and sprite lists | ⬜ | `features/sprite-tiles.md` |
| Garbage-fill tables | ⬜ | `features/garbage-init.md` |
| Music data | ⬜ | `features/music-data.md` |
| Sound-effect data | ⬜ | `features/sfx-data.md` |
| Demo data | ⬜ | `features/demo-data.md` |
| Miscellaneous constants | ⬜ | `features/misc-constants.md` |

## State

Mutable game state, mirroring the original's RAM layout as ordinary C++ structs.

| Feature | Status | Doc |
|---|---|---|
| Global game state | ⬜ | `features/engine-state.md` |
| Game-state-machine state | ⬜ | `features/game-state-machine-state.md` |
| Audio state | ⬜ | `features/audio-state.md` |
| Sprite renderer state | ⬜ | `features/sprite-renderer-state.md` |
| Serial / multiplayer state | ⬜ | `features/serial-state.md` |
| Demo state | ⬜ | `features/demo-state.md` |
| High-score state | ⬜ | `features/high-score-state.md` |

## Systems

Game logic.

| Feature | Status | Doc |
|---|---|---|
| Randomization | ⬜ | `features/rng.md` |
| Input | ⬜ | `features/input.md` |
| Game-state dispatcher | ⬜ | `features/dispatcher.md` |
| Piece system | ⬜ | `features/piece-system.md` |
| Line-clear logic | ⬜ | `features/line-clear.md` |
| Scoring | ⬜ | `features/scoring.md` |
| Chiptune audio backend | ⬜ | [`features/audio-engine.md`](features/audio-engine.md) |
| Anti-channel-stealing option | ⬜ | [`features/anti-channel-stealing.md`](features/anti-channel-stealing.md) |
| Title / config / menu screens | ⬜ | `features/menu-screens.md` |
| Type-A gameplay | ⬜ | `features/type-a-gameplay.md` |
| Type-B gameplay | ⬜ | `features/type-b-gameplay.md` |
| Demo playback | ⬜ | `features/demo-playback.md` |
| Demo recording (dead-but-present) | ⬜ | `features/demo-recording.md` |
| Serial protocol | ⬜ | `features/serial-protocol.md` |
| Multiplayer | ⬜ | `features/multiplayer.md` |
| Heart Mode (easter egg) | ⬜ | `features/heart-mode.md` |
| Victory / defeat screens | ⬜ | `features/victory-defeat-screens.md` |
| High-score recording | ⬜ | `features/high-score-recording.md` |
| Boot path and init quirks | ⬜ | `features/boot.md` |
| CPU virtualization | 🟫 | [`features/cpu-fidelity.md`](features/cpu-fidelity.md) — provided by the engine; no port-side integration |

## Rendering

| Feature | Status | Doc |
|---|---|---|
| Sprite renderer | ⬜ | `features/sprite-renderer.md` |
| Tilemap loader | ⬜ | `features/tilemap-loader.md` |
| Sprite-copy routine | ⬜ | `features/dma.md` |
| Frame tick handler | ⬜ | `features/vblank.md` |
| Presentation framework | ⬜ | `features/presentation-framework.md` |
| Integer-scale output | ⬜ | `features/presentation-integer-scale.md` |
| Free-aspect output | ⬜ | `features/presentation-free-aspect.md` |
| Pixel-art upscaling shaders | ⬜ | `features/presentation-pixel-art-shaders.md` |
| DMG display shader | ⬜ | `features/presentation-dmg-shader.md` |

## Entry and integration

| Feature | Status | Doc |
|---|---|---|
| Entry point and platform init | ⬜ | `features/main-entry.md` |
| Bootstrap and main loop | ⬜ | `features/engine-bootstrap.md` |
| Golden-frame test harness | ⬜ | `features/golden-frame-harness.md` |
| Demo-replay integration test | ⬜ | `features/demo-replay-test.md` |
| Tick scheduling | 🟫 | — (the engine run loop is the scheduler) |
