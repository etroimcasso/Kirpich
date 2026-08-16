# Contract — line-clear pipeline

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routines:** `CheckForCompletedRows` (`tetris.asm:5301-5410`), `AnimateLineClear`
(`:5412-5496`), `MoveBlocksDownAfterLineClear` (`:5498-5550`), `ClearLineClearsList` (`:5552-5560`),
the per-row wipe dispatchers `PlayingFieldWipe02`–`PlayingFieldWipe19` (`:5563-5810`) and
`WipePlayingFieldRow` (`:5896-5908`).
**Frame anatomy:** the main loop (`:386-417`) and the vertical-blank handler (`:214-232`).

This document is the behavioral authority the port's tests are written against. It describes what the
original game does; the port reproduces that observable behavior.

---

## 1. What the line-clear pipeline is

After a piece locks, a sequence runs to completion over many frames: find the completed rows and tally
them, flash them a few times, drop the stack down into the gaps, then redraw the field one row per
frame until the next piece spawns (or a Type B round ends). The port carries the sim side of that
sequence as free functions in `src/systems/line_clear.{h,cpp}` (`kirpich::systems`); they own no state
— every field lives on the already-ported state structs, and the functions take the game context by
reference.

The five routines advance a small state machine through three counters on `GameFlowState`:

- `pieceLockStage` (`$FF98`) — 2 = just locked, 3 = flashing, 0 = done.
- `blinkCounter` (`$FF9C`) — the flash phase, 0..6.
- `wipeCounter` (`$FFE3`) — 0 = idle, 1 = compact pending, 2..19 = redrawing row by row.

Two of the routines can spawn the next piece (`NextPiece`, see
[`piece-system.md`](piece-system.md)); the port passes the randomizer's draw as a callable so those
paths stay device-free.

---

## 2. The pipeline spans two execution contexts (the frame anatomy)

The routines do not all run at the same point in a frame, and the cadences depend on which point each
runs at.

- `CheckForCompletedRows`, `LockPieceIntoBackground`, and `MoveBlocksDownAfterLineClear` run in the
  **handler beat** — the gameplay handlers call them each frame (`:1683-1687` solo, `:4414-4418` two
  player).
- `AnimateLineClear` and the eighteen wipe dispatchers run in the **vertical-blank handler**
  (`:214-232`), after the main loop's timer decrement.

The main loop's frame order is: dispatch (the handler beat) → soft-reset chord → decrement `timer1`
(`$FFA6`) and `timer2` (`$FFA7`), each saturating at 0 → vertical blank (`AnimateLineClear`, then the
wipe dispatchers, `:214-232`). The consequences that make the flash and wipe cadences exact:

- `CheckForCompletedRows` sets `timer1 = 2` during the handler beat; the same frame's timer beat
  decrements it to 1, so `AnimateLineClear` first acts two vertical blanks after the scan.
- Each flash pass sets `timer1 = 10` in the vertical blank, so a pass runs every 10 frames (the
  decrement to 0 happens across the following frames' timer beats).
- The flash's terminal sets `timer1 = 13` and `wipeCounter = 1`. `MoveBlocksDownAfterLineClear` (handler
  beat) fires on the frame whose previous timer beat drove `timer1` to 0 — it sets `wipeCounter = 2`,
  and the same frame's vertical blank redraws the first wipe row.

**The port defers the wiring, not the timing.** The handler-side functions are called by the gameplay
handlers, and the vertical-blank-side functions by the frame's vertical-blank tick; both land later.
The port's tests reproduce this anatomy with a frame harness (handler beat → saturating timer decrement
→ vertical-blank beat) so the cadence assertions are frame-exact.

---

## 3. `checkForCompletedRows` — scan and tally

`CheckForCompletedRows` (`:5301-5410`) runs only on the frame a piece finishes locking
(`pieceLockStage == 2`, `:5302-5304`); otherwise it returns.

1. **Lock sound.** `NoiseSfxId::LOCK_PIECE` (`$02`) is cued on **every** stage-2 entry, whether or not
   a row clears (`:5305-5306`).
2. **Scan.** Reset the completed count to 0 and rebuild the completed-rows list. The scan covers **16
   of the 18 field rows, starting at field row 2** (`$C842`, `:5310-5311`) — the top two rows are never
   scanned. A row is complete when none of its ten field cells is the empty space (`cp " "`, `:5317`;
   the empty cell ties `CharTile::SPACE`). Complete rows are recorded top to bottom by field-row index
   and the count incremented (`:5321-5337`). The original stores the WRAM row **addresses** (high byte
   then low); the port stores field-row indices — the mapping (`addr = $C802 + row * $20`, with the
   field's left edge at column offset 2) is pinned in
   [`playing-field-state.md`](playing-field-state.md).
3. **Stage advance.** `pieceLockStage = 3`, `timer1 = 2` (`:5339-5342`).
4. **No completed rows → return** (`:5343-5345`): no tally.
5. **Line-count update**, forked on `gameType` (`cp $77`, `:5348-5350`):
   - **Type A** (`:5351-5364`): `lines = min(lines + n, 9999)` — the original's two-byte BCD add with
     `$99$99` saturation collapses to a decimal add and clamp (the decimal `lines` surface,
     [`game-state-machine-state.md`](game-state-machine-state.md)).
   - **Type B** (`:5366-5376`, `:5407-5410`): if `lines <= n` then `lines = 0` (the `jr z` + `jr c`
     floor); else `lines -= n`, then a preserved guard — if the result's tens digit is 9
     (`and $F0; cp $90`) then `lines = 0`. That guard is a decimal-adjust artifact of a borrow and is
     **unreachable** for legal Type B states (the line goal never exceeds 25); it is the original's own
     annotated `TODO Is this a thing that can happen?`, ported verbatim and recorded here as
     preserved-dead.
6. **Per-kind tally** (`:5377-5401`), for `n` ∈ {1,2,3,4}: increment the matching count
   (`stats.singles`/`doubles`/`triples`/`tetrises`, a wrapping 8-bit `inc`); set the rows-to-send-as-
   garbage count `garbageRowsToSend` (`$FFDC`) to {1→0, 2→1, 3→2, 4→4} (the `b`-register reuse at
   `:5397-5398`); cue the square channel with `SquareSfxId::LINE_CLEAR` (`$06`), or
   `SquareSfxId::TETRIS` (`$07`) for `n == 4`.

At most four rows complete from one lock (a piece is four cells tall and the field never rests with a
complete row), matching the original's four-entry list.

---

## 4. `animateLineClear` — the flash cadence (the flash pixels are render)

`AnimateLineClear` (`:5412-5496`) runs in `pieceLockStage == 3` and only when `timer1 == 0`
(`:5413-5418`); otherwise it returns.

- **Even flash phase, empty list** (`:5420-5426`, `:5494-5496`): this is a lock that cleared nothing —
  spawn the next piece (`NextPiece`) and set `pieceLockStage = 0`. `blinkCounter`, `timer1`, and
  `wipeCounter` are untouched. This is the original's own annotated "What the fuck" path: every
  non-clearing lock spawns its successor here, two vertical blanks after the scan.
- **Otherwise**: the flash pass writes video memory only (even phase: solid grey `$8C`, spaces on phase
  6; odd phase: restore each row from the WRAM board — `:5427-5447`, `:5470-5492`). That is a render
  effect with no game-state change; a renderer re-derives it from the flash phase, the completed-rows
  list, and the board. Then the phase steps (`:5448-5468`): `blinkCounter++`; on reaching 7 →
  `blinkCounter = 0`, `timer1 = 13`, `wipeCounter = 1`, `pieceLockStage = 0` (the flash terminal); else
  `timer1 = 10`.

Seven passes total (phases 0..6) at a 10-frame cadence; the board is never touched here.

---

## 5. `moveBlocksDownAfterLineClear` — compaction and the top-row duplicate quirk

`MoveBlocksDownAfterLineClear` (`:5498-5550`) runs only when `timer1 == 0` and `wipeCounter == 1`
(`:5499-5504`); otherwise it returns.

For each listed row `r`, in list order (top to bottom), every row above `r` drops down by one:
`for dest = r down to 1: fieldCell(dest) = fieldCell(dest - 1)`, ten cells per row. Row 0 is never
written inside this walk — the original stops when the source address crosses above the field top
(`cp a, $C7`, `:5529-5533`). After **all** listed rows are processed, row 0's ten cells are cleared to
`CharTile::SPACE` **once** (`:5540-5546`).

**The multi-clear top-row duplicate quirk.** Because row 0 is cleared only once, after every listed
row, a multi-row clear with content in the top rows leaves shifted copies of the old top row in rows
1..n−1 rather than emptying them. This is the original's behavior, preserved verbatim and pinned by a
test vector.

Only the ten field columns move; walls, the border, and everything outside the field's ten-column span
stay untouched (the original copies exactly ten bytes per row). Then the list is cleared
(`ClearLineClearsList`, `:5547`) and `wipeCounter = 2` (`:5548-5549`).

---

## 6. `clearLineClearsList` — empty the list

`ClearLineClearsList` (`:5552-5560`) zeroes the nine list bytes and nothing else; a cleared list is the
equivalence (the completed count is not touched). The original calls it from five state-init sites
(`:535`, `:1233`, `:2924`, `:3060`, `:4586`) that the menu and gameplay handlers wire when they land,
so it is a public function.

---

## 7. `playingFieldWipeTick` — the row-by-row redraw

The original has eighteen near-identical routines, `PlayingFieldWipe02`–`PlayingFieldWipe19`
(`:5563-5810`), each gating on one exact `wipeCounter` value and redrawing one field row via
`WipePlayingFieldRow` (`:5896-5908`), which increments the counter. The vertical-blank handler calls
them in **descending** order (19 → 02, `:215-232`) every frame, so exactly one row-step happens per
frame — a higher counter redraws a higher row on screen. `WipePlayingFieldRow`'s row copy is WRAM →
video RAM (render); its **sim** effect is the counter increment.

The port collapses the eighteen dispatchers into one range-gated step
(`playingFieldWipeTick`): return unless `wipeCounter` is in
[`kPlayingFieldWipeCounterFirst` (2), `kPlayingFieldWipeCounterLast` (19)]; otherwise advance the
counter by one and run that step's side effect. The collapse is exact because the counter increments
inside the row copy and the calls run descending, so an increment can never cascade into a second
row-step in the same frame. The row a step redraws is `playingFieldRowForWipeCounter(step)` (bottom row
first; [`playing-field.md`](playing-field.md)); that copy is render and is owned by the presentation
pass.

Counter 19's own increment to 20 is dead — the terminal step (below) overwrites the counter with 0
before anything reads it — so the port skips the increment inside the final step.

### 7a. Step 8 — the stack-fall / garbage cue (`:5619-5645`)

After the row copy: solo (`!isMultiplayer`) — if `gameState == GameState::NORMAL_GAMEPLAY`, cue
`NoiseSfxId::STACK_FALL` (`$01`), else nothing. Multiplayer — only in `GameState::TWO_PLAYER_GAME`
(`$1A`): if `garbageWipeActive` (`$FFD4`), cue `SquareSfxId::GARBAGE_ATTACK` (`$05`), else the same
`STACK_FALL` noise.

### 7b. Steps 16–18 — score / level redraws (no sim effect in this pipeline)

- **Step 16** (`:5710-5718`) calls the Type A level-up check (`Call_244B`, `:5825-5876`): a BCD
  lines-vs-level comparison, the level increment, the gravity reload, the level-up sound, and the level
  digits. Level-up gating belongs to the scoring work; the step here is a recorded seam with no sim
  effect yet.
- **Step 17** (`:5720-5731`) prints the score into the paused-screen tilemap and sets the lines-redraw
  mechanism byte (`$FFE0`, an adjudicated mechanism, see
  [`serial-multiplayer-state.md`](serial-multiplayer-state.md)) — render only.
- **Step 18** (`:5733-5742`) prints the score into the second paused-screen tilemap — render only.

### 7c. Step 19 — the terminal (`:5744-5810`)

Replacing the dead increment:

1. `blockSoftDropAfterLock = true` (`ld [$C0C7], a` with `a = 19`, `:5748` — the bool collapse; nonzero
   means latched, and the piece system's soft-drop unlatch consumes it, [`piece-system.md`](piece-system.md)
   §5).
2. `wipeCounter = 0` (`:5752-5753`).
3. Gates (`:5754-5759`, `:5802-5810`): solo — if `gameState != NORMAL_GAMEPLAY`, return. Multiplayer —
   if `gameState != TWO_PLAYER_GAME`, return; then if `garbageWipeActive`, clear it and return (a
   garbage-driven wipe ends here — no line print, no spawn).
4. The line count is redrawn (`PrintNumber`, `:5760-5771`) — render.
5. `gameType == GameType::TYPE_A` → spawn the next piece, return (`:5772-5774`).
6. Type B, `lines != 0` → spawn the next piece, return (`:5775-5777`).
7. **Type B win** (`:5778-5796`): `timer1 = 0x64`; cue `MusicId::STAGE_CLEAR` (`$02`); if `isMultiplayer`
   then `linesGoalReached = true` (`$FFD5`) and return, with `gameState` left alone; else
   `gameState = (typeBLevel == 9) ? GameState::INIT_TYPE_B_BONUS (`$22`) : GameState::TYPE_B_VICTORY_JINGLE
   (`$05`)`.

---

## 8. The audio cues

The routines here do not call the sound driver — they write into the audio cue mailbox
([`audio-state.md`](audio-state.md)): the lock sound and stack-fall (noise channel), the line-clear /
Tetris / garbage-attack sounds (square channel), and the stage-clear music. The mailbox holds the last
write of the frame; the audio tick that drains it lands with the sound system.

---

## 9. State-field resolution (no new state is minted)

| Original byte | Port field |
|---|---|
| `$FF98` lock stage | `flow.pieceLockStage` |
| `$FF9C` flash phase | `flow.blinkCounter` |
| `$FFA0` completed count | `flow.completedRowCount` |
| `$FFA6` frame timer | `flow.timer1` |
| `$FF9E` lines | `flow.lines` (decimal `uint16_t`) |
| `$FFC0` game type | `flow.gameType` |
| `$FFC3` Type B level | `flow.typeBLevel` |
| `$FFE1` game state | `flow.gameState` |
| `$FFE3` wipe counter | `flow.wipeCounter` |
| `$FFC5` multiplayer flag | `multiplayer.isMultiplayer` |
| `$FFD4` garbage wipe active | `multiplayer.garbageWipeActive` |
| `$FFD5` lines goal reached | `multiplayer.linesGoalReached` |
| `$FFDC` rows to send | `multiplayer.garbageRowsToSend` |
| `wLineClearsList` | `engine.lineClears` |
| `wSinglesCount`..`wTetrisCount` | `engine.stats` |
| `$C0C7` post-lock latch | `engine.blockSoftDropAfterLock` |
| `$C800` board | `field.board` via `fieldCell` |
| cue mailbox bytes | `game.audioCues` |

---

## 10. The stage-2 gravity window is unreachable — the `{3}`-only drop guard is exact

The piece system's soft-drop path gates on lock stages `{1,2,3}` explicitly (`:5181-5183`), but the
gravity path's guard tests only stage 3 (`cp a, 3`, `:5212-5214`). These are behaviorally identical in
every reachable state: a gravity step cannot happen while the lock is at stage 2. The locking frame
reloads the drop timer to the per-level frames-per-drop (≥ 2 at every level), the stage-2 frame
decrements it to ≥ 1 without firing, and `CheckForCompletedRows` advances the stage to 3 on that same
frame. So no gravity step is ever attempted at stage 2, and the piece system's stage-3-only gravity
guard is exact — recorded here to close the open note in [`piece-system.md`](piece-system.md) §5.

---

## 11. Verification — the substitute for a two-baseline trace

A full side-by-side trace against the original is not possible before the frame loop exists, since
these routines run inside the frame the loop drives. In its place the port pins, in
`tests/test_line_clear.cpp`, using a frame harness that reproduces the §2 anatomy:

1. **Scan** — the stage gate, the 16-of-18-row window (rows 0/1 never detected, 2–17 detected), the
   one-empty-cell-anywhere incompleteness, the top-to-bottom list of field-row indices, the count, the
   stage/timer advance, and the unconditional lock cue.
2. **Tally** — the Type A add and 9999 clamp; the Type B floor (equal and borrow cases) and the
   preserved tens-digit-9 guard; the per-kind stat increment; the rows-to-send map; the clear-vs-Tetris
   cue; the no-clear early-out.
3. **Flash cadence** — the seven-pass phase walk at the 10-frame cadence through the harness; the
   terminal state; the board untouched throughout.
4. **No-clear spawn** — the even-phase / empty-list path spawns the next piece and ends the lock,
   leaving the counters untouched, two frames after a clear-less scan.
5. **Compaction** — the gates; the single-clear shift with the top row cleared and the non-field
   columns byte-identical; the multi-clear top-row duplicate quirk; the list clear and wipe advance.
6. **Wipe stepper** — the out-of-range no-ops; the one-step-per-frame 2→19→0 walk with the row identity
   cross-checked against `playingFieldRowForWipeCounter`; the step-8 cue gating; the soft-drop latch
   re-armed at the final step.
7. **Terminal** — the wrong-state returns (counter still reset, latch still armed); the multiplayer
   garbage-wipe consume path; the Type A and lines-remaining Type B spawns; the Type B win (hold,
   stage-clear music, the solo level-9 fork, and the multiplayer lines-goal flag with the state left
   alone).

**Known gap:** none of the above compares against a capture from real hardware or a reference emulator.
The gap is inherent to porting without the original runtime available; it is recorded here and revisited
if a hardware trace becomes available.

---

## Tested by

`tests/test_line_clear.cpp` — seven behavioral cases: `ScanVectors`, `LinesTallyVectors`,
`AnimateCadence`, `NoClearSpawnPath`, `CompactionVectors`, `WipeStepperWalk`, `Wipe19TerminalVectors`.
All device-free; every asserted value traced to the lines named above.
