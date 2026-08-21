# Boot path and init quirks

**Date:** 2026-08-20
**Status:** Delivered — the cold boot, the soft reset the four-button chord runs, and the startup
ordering that keeps a player's saved scores. The behavioral authority is
[`../contracts/boot.md`](../contracts/boot.md).

## Concept

The original has one startup routine with two entry points. Power-on enters at the top; the
Start + Select + B + A chord enters four instructions in, at a label that skips the first of six clear
loops. That skipped loop covers the work-RAM bank the top-score tables live in, which is the entire
reason a player's scores survive a reset and do not survive a power cycle. Everything below the label
is shared: five more clears, the sound driver's startup, a fill of the first background map, and three
byte writes that leave the machine pointed at the copyright screen with a game type and a music
selection already chosen.

Two things in the port were waiting on this. The chord was detected in both of the places the original
detects it and fired a seam nobody had assigned, so holding all four buttons did nothing. And the boot
itself was a substitution: three values written inline at startup, with a comment saying so.

## Design decisions

**`softReset` is `coldBoot` plus a save and restore, not a second sequence.** The original's two entry
points are one routine, so the two paths cannot drift apart by construction. Writing the reset as its
own list of steps would let them.

**The two tables are saved by member, never by structure.** `HighScoreState` holds the two top-score
tables *and* four bytes the score-entry flow uses. The tables live in the work-RAM bank a reset skips;
the four bytes live in high memory, inside a clear a reset runs. So a reset keeps the tables and
clears the four bytes, and an implementation that preserved or reset the whole structure would be
wrong in one direction or the other. `SoftResetPreservesTablesAndResetsItsHramBytes` asserts both
halves in one case so neither can pass alone.

**`bootGame` exists to hold an ordering.** The original has nowhere to keep a score between sessions;
this port writes its tables to the player's save file. So a launch has to clear and then load — the
reverse would wipe the scores it had just read, on every launch, invisibly. Two statements at the call
site would be a correctness property that nothing tests. Wrapping them in a named function moves it
somewhere a test can hold it, and `BootOrderKeepsSavedScores` does.

**Both chord seams get the same closure.** The frame dispatcher and the gameplay frame each test the
chord, matching the original, which reaches one reset routine from both sites. Assigning one closure
to both keeps them the same reset rather than two that might diverge.

**The dispatcher's input mechanism resets with the machine.** The original's clear covers the held- and
pressed-button bytes, so the first frame after a reset derives its presses against nothing and every
button still down reads as newly pressed. That is also what makes a chord held down keep resetting
until it is released. `GameStateDispatcher::reset` returns exactly that mechanism to boot, so the
closure calls it alongside `softReset`.

**A matched chord ends the gameplay frame.** The original reaches its reset with a jump, not a call, so
the remainder of the frame — the recorded-input substitution, the piece, the scan, the lock, the
compaction, the award — never runs. `handleStartSelect` returns whether the frame continues, and
`normalGameplay` stops on false. Without it, a reset mid-round would step a piece across the board the
reset had just cleared. Marking the return `[[nodiscard]]` was what surfaced every call site.

**The two-byte over-copy is carried as an equivalence, with a proof.** The routine copy into high
memory writes two bytes more than the routine is long, and they land on the two selection bytes. The
startup overwrites both a few instructions later, through two calls that return and no branch — so
nothing can read the stray bytes before they are replaced. The port models no stray bytes and asserts
the outcome instead, deriving the landing addresses from the shipped layout so a change to it breaks
the test rather than the claim.

**The hardware writes produce no code.** Roughly a third of the routine configures the original's
display, interrupt, stack and timer registers. This port draws through a display the engine owns and
takes its frame from the engine's run loop, so there is nothing for those writes to reach. Rather than
leave that as an absence a reader has to infer, the contract carries a line-by-line accounting of the
whole routine — every range, and what became of it.

### Considered and rejected

**Giving the sound system a synchronous re-initialisation call.** The original calls the driver's
initialisation inline, part way through the reset; the port sets the request the frame's sound step
consumes, so it happens a frame later. Adding a direct call would match the original's instant more
closely and buy nothing: no state handler runs between the two points, and the sound step performs the
re-initialisation before it reads the frame's cues, so no cue can be lost to the gap. The request
mailbox is also how every other part of the game asks for the same thing.

**Preventing the reset from running twice in one frame.** From the gameplay path the reset fires, the
frame ends, and the frame-level check then matches the same still-held chord and fires it again. The
original cannot do this — its jump leaves the loop entirely. The two are indistinguishable, because a
reset applied to an already-reset machine restores the same two tables and produces the same state, so
threading a flag through the frame would add a mechanism for no observable gain.

**Re-reading the save file on a soft reset.** The original keeps its tables in memory across a reset,
and so does the port. Reading the file again would be a difference, not a fidelity gain.

## Implementation details

`src/systems/boot.{h,cpp}` — `coldBoot`, `softReset`, `bootGame`, and a file-local fill of the first
background map. One line in `src/CMakeLists.txt`.

`src/main.cpp` calls `bootGame` once and assigns one reset closure to both seams. The three values the
following screens read come from `coldBoot`.

`src/systems/gameplay.{h,cpp}` — `handleStartSelect` returns `[[nodiscard]] bool`; `normalGameplay`
returns early on false. No other behavior changed: every non-chord path returns true.

`tests/test_boot.cpp` — eight cases. Seven device-free; `BootOrderKeepsSavedScores` builds a hermetic
save store in a temporary directory.

## Open questions / future work

The two-player round shares `handleStartSelect` with the solo one. When the link-cable states are
ported, that caller takes the same return value and stops the same way.
