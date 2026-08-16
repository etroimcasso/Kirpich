# Contract — piece system

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routines:** `NextPiece` (`tetris.asm:5078-5164`), `DownHeld` / `DropPiece`
(`:5167-5299`), `RotateAndShiftPiece` (`:5910-6028`), `DetectCollision` (`:6030-6065`),
`LockPieceIntoBackground` (`:6068-6107`).
**Geometry routines:** `_RenderSprites` (`:6687-6846`), `_LookupTile` (`:6558-6584`).

This document is the behavioral authority the port's tests are written against. It describes what the
original game does; the port reproduces that observable behavior.

---

## 1. What the piece system is

The active falling piece is manipulated once per frame by a small family of routines: spawn the next
piece, drop it by gravity or the faster soft drop, rotate and shift it under player input, test it
against the board, and — when it comes to rest — lock its tiles into the board. Two slots carry the
piece state through the sprite-object array: the **active piece** is always slot 0 (`$C200`) and the
**preview piece** always slot 1 (`$C210`). Every routine here reads and writes those slots and the
game-flow counters that time the fall.

The port ports the six routines as free functions in `src/systems/piece.{h,cpp}`
(`kirpich::systems`). They own no state — every field lives on the already-ported state structs, and
the functions take the game context by reference.

---

## 2. Collision and locking go through the piece's on-screen cells

Both `DetectCollision` (`:6030`) and `LockPieceIntoBackground` (`:6068`) read the active piece's four
sprite-object entries out of the rendered object buffer (`$C010`), map each entry's pixel position to
a board cell, and either test the cell (collision) or write the piece's tile into it (lock). The
original therefore depends on the piece having been rendered into the object buffer first.

The port computes the four board cells directly from the active slot's position and its composed
sprite, so no render step is needed — `activePieceCells` is the shared helper. It reproduces the
original's geometry exactly, including two quirks:

### 2a. The renderer's position law carries an 8-bit carry leak (LIVE for pieces)

`_RenderSprites` computes each part's on-screen coordinate as `offset + slotPos` then `+ partOffset`
using an SM83 `add` / `adc` pair (`:6783-6785` for Y, `:6808-6810` for X). The carry out of the first
add feeds the second. Each part's coordinate is:

```
base = uint8(spriteOffset) + slotPos      ; "add b" — may carry
coord = uint8( uint8(base) + partOffset + carry(base) )   ; "adc c"
```

The piece sprites' render offsets are negative (`offset_y = -17`, `offset_x = -16` for every piece
rotation), so the first add always overflows and the carry fires on **every** part of **every** piece.
The flip branches (`:6788`, `:6813`) are dead for pieces — the active slot's attribute byte is `$00`.

### 2b. The tile-lookup cell map

`_LookupTile` (`:6558-6584`) maps an on-screen pixel to a board cell by removing the hardware sprite
offsets and dividing by the 8-pixel tile size, and the collision/lock code reaches the board shadow by
adding `$30` to the address high byte (`:6044`):

```
row = uint8(coordY - 0x10) >> 3
col = uint8(coordX - 0x08) >> 3
cell = board[row][col]          ; board is the $C800 32-wide grid
```

All arithmetic is 8-bit wraparound.

### 2c. The hidden-piece Y law

When the active slot is hidden (status byte `$80`), the renderer writes `$FF` as every part's OAM Y and
the real X (`:6829-6833`). `activePieceCells` reproduces that (`coordY = 0xFF` → row 29), so collision
and locking read the piece's real cells even on a hidden piece.

### 2d. Fixed four cells

The 28 piece-rotation sprites each compose to exactly four parts, with no skip parts. So the active
piece always covers exactly four cells, matching the original's fixed four-slot collision and lock
loops (`b = 4`). The zero-OAM-X early-out in both loops (`:6037`, `:6078`) is unreachable for pieces —
their X is never zero — and is not ported.

---

## 3. `detectCollision` → collide or not

`DetectCollision` (`:6030-6065`) reports a collision when any of the four cells holds a byte other than
the empty space (`cp " "`, `:6047`; the empty cell ties `CharTile::SPACE`). The result is the routine's
`A` register (0 = clear, 1 = collide). The original also writes the result to a shadow byte (`$FF9B`,
annotated "Never checked?", `:6057`/`:6064`) — that write is dead and is dropped; the return value is
the contract.

Callers move the piece, call `detectCollision`, and revert the move on a collision — the original's
render-then-test-then-revert, minus the render.

---

## 4. `rotateAndShiftPiece` — rotation, then shift

`RotateAndShiftPiece` (`:5910-6028`) runs both stages every frame the active piece is manipulated. A
hidden active piece is skipped entirely (`:5911-5914`).

### 4a. Rotation (`:5920-5963`)

Counter-clockwise (**B**) is tested before clockwise (**A**). A piece's four orientations are four
consecutive sprite ids, so rotating steps the id within the piece's own four-id block:

- **Clockwise** = decrement; from orientation 0 (low two bits clear), set the low two bits instead, to
  wrap up to orientation 3 (`:5925-5936`).
- **Counter-clockwise** = increment; from orientation 3 (low two bits set), clear the low two bits
  instead, to wrap back to orientation 0 (`:5938-5949`).

On any rotation attempt, the rotate sound effect is cued **before** the collision test (`:5952`); on a
collision the rotation is reverted and the cue overwritten to none (`:5958-5962`) — the write-then-
cancel the audio mailbox exists for. Control always falls through to the shift stage.

### 4b. Shift with auto-repeat (`:5965-6028`)

Right is tested before left; a right press or hold short-circuits the left checks entirely
(right-over-left priority). Each direction's fire decision comes from the shared key-repeat core
(`keyRepeatFire`, [`input.md`](input.md) §4): a fresh press fires and arms the 23-frame initial delay;
a hold counts the timer down and fires on reaching zero, reloading the 9-frame repeat interval.

On a firing frame the piece moves ±8 pixels, the shift sound is cued, and the position is tested. On a
collision the move is reverted, the cue overwritten to none, and the repeat timer parked at 1 so the
next frame retries immediately (wall charge, `:6001`). When neither direction is active, the timer is
reloaded to 23 every frame (idle re-arm, `:6006-6011`), so a shift always begins after the full delay.

The original's ordering of the render and the SFX write differs between the right and left paths
(`:5988-5991` vs `:6022-6025`); both land before the audio tick, so the port uses one order — observably
equivalent.

---

## 5. `dropPiece` — gravity and soft drop

`DropPiece` (`:5194`) and its soft-drop entry `DownHeld` (`:5167`) are one control-flow blob; the port
folds `DownHeld` into `dropPiece` as an internal branch.

**Which branch runs.** The soft-drop branch runs when Down is held and neither Left nor Right is
(`hJoyHeld & (D|L|R) == DOWN`, `:5195-5198`; Up is outside the tested mask). Otherwise gravity runs.

**Soft-drop branch (`:5167-5191`).** If the post-lock latch (`$C0C7`) is set, only a fresh press that is
exactly Down among the three unlatches it; anything else cancels to the gravity path (`:5171-5176`).
Then three gates — `timer2`, the lock stage, the wipe counter, each non-zero → return (`:5178-5186`) —
guard the 3-frame soft-drop cadence: set `timer2 = 3` (`:5187`; the dispatcher's timer law decrements
it), increment the soft-drop counter, and step.

**Gravity branch (`:5199-5219`).** Zero the soft-drop counter; if the drop timer is non-zero, decrement
it and return; at zero, return if the lock stage is 3 or a wipe is running, else reload the drop timer
from the per-level frames-per-drop and step.

**The step (`:5220-5236`).** Move the piece down 8 pixels and test. No collision → return. On a
collision: revert, begin the lock (`pieceLockStage = 1`), set the post-lock latch, then run the
soft-drop award and the top-out check.

### 5a. Soft-drop award at lock (`:5237-5260`, `:5283-5299`)

Only when the soft-drop counter is non-zero. The award is one point per soft-dropped row **minus one**
— the original's own off-by-one (`softDropAward`, [`scoring.md`](scoring.md)). In a **Type A** game the
award is added to the score (saturating at the score ceiling) and a score redraw is flagged; in any
other mode it accumulates in the separate soft-drop total the scoreboard tallies later (a 16-bit binary
add). Then the counter is zeroed. (The original converts the count to decimal with an `inc`/`daa` loop;
that collapses to the port's decimal score surface, [`scoring.md`](scoring.md). Its mod-100 wrap needs a
count ≥ 101, unreachable at ≤ 17 rows per drop — recorded, not ported.)

### 5b. Top-out (`:5261-5281`)

On every lock, if the piece is at the spawn position (`Y == $18 && X == $3F`): when the spawn-lock
counter is already 1, request an audio re-init (`InitAudio`, `:5272`), set the game state to init-game-
over, and cue the game-over wave sound; otherwise increment the counter. The game tops out on the
**second** piece to lock at the spawn position. The counter lives at `$FFFB`, a byte it shares with the
top-score pointer; the two uses are disjoint in time
([`game-state-machine-state.md`](game-state-machine-state.md)).

---

## 6. `lockPieceIntoBackground` — tiles into the board

`LockPieceIntoBackground` (`:6068-6107`) runs only while the lock is at its first stage
(`pieceLockStage == 1`, `:6069-6071`); otherwise it returns. For each of the four cells it writes the
piece's tile into the board (`:6092-6098`). It then advances the lock stage to 2 and hides the active
piece (`:6103-6106`). The original also mirrors each tile into video RAM under an HBlank wait; the board
is that shadow, so the mirror and the wait are render-bridge mechanism and are not carried here.

---

## 7. `nextPiece` — spawn and choose the next preview

`NextPiece` (`:5078-5164`) resets the active slot and promotes the current preview into it: the active
slot becomes visible at the spawn position (`Y = $18`, `X = $3F`) with the preview's sprite id
(`:5079-5087`). The rejection reference is the promoted piece's kind (`:5088`).

**Deterministic path** — a demo is running or a two-player game is active (`:5090-5095`). Walk the shared
piece list by an index that wraps at 256 (`:5096-5108`; the original's `$C4` page-cross check is exactly
`uint8` index wraparound), and if received garbage is staged, mark it to apply at this piece (set bit 7,
`:5109-5113`). The next-preview is untouched.

**Random path** — solo play only (`:5116-5157`). Draw up to three candidates from the randomizer's fold
([`piece-random.md`](piece-random.md) §2). Reject a candidate whose kind, OR-ed with the next-preview and
the reference kind, adds no bit outside the reference kind (`:5150-5154`); the third draw is accepted
unconditionally. The accepted candidate becomes the next-preview; the piece promoted into the visible
preview is the **old** next-preview (a one-stage pipeline). This site differs from the shared
`PickRandomPiece` in two ways ([`piece-random.md`](piece-random.md) §4b): the reference is the visible
preview's kind (not the temp-preview), and the accept path writes the next-preview only — no temp-preview
touch, no return-of-previous.

Finally the visible preview is set and the drop timer reloaded from the per-level frames-per-drop
(`:5158-5163`). The port passes the randomizer's draw as a callable, so the tests can substitute a
scripted source and stay device-free.

---

## 8. The audio cues

The routines here do not call the sound driver — they write into the audio cue mailbox
([`audio-state.md`](audio-state.md)): the rotate and shift sounds (square channel), the game-over sound
(wave channel), and the re-init request at top-out. A cue can be written and then overwritten to none
within the same frame (the rotate and shift write-then-cancel); the mailbox holds the last write, and
the audio tick — which drains it — lands with the sound system.

---

## 9. Reset

The piece system holds no state of its own: the slots, the pipeline counters, the timers, and the audio
mailbox all live on the game-state structs and are cleared by their own resets. Nothing here needs a
reset hook.

---

## 10. Verification — the substitute for a two-baseline trace

A full side-by-side trace against the original is not possible before the main loop exists, since the
piece routines run inside the frame the loop drives. In its place the port pins, in
`tests/test_piece_system.cpp`:

1. **The cell geometry** — the carry-leak position law and the tile-lookup cell map against values
   hand-traced from `_RenderSprites` and `_LookupTile` for two piece sprites at the spawn position, the
   hidden-Y law, and the four-parts invariant across all 28 piece-rotation ids.
2. **Collision** — the empty-space tie and non-space collisions at hand-placed board cells.
3. **Rotation** — the four-orientation block-wrap in both directions, B-over-A priority, the hidden
   guard, collision revert, and the SFX write-then-cancel.
4. **Shift** — the DAS timeline at this site (press-arm, held-fire, wall-charge park, idle re-arm,
   stale-zero wraparound) and right-over-left priority.
5. **Drop** — the gravity timer, the three soft-drop gates, the 3-frame cadence, the post-lock latch and
   its fresh-press unlatch, and the soft-drop counter's accumulation.
6. **Lock and top-out** — the stage guard, the four board writes, the stage-and-hide, the two-lock top-
   out law, and the Type-A/other award fork with saturation.
7. **Next piece** — promotion and pipeline writes, the deterministic ring walk with index wrap and the
   staged-garbage mark, and the random path's OR-rejection, third-draw auto-accept, and next-preview-only
   write.

**Known gap:** none of the above compares against a capture from real hardware or a reference emulator.
The gap is inherent to porting without the original runtime available; it is recorded here and revisited
if a hardware trace becomes available.

---

## Tested by

`tests/test_piece_system.cpp` — seven behavioral cases: `PieceCellGeometry`, `DetectCollisionVerdicts`,
`RotateVectors`, `ShiftDasVectors`, `DropGravityAndSoftDrop`, `LockAndTopout`, `NextPieceVectors`. All
device-free; every asserted value traced to the lines named above.
