# Demo playback

**Date:** 2026-08-19
**Status:** Complete

The attract-mode demos: the two recorded rounds the game plays to itself when the title screen is left
alone.

## Concept

Leave the title screen alone and it counts down, then plays a round by itself — Type A first, Type B
second, alternating from then on. Press Start and it stops.

The demos are not video. The game plays them by running itself: the same round init, the same frame,
the same piece and line-clear logic a player drives, with recorded button presses fed in instead of
the player's. That is why a demo can top out, clear lines, and score exactly as a round does — it *is*
a round.

## Design decisions

### The recordings are input, not frames

Each of the two recordings is a run-length-encoded list of steps: a set of buttons and how many frames
they are held for. Playback counts frames off and only advances when the input changes. This is the
original's format and the port keeps it — the data unit composes the two timelines from the ROM bytes,
and this unit walks them.

Because the recordings are input rather than outcomes, they only reproduce the same round if
everything downstream is deterministic. It is: while a demo runs, the piece pipeline reads a fixed
shared list instead of the randomizer.

### The demo's presses are derived against the recording, not the player

The game derives "newly pressed" the same way for a demo as for a player — held now and not held
before — but the *before* is the recording's own previous step, not the previous frame's real buttons.

This distinction is not cosmetic. Taken against the player's buttons, a demo would report presses its
recording never made, and would corrupt the live input path's history for the following frame. The
port derives the demo's edge itself and leaves the live input path alone; the header comment on the
input layer that used to suggest otherwise was corrected when this unit landed.

### The player keeps the buttons

The substitution lasts one frame beat. The player's held buttons are parked when the demo takes over
and put back before the frame ends, which is what lets Start end a demo and lets the soft-reset chord
work while one plays. The end check deliberately runs *before* the substitution so the Start it sees
is the player's own.

### The recording path ships dead

The original carries a routine that writes recordings, called every gameplay frame, plus the routine
that arms it. Nothing calls the second one — it appears exactly once in the whole disassembly, its own
label — so the flag it sets is never set and the recorder never runs.

It is ported anyway, dead as it is in the original. **Rejected:** dropping it as unreachable. The
routine is reachable code sitting behind a flag, not unreachable code; the game calls it every frame
and the port should too. What *is* dropped is its two stores, which write through a cursor pointing at
cartridge ROM and therefore land nowhere on hardware — dropping them is equivalence, not omission.

A separate dead tail inside the replay routine *is* dropped: no branch targets it at all, and the
upstream itself marks it unused and unreachable.

### One frame of the config screen

Launching a demo loads the config screen's backdrop and then immediately enters the round init, which
overwrites it on the next frame. That single frame is visible on hardware, so it is reproduced rather
than optimized away.

## Implementation details

**Files:**

- `src/systems/demo.{h,cpp}` — launch, replay, restore, the two terminals, and the dead recording pair.
- `tests/test_demo_playback.cpp` — 9 behavioral cases.
- `docs/contracts/demo-playback.md` — the behavioral contract, with source line anchors.

**Wiring:** the four per-frame routines are handed to the gameplay installer as a set; the launch is
handed to the title-screen installer, whose attract countdown fires it. Both are bound in `main()`.

**Consumed, unchanged:** the two timelines and the shared piece list from the demo data; the demo state
block; the gameplay frame's four seams and the title screen's launch seam, all of which had been in
place and inert since those units landed.

**Values:** demos run at level 9. The Type B demo starts at height 2 with its piece count at 17. The
Type A recording ends as the piece count reaches 16, the Type B one at 29 — an equality test, not a
threshold, so a count that steps past its target does not end the demo.

## Open questions / future work

- **The two-player attract path.** The title screen's link-cable poll can also leave the title screen;
  that path belongs to the serial system and is not ported yet. It does not affect demo playback on a
  single machine.
