# Gameplay session

**Date:** 2026-08-16
**Status:** Complete

## Concept

A round of Tetris, as a set of game states: the init that sets one up, the frame that plays it, the
pause that interrupts it, and the game-over chain that ends it. This is the unit that turns the piece,
line-clear, and scoring systems from standalone functions into a game — before it, all three existed
and nothing called them.

## Design decisions

### The unit is the solo session, not "Type A gameplay"

This unit was first scoped as "Type A gameplay", covering states `$0A`–`$0D`. Reading the dispatch
table showed both halves of that to be wrong, and the scope was widened before any code was written.

The state list omitted `GameState_00` — the per-frame gameplay loop, the single most important state in
the game — along with the two states that bracket the game-over curtain. And nothing in the set is
Type A specific: `GameState_0A` forks internally on the game type and initialises both, and it is
entered from the Type A level picker, the Type B level picker, the Type B height picker, *and* the
attract-demo launch. A Type A round, a Type B round, and a demo all share this init and this frame.

The alternative considered was keeping the unit narrow and having the Type B work re-open the same
functions later. Rejected: `GameState_0A` is one routine, and splitting a routine's internal forks
across two units means two units own the same function. The Type B work instead narrows to the parts
that really are Type B specific — its screens, its height cursor, and its garbage fill.

### Garbage filling stayed a seam

`GameState_0A` fills the starting garbage for a Type B round, and the Type B work owns that fill. It
could have been pulled forward into this unit instead. It was not, because the fill reads the hardware
divider twice as a random source, which makes it work of a different character with its own
verification needs — the same reason the piece randomizer is its own unit.

The seam takes the row count and whether the fixed demo table applies, so the Type B work can install
the real fill without changing any signature here.

### The dead state is carried, not dropped

`GameState_0C` cannot be reached. Every write to the dispatch index loads an immediate value or zeroes
the register; every read is a comparison or the dispatch itself, so the index is never modified in
place; and the value `$0C` appears in executable code only as a loop count and an address comparison.
No instruction can put the game in that state.

It is ported anyway. The precedent that was weighed against it — an over-copy in the title-screen init
that was documented and dropped — is a different case: that was a read past the end of a table with no
observable effect, whereas this is a whole handler occupying a real entry in the dispatch table. The
port reproduces the table as the original built it.

### The pause family is ported whole, multiplayer branches included

Pause is shared between the solo frame and the two-player round, which calls into it twice. The
multiplayer branches could have been left as a seam for the multiplayer work. They were not: the
multiplayer state they touch already exists, the branches are small, and leaving them out would mean
the multiplayer work re-opens this file — the same argument that widened the unit's scope.

### The pause command joined the audio cues

Pausing and unpausing send the sound driver a command byte. It is a command rather than a cue — it
names an action, not something to play — but it has exactly the cue lifecycle: the game writes it, the
driver acts on it and clears it, and a stale value never re-fires. So it became a fifth field on the
cue mailbox rather than a new home of its own. It is the only new type this unit introduces.

### The caller-skip became a return value

One multiplayer pause routine discards its caller's return address so that returning from it skips the
rest of the caller. There is no way to express that directly, so the routine reports it and the caller
returns when told to. This is the only construct in the unit that could not be carried across as
written.

## Implementation details

Seven state handlers, the pause family, and two board helpers, all free functions in
`src/systems/gameplay.{h,cpp}`. The frame runs twelve steps in a fixed order, six of them the already
built piece, line-clear, and scoring systems, four of them demo seams, one the pause, one an early-out.

`installGameplayHandlers` takes a `GameplayWiring` aggregate rather than a list of parameters: the
piece randomizer is required, and the demo, garbage, and soft-reset seams default to inert.

No new state fields — every field the unit needs already existed. No data change: the game-over text it
prints into the board comes from the static tilemaps, and the rocket ending tiers come from the scoring
tables.

## Open questions / future work

- **Two behaviours in the two-player unpause look like defects.** The serial-flag test can never be
  taken because the load that precedes it leaves the condition flags alone; and the slave's unpause
  test reads inverted against the command it names. Both are carried exactly as written. Whether the
  second is genuinely inverted or whether the disassembly's naming of the serial buffers is reversed is
  a question the serial protocol work should settle.
- The garbage fill, the demo seams, and the soft reset are unwired until their own units land.
- Everything the round draws — the backdrop, the level digits, the pause text, the piece sprites — is
  the renderer's, and a round is not playable on screen until that exists.
