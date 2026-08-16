# Line-clear logic

**Date:** 2026-08-15
**Status:** Delivered — the post-lock line-clear sequence (scan, flash, compaction, and the row-by-row
field wipe) as free functions. No state handler is installed yet; the gameplay handlers and the frame's
vertical-blank tick compose these when they land.

## Concept

Once a piece locks, a sequence runs to completion over many frames: find and tally the completed rows,
flash them, drop the stack down into the gaps, then redraw the field one row per frame until the next
piece spawns or a Type B round ends. This feature ports the sim side of that sequence as native free
functions. The behavioral authority is [`../contracts/line-clear.md`](../contracts/line-clear.md).

This closes the gameplay loop the piece system opened: a piece locks, the rows are found and tallied,
the flash runs, the stack compacts, the field redraws bottom-up, and the terminal redraw spawns the
next piece — without the wipe stepping, the loop would deadlock after any clear (gravity and soft drop
both gate on the wipe counter, and nothing else returns it to zero).

## Design decisions

**Free functions, not a class.** Like the piece system, the line-clear pipeline owns no state — the
lock stage, the flash phase, the wipe counter, the completed-rows list, the board, and the audio cues
all live on the already-ported state structs. The functions take the game context by reference and
there is nothing to construct.

**The pipeline spans two frame beats, and the port keeps that split.** In the original, the scan and
the compaction run with the gameplay handlers, while the flash and the wipe run in the vertical-blank
handler, after the frame's timer decrement. That split is what makes the flash's 10-frame cadence and
the one-row-per-frame wipe exact. The port ports the functions in that shape and defers the wiring: the
handler-side functions are called by the gameplay handlers, the vertical-blank-side functions by the
frame tick, both later. The tests reproduce the frame anatomy with a small harness so the cadence
assertions are frame-exact.

**Eighteen near-identical wipe routines collapse to one stepper.** The original redraws the field with
eighteen dispatchers, each gating on one exact counter value, called in descending order every
vertical blank — which yields exactly one row-step per frame. The port collapses them into one
range-gated step (`playingFieldWipeTick`) that advances the counter by one per call. The collapse is
exact: the counter increments inside the row copy and the calls run descending, so an increment can
never cascade into a second row-step in the same frame. The particular steps that carry a side effect
(the stack-fall sound at step 8, the terminal at step 19) keep their behavior; the score/level redraw
steps (16–18) are render and are recorded seams the scoring work fills.

**The row redraw is render; only the counter and the cues are sim.** Both the flash pixels and the
wipe's row copies are writes to video memory. A renderer re-derives them from the flash phase, the
completed-rows list, and the board, so the port drops them here and carries only the state effects (the
counter advance, the audio cues, the piece spawn, the round-end transitions).

**The completed-rows list stores field-row indices.** The original stores WRAM row addresses in a
nine-byte list terminated by a zero word; the port stores field-row indices in the existing bounded
list on `EngineState`, whose length replaces the terminator. The list is rebuilt each scan through the
list's constructor rather than mutated in place.

**No new state, no new `GameContext` member.** Every byte the routines touch resolves to an
already-ported state field or to an adjudicated mechanism byte; the audio cue mailbox shipped with the
piece system. Nothing is minted here.

**The Type B daa-artifact guard is preserved dead.** The Type B count-down carries a decimal-adjust
guard (a borrow that lands the tens digit on 9 zeroes the count) that is unreachable for legal Type B
states — the original's own annotated `TODO`. It is ported verbatim and recorded in the contract as
preserved-dead, and pinned by a test on an out-of-range state.

## Implementation details

Files:

- `src/systems/line_clear.h` / `.cpp` — `kirpich::systems`:
  - `checkForCompletedRows(GameContext&)` — the stage-2 scan, the line-count update (Type A clamp /
    Type B count-down), the per-kind stat/garbage/SFX tally.
  - `animateLineClear(GameContext&, const std::function<std::uint8_t()>& draw)` — the flash cadence
    (sim only) and the no-clear spawn shortcut.
  - `moveBlocksDownAfterLineClear(GameContext&)` — the stack compaction and the top-row duplicate quirk.
  - `clearLineClearsList(GameContext&)` — empty the completed-rows list.
  - `playingFieldWipeTick(GameContext&, const std::function<std::uint8_t()>& draw)` — one field-wipe
    step: the counter advance, the step-8 cue, the terminal spawn / round-end.
- `tests/test_line_clear.cpp` — seven behavioral cases covering the scan, the tally, the flash cadence,
  the no-clear spawn, the compaction and its quirk, the wipe stepper, and the terminal branches, driven
  through a frame harness.

Wiring: `src/CMakeLists.txt` adds `systems/line_clear.cpp`. No new action enumerators, no dispatcher
change, no data or state change.

The routines ship as testable functions; no state handler installs them yet. The gameplay handlers
(Type A / Type B) call the scan and compaction, and the frame's vertical-blank tick calls the flash and
wipe, when they land.

## Open questions / future work

- **The handlers and the frame tick that call these.** The Type-A and Type-B gameplay states drive the
  scan and compaction each frame; the vertical-blank tick drives the flash and wipe. Recorded in the
  contract (§2).
- **The level-up seam.** Wipe step 16 carries the Type A level-up check as a recorded no-op seam; the
  scoring work fills it (the BCD lines-vs-level compare, the level increment, the gravity reload, the
  level-up sound).
- **The render bridge.** The flash pixels and the wipe's row copies are render effects the pipeline
  drops; the renderer re-derives them from the flash phase, the completed-rows list, and the board.
- **No hardware trace.** Verification substitutes the frame-harness cadence and the scan/tally/
  compaction/wipe/terminal vectors for a side-by-side trace against the original, which is not possible
  before the frame loop exists. Recorded in the contract (§11).
