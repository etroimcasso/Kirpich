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
| Asset acquisition | ✅ | [`features/asset-acquisition.md`](features/asset-acquisition.md) — audio byte spans ride the audio backend |
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
| Garbage-fill tables | ✅ | [`features/garbage-init.md`](features/garbage-init.md) |
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
| Chiptune audio backend | ⬜ | [`features/audio-engine.md`](features/audio-engine.md) |
| Anti-channel-stealing option | ⬜ | [`features/anti-channel-stealing.md`](features/anti-channel-stealing.md) |
| Title / config / menu screens | ✅ | [`features/menu-screens.md`](features/menu-screens.md), [`features/title-screens.md`](features/title-screens.md) — the whole pre-game flow (copyright, title, config, and the game-type / music-type / difficulty selectors) is delivered as per-state handlers. The demo launch and the two-player serial paths are seams later units fill |
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
