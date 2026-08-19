# High-score recording

**Status:** Complete

## Concept

A finished round's score is compared against the three best scores stored for the difficulty it was
played at, and if it beat one, it takes that rank and the player is asked to spell a name for it. The
three ranked entries are shown on the difficulty screen the player returns to, so the table is both
the record and the screen furniture.

Type A keeps three scores per starting level. Type B keeps three per starting level *and* starting
garbage height, because a round at height 5 is a different game from one at height 0. Names are six
glyphs, entered one column at a time on a letter wheel.

## What happens, in order

1. A round ends and the game returns to the difficulty screen for the type just played.
2. That screen refreshes its table: it clears the three display rows, compares the round's score
   against the stored three, inserts it if it beat one, and re-stages all three rows.
3. An insert also seeds a blank name, records which rank was taken, cues the top-score music, and
   raises a flag. The difficulty screen reads that flag and enters name entry instead of level
   selection.
4. Name entry blinks a cursor over the six name cells. Up and down turn a letter wheel; A settles the
   glyph and steps right; B steps left; Start submits from wherever the cursor is, and so does
   stepping right off the last column.
5. Submitting restores the menu music, writes the table to disk, and returns to the level picker.

## Design decisions

**A tie does not displace.** A score has to be strictly greater than a stored one to take its rank.
The original compares packed-decimal pairs and branches only on a borrow, so an equal score falls
through to the next rank; ties against every reachable rank leave the table alone.

**Leading zeros are skipped, not blanked.** The digit printer steps over a score's leading zeros
without writing them, so the empty-glyph fill underneath shows through and a score of zero prints
nothing at all. The disassembly marks this a bug. It is what the screen shows, so the port does it.

**The rank is stored inverted.** Beating the best score records a 3, second records a 2, third a 1 —
the original's compare counter, kept as it is rather than normalised to an index. Everything that
needs an index or a screen row derives it: the table index is `3 − rank` and the display row is
`16 − rank`, so rank 3 is the topmost row.

**The name cursor is derived, not stored.** The original keeps a pointer to the name cell being
edited across frames. That pointer is fully determined by the game type, the chosen level and height,
and the rank, so the port recomputes it each frame and carries no pointer field.

**Two grids, written differently.** The three staged rows are written into the board — the game's own
copy of the background — and carried into the displayed map when a redraw is requested. The two-cell
gap between each name and its score is stepped over rather than copied, so the screen's own backdrop
survives there. Name entry is the exception: it writes the cursor glyph straight into the map, and
the board keeps whatever the staged row last held for that cell.

**Heart mode reaches into the wheel.** Normally the wheel runs `a` through `z`, then `.`, `-`, `×`,
then a space, and wraps. Heart mode swaps the `×` for a `♥`, which extends the ring by one and makes
a heart typeable in a name. This is the last of heart mode's three effects.

**Scores persist across launches, and are written on submit.** The original has no persistence at all
— a power cycle clears every score, and only a soft reset preserves them. The port writes the tables
to a save document, which is an addition rather than a change to how the tables behave in play. The
write happens when a name is submitted and at no other point: the table is mutated at insert, before
a name exists, so saving there would leave a half-named entry on disk with no way to finish it.
Quitting during name entry loses that one score.

## Implementation details

`src/systems/high_scores.{h,cpp}`.

| Routine | What it does |
|---|---|
| `clearTopScoreFields` | fills the three display rows with the empty glyph |
| `updateTypeATopScores` / `updateTypeBTopScores` | pick the slice for the current difficulty, compare, insert, stage, request a redraw |
| `drawTopScoresToVram` | carries the staged names and scores into the displayed map |
| `enterTopScore` | the name-entry screen, one frame at a time |
| `installHighScoreHandlers` | binds the name-entry state and the save seam |

The two refresh routines are bound to the five difficulty-screen handlers that take a refresh seam;
`drawTopScoresToVram` runs from the frame's last beat, where the original runs it. Saving goes
through a seam so this layer holds no engine save types.

Layout: three rows from row 13 — a six-glyph name from column 4, a two-cell gap, a six-digit score
from column 12. The blink interval is seven frames; the wheel's key repeat is the same
twenty-three-then-nine timeline the piece shift uses.

The full behaviour, with source line anchors, is in `docs/contracts/high-score-state.md`.

## Open questions / future work

The link-cable game keeps no top scores of its own, so nothing here is reached in a two-player round.
