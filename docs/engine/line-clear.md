# Line-clear pipeline

The sequence that runs once a piece locks: find the completed rows, flash them, drop the stack into the
gaps, and redraw the field a row at a time until the next piece spawns. The behavioral specification —
what the original game does, line by line — is in [`../contracts/line-clear.md`](../contracts/line-clear.md);
the design rationale is in [`../features/line-clear.md`](../features/line-clear.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/line_clear.h` / `.cpp` | The `kirpich::systems` line-clear functions. |
| `tests/test_line_clear.cpp` | The behavioral tests. |

The functions take a `GameContext&` (`src/systems/game_context.h`) and read or write through it. They
own no state: the lock stage, the flash phase, the wipe counter, and the frame timers live on
`GameFlowState`; the completed-rows list and the per-kind clear stats on `EngineState`; the board on
`PlayingFieldState`; the multiplayer flags on `MultiplayerState`; the audio cues on the `AudioCues`
member.

## The surface

```cpp
namespace kirpich::systems {

// Scan the field for completed rows on the frame a piece finishes locking. Records the completed rows
// (by field-row index) and their count, adjusts the line count for the game type, bumps the per-kind
// stat and garbage count, and cues the lock and clear sounds. Advances the lock into its flash stage.
void checkForCompletedRows(GameContext& game);

// Run one frame of the flash-then-spawn step. While rows flash it advances the flash phase on its
// timed cadence and, after the last pass, hands off to the wipe. A lock that cleared no rows spawns the
// next piece here and ends the lock. `draw` feeds that spawn.
void animateLineClear(GameContext& game, const std::function<std::uint8_t()>& draw);

// Compact the field once the flash finishes: drop every row above each cleared row down by one and
// empty the top row. Then clear the completed-rows list and advance the wipe to its first redraw step.
void moveBlocksDownAfterLineClear(GameContext& game);

// Empty the completed-rows list.
void clearLineClearsList(GameContext& game);

// Run one step of the field wipe. Each call advances the wipe by one row (the row redraw is a render
// effect). Particular steps cue the stack-fall sound and, on the final step, spawn the next piece or
// finish a Type B round. `draw` feeds that spawn.
void playingFieldWipeTick(GameContext& game, const std::function<std::uint8_t()>& draw);

}
```

**Using them.** The five functions are called from two points in a frame. In the gameplay handler,
after the piece's lock is finished, run the scan; and, once the flash's hold has elapsed, the
compaction:

```cpp
kirpich::systems::checkForCompletedRows(game);        // scan + tally, on the lock frame
kirpich::systems::moveBlocksDownAfterLineClear(game); // compaction, when the flash hold reaches 0
```

In the frame's vertical-blank tick, run the flash and then the wipe:

```cpp
kirpich::systems::animateLineClear(game, drawByte);    // flash cadence, and the no-clear spawn
kirpich::systems::playingFieldWipeTick(game, drawByte); // one field-wipe row-step
```

`drawByte` is a `std::function<std::uint8_t()>` — the randomizer's registered routine for live play
(see [`piece-random.md`](piece-random.md)). It is consulted only when a path spawns the next piece
(a lock that cleared nothing, and the wipe's terminal step) and only on the random (solo) path.

The counters carry the state through the sequence: `game.flow.pieceLockStage` (2 → 3 → 0),
`game.flow.blinkCounter` (the flash phase 0..6), and `game.flow.wipeCounter` (0 idle, 1 compact
pending, 2..19 redrawing). The scan sets stage 3 and arms `timer1`; the flash's last pass sets
`wipeCounter = 1`; the compaction sets `wipeCounter = 2`; the wipe steps it to 19 and the terminal step
resets it to 0.

## Gotchas

- **The order and the frame beats matter.** The scan and compaction belong in the gameplay handler; the
  flash and wipe belong in the vertical-blank tick, which runs after the frame's timer decrement. The
  10-frame flash cadence and the one-row-per-frame wipe depend on that split — running them all at one
  point changes the timing. The tests reproduce the beats with a harness.
- **`checkForCompletedRows` only acts on the lock frame.** It gates on `pieceLockStage == 2`; called
  any other frame it returns. It cues the lock sound and advances the stage on every stage-2 entry, even
  when nothing clears.
- **The board is space, not zero.** A cell counts as empty only when it holds `CharTile::SPACE`; a boot
  board is all-zero. The playable field is space-filled during title-screen setup.
- **The wipe stepper runs once per call.** `playingFieldWipeTick` advances the counter by one and does
  nothing when the counter is outside 2..19. Call it once per frame from the vertical-blank tick; it is
  the only thing that returns the wipe counter to 0 and spawns the next piece after a clear.
- **The audio cues accumulate.** The functions write `game.audioCues` (lock, stack-fall, line-clear /
  Tetris / garbage-attack, stage-clear music); nothing drains them yet — the audio tick will. Read the
  mailbox as the last cue of the frame.
- **The multi-clear top-row duplicate quirk is intentional.** Clearing more than one row leaves copies
  of the old top row in more than one row — the original's behavior, preserved. Do not "fix" it.

## Changing behavior

- **The scan window and completeness rule** are in `checkForCompletedRows`: the scan starts at field
  row 2 (`kUnclearedTopRows`, file-local) and a row is complete when none of its ten cells is
  `CharTile::SPACE`. The list stores field-row indices.
- **The line-count update** is in `checkForCompletedRows`: the Type A add and 9999 clamp, and the
  Type B count-down with its floor and the preserved tens-digit-9 guard. Change it only against the
  contract (§3).
- **The per-kind tally** — the stat increment, the rows-to-send-as-garbage map {1→0, 2→1, 3→2, 4→4},
  and the clear-vs-Tetris cue — is the `switch` at the end of `checkForCompletedRows`.
- **The flash cadence** is in `animateLineClear`: seven passes, `timer1 = 10` between them,
  `timer1 = 13` and `wipeCounter = 1` at the terminal. The flash pixels themselves are render and are
  not here.
- **The compaction** is in `moveBlocksDownAfterLineClear`: the descending shift and the single top-row
  clear after all listed rows. The order of the clear is what produces the multi-clear quirk.
- **The wipe steps** are in `playingFieldWipeTick`: the range gate, the counter advance, the step-8
  cue gating, and the terminal (step 19) — the soft-drop latch, the counter reset, the wrong-state
  returns, the multiplayer garbage-wipe consume, the Type A / Type B spawns, and the Type B win. Steps
  16–18 are render seams (score/level redraws) with no sim effect yet.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^LineClear\.'
```

The tests are device-free: the functions are pure logic over the game-state aggregate, driven through a
frame harness that reproduces the handler / timer / vertical-blank beats.
