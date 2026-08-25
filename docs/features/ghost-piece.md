# Ghost piece

**Date:** 2026-08-22
**Status:** Complete

## Concept

A shadow of the falling piece, drawn on the row it would land on if dropped. It is help the original
game does not give — the Game Boy release has nothing like it — so it is off until a player turns it
on, and a build with it off draws exactly the frames a build without it draws.

It is presentation only. No game state records it, no simulation step consults it, and the round
plays identically whether it is on or off. The shadow is derived fresh every frame from state the
game already keeps.

## Design decisions

### The shape comes from the piece, not from a description of the piece

The falling piece is already drawn: four entries in the object buffer, composed into four placed
sprites every frame. The engine can hand a sprite back its own image as geometry — a polygon traced
from the sprite's coverage — and that polygon can drive a region carrying a colour with no art behind
it. So the shadow asks each of the piece's own sprites for its shape, and draws that.

Asked in the **layer's** space, the shape comes back carrying the sprite's flips, its rotation, its
transform and its placement. That is the whole reason for taking this route rather than computing
four rectangles from the piece's cell coordinates: the two would agree today, and would stop agreeing
the moment anything about how a piece is drawn changed. The shadow cannot disagree with the piece
about what shape a piece is, because it does not hold an opinion — it asks.

The sprites it asks are built through the same function that builds the frame's own
(`oamEntrySprite`, `src/render/sprites.h`), so there is no second placement calculation that could
drift from the first.

**Rejected: four cell-sized rectangles derived from `activePieceCells`.** Simpler, and identical on
today's art. It puts the burden of matching the piece's placement on this feature, permanently, for
no benefit.

### The shadow is drawn on the background layer, and that is not enough on its own

Two things were wrong with drawing it over the finished frame, and only one of them is about depth.

A region attached to the frame grades the composited picture — everything, the piece included. So the
shadow tinted the piece as soon as the piece came down far enough to overlap it. Moving the regions
onto the background layer fixes the ordering: the shadow grades the board, and the objects composite
over it afterwards.

That alone did not fix it, because **a piece block is see-through in its middle.** An object's
lightest colour is transparency rather than a shade (`docs/engine/rendering.md`), so the interior of
a block is a hole. A shadow underneath one shows *through* the block and tints it however far below
it is drawn. Depth decides what draws in front of what; it does not decide what shows through.

So the shadow also has to not be drawn where the piece is. That is the rule below.

### The shadow is withdrawn whole when the piece reaches it

Two ways to keep the shadow and the piece out of the same cell, both tried:

1. **Per-cell.** Omit only the cells the piece is standing on. The piece takes the shadow's place a
   block at a time as it descends.
2. **All-or-nothing.** The moment any of the four shadow cells would coincide with any cell the piece
   occupies, withdraw the entire shadow.

Both were built and looked at on a running build. The first is what an upright I-piece descending
into its own shadow makes worst: the shadow comes apart one block at a time over the last four rows,
which reads as a fault rather than as a decision. The second is what ships. The shadow leaves as one
thing, and by the time it does, the piece is within its own height of resting — where a player can
see where it is going without being told.

This also covers a piece already at rest, whose every landing cell is a cell it is standing on.

### The landing row is the lock's own answer

The walk steps the piece down a row at a time and asks whether it would overlap the board, using the
same emptiness test the lock uses (`src/systems/piece.h`). The shadow can therefore never sit
somewhere the piece could not come to rest.

The row steps **around** the board rather than off the end of it, because that is what the arithmetic
the row came from does. A cell's board row is derived as an eight-bit subtraction shifted right three,
so a cell above the top of the playing field — which is where three quarters of an upright piece sits
at spawn — comes out as row 29, 30 or 31 rather than as anything negative. Stepping such a row down
is stepping it around the same thirty-two, and the wrap tracks the piece's real position exactly.

Reading the wrap as the bottom of the board instead was a defect found by playing: an upright piece
cast no shadow at all until it had fallen clear of the top of the field, because its three high cells
read as having already landed and the piece was reported as having nowhere to fall.

For the same reason the shadow takes the shapes of the piece's parts that are **off the top of the
screen** as well as the ones on it. They draw nothing where they are; they are inside the field at
the row the piece lands on. Leaving them out drew a shadow missing its top cells.

## Implementation details

`src/render/ghost_piece.{h,cpp}`:

| Call | Answers |
|---|---|
| `ghostDropRows(game)` | How many rows the piece would fall before coming to rest |
| `ghostVisible(game)` | Whether a shadow belongs on screen this frame |
| `ghostShadowCells(game)` | The board cells the shadow occupies — four of them, or none |
| `ghostPieceRegions(game, atlas, tick, ramp)` | The regions themselves |

The shadow is drawn in the **darkest colour of the ramp in effect**, at `kGhostAlpha` (0.4), so it
recolours with the rest of the game when a player changes palette. The silhouette trace is given a
budget of `kGhostShapeVertices` (16) per part.

`ghostVisible` requires all of: a round in progress, the playing field on screen rather than the
paused screen, a piece that is not hidden, and a shadow clear of the piece.

Wired in `src/main.cpp`'s render loop, onto the background layer, behind the player's setting.

### The setting

`Settings::ghostPiece`, off by default. Its row is the first of the enhancements page (the settings
screen's second) and opens a screen of its own: the switch sits there beside a description of what
the shadow is (`docs/features/fixes-screen.md` describes the carousel machinery the screen runs on).

Adding it grew the settings save document from three bytes to four, which **bumped its schema version
from 1 to 2** with a registered `1 → 2` migration that appends the flag as off. Documents written by
0.9.0 and 0.9.1 are on players' disks and are migrated on the way in rather than read short: a build
that changed what it wrote without changing what it called it would leave two different formats
answering to one version.

The store carries **one** current version and **one** migration chain for every document in it, and
the settings share a store with the top scores. Each loader naming its own version immediately before
its own read is what keeps them apart — see `docs/engine/settings.md`.

## Open questions / future work

- **Outline instead of filled.** The shape a region carries supports a stroked band along its
  boundary as readily as a fill, so an outline ghost is a small change. It was not built: an outline
  drawn per part would draw seams through the middle of the tetromino, and a single outline of the
  whole piece needs one polygon over the four cells rather than four polygons.
- **The regions themselves are not covered by the test suite.** A sprite resolves its coverage against
  the uploaded sheet through the renderer, which needs a graphics device no test job has. The landing
  walk, the visibility gate and the withdrawal rule are all tested; what the shadow looks like is owed
  by hand on a running build.
