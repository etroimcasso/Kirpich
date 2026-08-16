# Piece system

**Date:** 2026-08-15
**Status:** Delivered — the six piece-manipulation routines (spawn, drop, rotate/shift, collide, lock)
as free functions, plus the audio cue mailbox they are the first writer of. No state handler is
installed yet; the gameplay and line-clear handlers compose these when they land.

## Concept

The active falling piece is manipulated once per frame by a small family of routines: spawn the next
piece, drop it by gravity or the faster soft drop, rotate and shift it under player input, test it
against the board, and lock its tiles into the board when it comes to rest. This feature ports those
six routines as native free functions. The behavioral authority is
[`../contracts/piece-system.md`](../contracts/piece-system.md).

## Design decisions

**Free functions, not a class.** The piece system owns no state — the active/preview slots, the drop
and lock counters, the score, and the audio cues all live on the already-ported state structs. So the
functions take the game context by reference and there is nothing to construct. (The input layer is a
class only because it owns the previous-held byte; the piece system has no equivalent.)

**Collision and locking compute the piece's cells directly.** The original renders the piece into the
sprite-object buffer and reads that buffer back to find which board cells the piece covers. There is no
renderer yet, and the port does not need one here: `activePieceCells` computes the four cells straight
from the active slot's position and its composed sprite. The two consumers — collision and locking —
share that one helper.

- **The geometry reproduces the original's renderer arithmetic exactly.** The renderer computes each
  part's on-screen position with an 8-bit `add`/`adc` pair, so the carry out of the first add leaks
  into the second; the piece render offsets are negative, so that carry fires on every piece. The port
  carries the same 8-bit adds with the carry threaded across, and the tile-lookup cell map that turns a
  pixel into a board cell. A hidden piece maps to an off-screen row exactly as the renderer's hidden
  branch does. These are contract-pinned laws, not incidental details — collision correctness depends on
  them.

**The audio cue mailbox is a game-state member.** The piece routines cue sounds (rotate, shift,
game-over) and request an audio re-init at top-out. Rather than call a sound driver that does not exist
yet, they write into an `AudioCues` mailbox on the game context — the game's half of the game-to-driver
interface, matching the four cue bytes the original leaves for its driver. A cue can be written and then
overwritten to none within the same frame (a rotation that turns out to collide cues the rotate sound,
then cancels it), so the mailbox holds the last write of the frame. The audio tick that drains it lands
with the sound system.

- **Rejected — cueing through a driver call.** The sound driver is not ported, and the mailbox is what
  the original itself uses (the game writes a byte; the driver reads it next frame). Modelling the
  mailbox keeps the write-then-cancel semantics the rotation and shift depend on.

**The draw source is a callable parameter.** `nextPiece`'s random path draws candidate pieces from the
randomizer. The randomizer's draw core runs on the virtual machine, but passing the draw as a
`std::function` lets the tests substitute a scripted source and stay device-free — the piece system is
tested without constructing a VM.

**One order where the original has two.** The original's shift stage writes its sound effect and renders
in a different order on the right and left paths; both land before the audio tick, so the port uses one
order. Observably equivalent — recorded in the contract.

## Implementation details

Files:

- `src/systems/piece.h` / `.cpp` — `kirpich::systems`:
  - `PieceCell { row, col, tile }` and `activePieceCells(const GameContext&)` — the four board cells the
    active piece covers.
  - `detectCollision(const GameContext&)` — whether any covered cell is non-empty.
  - `rotateAndShiftPiece(GameContext&)` — rotation then shift-with-auto-repeat.
  - `dropPiece(GameContext&)` — gravity / soft drop, the lock start, the soft-drop award, and top-out.
  - `lockPieceIntoBackground(GameContext&)` — writes the resting tiles into the board.
  - `nextPiece(GameContext&, const std::function<std::uint8_t()>& draw)` — spawn and choose the preview.
- `src/systems/audio_cues.h` — `AudioCues` (the cue mailbox), added as a `GameContext` member.
- `tests/test_piece_system.cpp` — seven behavioral cases covering the cell geometry, collision, rotation,
  the shift DAS timeline, the drop gates and soft-drop counter, lock and top-out, and the next-piece
  paths.

Wiring: `src/CMakeLists.txt` adds `systems/piece.cpp`. `GameContext` gains an `AudioCues audioCues`
member. No new action enumerators, no dispatcher change, no data or state change.

The routines ship as testable functions; no state handler installs them yet. The gameplay handlers
(Type A / Type B) and the line-clear flow compose them when they land, and the audio tick drains the cue
mailbox when the sound system lands.

## Open questions / future work

- **The handlers that call these.** The Type-A and Type-B gameplay states and the line-clear flow drive
  the piece each frame; they compose `dropPiece`, `rotateAndShiftPiece`, `nextPiece`, and the collision /
  lock pair. Recorded in the contract.
- **The audio tick.** The mailbox accumulates cues; the tick that drains it into the sound driver each
  frame lands with the audio system.
- **The lock stage machine.** The drop begins the lock (stage 1) and `lockPieceIntoBackground` advances
  it (stage 2); the full stage sequence and the stage-3 gate belong to the line-clear flow, which
  finishes the lock.
- **No hardware trace.** Verification substitutes the hand-traced cell geometry, the collision and
  rotation vectors, the DAS timeline, the drop and lock behavior, and the next-piece paths for a
  side-by-side trace against the original, which is not possible before the main loop exists. Recorded in
  the contract (§10).
