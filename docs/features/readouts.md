# Number readouts

**Date:** 2026-08-19
**Status:** Complete

## Concept

The stats panel beside the playing field shows numbers: a score, a level, a line count, and — in
Type B — the height of the starting garbage under the label `HIGH`. All four values were live in the
simulation for several units before anything drew them, so a round ran correctly with a blank panel.
This is the unit that draws them.

It also mints the second background map, because the readouts cannot be ported without it.

## Design decisions

**One printer, reproducing the digit string rather than the byte walk.** The original stores the score
and line count as packed decimal — two digits per byte — and its printer walks a fixed number of those
bytes, most significant first, writing map cells as it goes. The port carries both values as ordinary
integers, so there are no packed bytes to walk. `printNumber` takes a value and a count of digit pairs
and produces the string the original's walk would have produced: leading zeros drawn as spaces, every
digit drawn once the number has started, the final digit always drawn, and digits above the printed
width dropped exactly as the original never reads them.

Simulating the byte walk was rejected: it would have meant carrying a packed-decimal shadow of two
values that the rest of the port has treated as integers since the data layer, for no observable
difference.

**The print flag is one field, not two.** The original's `$FFE0` carries two roles. Outside a print it
means "the score has changed", set by the routine that adds to the score and read as the gate on
whether the score is drawn at all. Inside a print it is the printer's own "a nonzero digit has been
drawn" flag, cleared on entry and again on exit.

The second role destroys the first: any print clears the flag, so the next score draw is suppressed.
Every site that draws the score into both maps sets the flag again between its two calls to compensate.
Splitting the roles into two fields was rejected — it would remove exactly the interference those sites
exist to work around, and the second map would silently stop updating. One field, both roles
documented, and a test that fails if the compensating set is removed.

**The second map is real state, not an approximation.** The hardware keeps two background maps and
displays one at a time; the game uses the second as its paused screen. Earlier units recorded this as
a known difference and drew one map, which left pausing as a change to the simulation with no change to
the picture.

Drawing only the live map here was rejected. Every score site writes both, and the pause handler exists
partly to copy the line count across, so half of each routine would have had to be dropped and re-added
later. `DisplayState` gains a second grid and a selector naming which is displayed; the render bridge
reads the selected one. The same map serves the link-cable round init and the launch scenes, so the
shape is now settled for those too.

**The level cell is derived, not carried.** The original keeps a byte holding the map address the level
digit goes to, written once at round init from the game type and read twice. It is a pure function of
the game type, so the port derives it at each site rather than carrying a field.

## Implementation details

`src/systems/readouts.{h,cpp}` — the printer and eight drawing functions, all free functions in
`kirpich::systems`. `DisplayState` gains `secondMap`, `displayed` and `displayedMap()`;
`GameFlowState` gains `scorePrintFlag`.

Wired at seven sites: the frame's last beat in `src/main.cpp`, field-wipe steps 17, 18 and 19 in
`line_clear.cpp`, the level step in `scoring.cpp`, and the round init and pause handler in
`gameplay.cpp`.

**A defect found while wiring it.** The original sets the print flag inside the routine that adds to
the score. The port has no such routine — the score is an integer and each addition is a plain sum — so
nothing set the flag, and the score would never have been drawn at all. The four additions that
correspond to the original's four score-affecting calls now each set it. A test drives an award through
to a drawn score without touching the flag by hand, which is the coverage whose absence let this
through.

## Open questions / future work

- The Type A line count on the paused screen is stale until the moment of pausing: the redraw writes
  the live map alone, and the second map's copy arrives only when the pause handler runs. This is the
  original's behavior, and its own source comments it as a suspected bug. Ported as written.
- The link-cable round init and the launch scenes write the second map too. Both are unported; the map
  they need now exists.
- The top-scores screen is a screen of its own rather than a panel readout, and lands with the
  high-score recording.
