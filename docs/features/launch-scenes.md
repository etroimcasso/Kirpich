# Launch scenes

**Date:** 2026-08-20
**Status:** Complete

## Concept

The two bonus endings the game keeps behind its hardest achievements. Win a Type B round that was
started at garbage height 5 and the Buran shuttle launches; score 100 000 points in a Type A game and
a rocket does — one of three, sized by how far past 100 000 the score went.

Both are pure spectacle. Nothing in either sequence reads the joypad, so they cannot be skipped or
hurried, and nothing in them changes a score, a level, or a line count. Each is a fixed run of timed
steps: build a launch pad, hold, reveal the smoke, ignite, fly the vehicle up off the top of the
screen, and hand back to a screen that already exists.

They are the last two dead-ends in the single-player flow. Both entry states were already being
written — the ending dance forks to the Buran at height 5, and the game-over chain writes the rocket's
entry state at 100 000 points — but neither target had a handler, so a player who earned either one
reached a state that did nothing and the game stopped.

## Design decisions

**One unit covering both chains, not two.** The chains are structurally parallel and share both
helpers — the launch pad and the hold flicker. Splitting them would have meant one half shipping a
helper the other half consumes.

**The pad is built from two loops with different shapes, and the original's names for them mislead.**
One writes a two-dimensional block (rows of twenty cells); the other writes a vertical strip, despite
having "Row" in its name. The tower art is stored one-dimensionally, which agrees with the second.
Both stamps are local to this feature rather than added to the shared screen-loading surface: the
shared stamp is fixed at a full 18×20 screen from the top-left corner and cannot express a four-row
block starting at row 14 or a seven-cell column. If the link-cable screens later want the same shapes,
promoting them is that work's call.

**Everything draws on the second background map.** The hardware keeps two and displays one; these
scenes switch to the second on the way in and back on the way out. That means a playing field left
underneath survives the whole sequence untouched. The second map had been introduced for the paused
screen, and this is its third use.

**The clear is to spaces, not to zero.** The routine that blanks the second map fills it with the
space glyph. Clearing to zero instead is invisible to a headless test and wrong on screen.

**The climbs run their coordinate past zero, and that is the mechanism.** Both flights decrement an
eight-bit screen coordinate and test it for equality against a value *above* where they started, so
the coordinate wraps through zero on the way — 136 steps for the shuttle, 138 for the rocket. Because
the tests are equality rather than a threshold, an implementation that saturates at zero never reaches
its terminal and the scene runs forever. It is pinned by a test that counts the steps and asserts the
coordinate passes through zero.

**Three asymmetries between the chains are preserved rather than regularised.** The rocket's pad has
no left tower and no umbilicals; its ignition sets no smoke art where the shuttle's does; and it has no
congratulations screen, so it forks straight to its exit. Its exit also re-initialises the sound driver
where the shuttle's exit does not, and the last handler of the rocket chain has no timer gate at all —
alone among the fifteen, it runs on its first frame. Each is asserted directly, so a later tidying pass
that "fixes" one fails a test that says it is deliberate.

**The congratulations cursor is carried as a column.** Printing the sixteen-letter message is spread
over sixteen timed steps and nothing else records how far it has got, so the cursor has to survive the
frame. The original stores a full destination address across two bytes; the port carries the low half
only, as a column number, because the high half is constant for the whole sequence and which map is
being drawn is already known. That byte has a second, unrelated role in the top-score name-entry
screen, which recomputes its own cursor rather than storing one — the two screens cannot run at the
same time, so one byte serves both. This is the third such split the port carries.

**Rejected: guarding the print cursor by clamping.** The original indexes its message table with no
check, so an unseeded cursor there reads a neighbouring byte — meaningless but harmless. A port has no
neighbouring byte to read and would run off the end of the table, which is undefined behaviour rather
than a harmless garbage read. The handler returns early instead when the cursor is outside the
message. The case is unreachable in play (the state is entered only from the flight, which always
seeds it), and the guard is pinned by a test so it does not silently become dead code.

## Implementation details

Fifteen state handlers and two shared helpers in `src/systems/launch_scenes.{h,cpp}`, installed into
the frame dispatcher by `installLaunchSceneHandlers`. The installer takes no seam — unlike the ending
dance, nothing here needs to ask the sound system anything.

Everything the scenes draw with was already on the shelf: the backdrop, the four tower strips and the
congratulations message are stored tilemaps; the launch objects, every sprite id, the launch music and
both sound effects, and the score-to-rocket-tier mapping all shipped with earlier work. This feature
added no data.

Two additions to existing state:

- A third tile set, the multiplayer-and-Buran art, which both launch pads load.
- One byte on the game-flow state for the congratulations print cursor.

The per-state effects, the pad geometry with its addresses converted to rows and columns, the climb
law, and the asymmetries are specified with source line anchors in
[`../contracts/launch-scenes.md`](../contracts/launch-scenes.md). What to edit to change a timing, a
placement, or the message is in [`../engine/launch-scenes.md`](../engine/launch-scenes.md).

## Open questions / future work

- The link-cable screens load the same tile set these do and will select it through the same
  enumerator; the two stamp shapes here are the ones those screens are likely to want.
