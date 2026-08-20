# Launch scenes

The two bonus endings: the Buran shuttle a height-5 Type B win earns, and the rocket a 100 000-point
Type A game earns. Fifteen state handlers in `src/systems/launch_scenes.{h,cpp}`, installed into the
frame dispatcher by `installLaunchSceneHandlers(dispatcher)`.

Behaviour with source line anchors: [`../contracts/launch-scenes.md`](../contracts/launch-scenes.md).
Design rationale: [`../features/launch-scenes.md`](../features/launch-scenes.md).

## What runs, in order

Each chain is a run of states, one handler per frame, each gating on the frame timer until its step is
due.

```
Buran   $26 → $27 → $28 → $29 → $02 → $03 → $2C → $2D → $05  (Type B scoreboard)
rocket  $34 → $2E → $2F → $30 → $31 → $32 →────── $33 → $10  (Type A difficulty screen)
```

| Handler | State | Does |
|---|---|---|
| `initBuran` | `$26` | Builds the pad, places the shuttle, starts the music |
| `prepareBuranLaunch` | `$27` | Reveals both smoke plumes |
| `buranIgnition` | `$28` | Holds, then swaps the smoke art and clears the playing field |
| `buranIgnition2` | `$29` | Holds, then swings the umbilicals away |
| `buranLiftoff` | `$02` | Raises the shuttle to its ignition height, lights the exhaust |
| `buranRising` | `$03` | Flies it off the top, then seeds the message cursor |
| `printCongratulations` | `$2C` | Prints sixteen letters, one every six frames |
| `congratulations` | `$2D` | Restores the gameplay art, hands to the scoreboard |
| `gameOverToBonusEnding` | `$34` | Holds on the game-over screen |
| `initRocketLaunch` | `$2E` | Builds the pad, places the earned rocket, starts the music |
| `rocket` | `$2F` | Reveals both smoke plumes |
| `rocketIgnition` | `$30` | Holds, then clears the playing field |
| `rocketLiftoff` | `$31` | Raises the rocket to its ignition height, lights the exhaust |
| `rocketMainEngineFire` | `$32` | Flies it off the top |
| `endOfBonusScene` | `$33` | Restores the gameplay art, re-inits the driver, hands to the menu |

Two file-local helpers do the shared work: `buildLaunchPad` clears the second background map and stamps
the backdrop and right tower, and `flickerExhaust` blinks both smoke plumes on its own timer while a
state waits.

## Where the drawing goes

Both scenes draw into `DisplayState::secondMap` and set `displayed` to `DisplayedMap::SECOND` on entry,
`FIRST` on exit. The first map is never written, so a playing field underneath survives the sequence.

The pad's cells, as rows and columns of the second map:

| Piece | Rows | Columns | Painted by |
|---|---|---|---|
| Backdrop block | 14–17 | 0–19 | both pads |
| Right tower, two sides | 7–13 | 12, 13 | both pads |
| Left tower, two sides | 7–13 | 6, 7 | the Buran pad only |
| Umbilicals | 8 | 8, 9 | the Buran pad only |
| Crew tunnel | 9 | 8, 9 | the Buran pad only |
| Congratulations message | 4 | 2–17 | `printCongratulations` |

## What to edit

**A hold length.** The timer reloads are named constants at the top of `launch_scenes.cpp`:
`kPadHoldFrames` (187, both pads), `kMaximumHoldFrames` (255, the Buran's two ignition holds),
`kRocketRevealFrames` (160), `kRocketIgnitionFrames` (128), `kClimbStepFrames` (10, one step of either
climb), `kFlickerFrames` (10, the smoke blink) and `kExhaustFrameFrames` (6, the exhaust's frame
alternation and the message's letter cadence).

**Where a vehicle ignites or ends its flight.** `kBuranIgnitionY` / `kBuranTerminalY` and
`kRocketIgnitionY` / `kRocketTerminalY`. Both terminals are numerically *above* their ignition heights
because the coordinate wraps through zero during the climb — changing one to a value the climb passes
rather than lands on makes the scene run forever, since the test is equality. `kBuranExhaustDrop` /
`kBuranExhaustX` and their rocket counterparts place the exhaust when it lights.

**Where a pad piece sits.** The row and column constants near the top of the file, and
`kBuranPadFittings` for the four umbilical and crew-tunnel cells with their tiles.

**The message.** `kCongratulationsTilemap` in the tilemap data — it is sixteen tiles and the run length
follows from its size. `kCongratulationsRow`, `kCongratulationsFirstCol` and `kCongratulationsEndCol`
place it; `kCongratulationsUnderTile` is the tile drawn beneath each letter.

**The art or the objects.** The pad's tilemaps (`kBuranBackdropTilemap`, the four tower strips) and the
launch objects (`buranLaunchSprites()`, `rocketLaunchSprites()`) are stored data, extracted from the
cartridge — see [`data-layer.md`](data-layer.md). Which rocket a score earns is
`rocketSpriteForScore`, in the scoring data.

## Reaching them

Win a Type B round on level 9 started at garbage height 5, or finish a Type A game with at least
100 000 points.

## Gotchas

- **The second map clears to the space glyph, not to zero.** Clearing to zero looks identical to a
  headless test and wrong on screen.
- **The two pad stamps are not the shared screen loader.** `loadScreenTilemap` is fixed at a full
  18×20 screen from the top-left corner and cannot place a four-row block at row 14 or a seven-cell
  column, so this file has its own block and column stamps.
- **The chains differ in three places on purpose** — the rocket's sparser pad, its ignition setting no
  smoke art, and its lack of a congratulations screen — plus its exit re-initialising the sound driver
  where the Buran's does not, and `endOfBonusScene` having no timer gate at all. Each is asserted by a
  test, so removing one to make the chains symmetrical turns that test red.
