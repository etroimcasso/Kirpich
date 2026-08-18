# Type B ending

**Date:** 2026-08-17
**Status:** Complete

## Concept

What a player is shown after winning a Type B round: the scoreboard that totals what the round was
worth, and — on the hardest level — the dance that runs before it. Three game states, and together they
close the one place where a finished round had nowhere to go.

Before this unit, a won Type B round dead-ended. The line-clear pipeline already wrote the right next
state when the last line cleared, but neither of the two states it could write had a handler, so the
game reached the end of a winning round and stopped on a screen that never changed.

## Design decisions

### The unit is all three states, including the scoreboard

The work was first scoped as the two bonus-ending states — the dance layout and the dance itself. That
misses the scoreboard, which is the join point for *both* endings: a round below level 9 goes straight
to it, and the dance falls through to it as well unless the round was started at the maximum garbage
height. Porting the two dance states alone would have moved the dead end rather than removed it.

The same scoping pass moved two states that had been filed here to the multiplayer work: they are the
two-player difficulty and start-height screens, bound to the link-cable protocol, and are not Type B
states at all.

### Whether the music is still playing is a supplied query

The dance holds until its jingle ends, which means one of these handlers has to ask the sound driver
what it is playing. A state handler receives only the game's state aggregate, and the sound system is
not part of it.

Three options were weighed. Putting the sound system on the state aggregate was rejected: the aggregate
is state, and mixing a live system into it would make every handler's signature imply access to
hardware. Copying the driver's read-back byte into the state each frame was rejected as a second copy of
something the driver already publishes, with a staleness question attached. What shipped is a supplied
callable — the same shape the demo launch, the top-score refresh, and the garbage fill already use, and
the sound system already publishes exactly the value it needs.

**Its default is "no song", which ends the dance.** That direction was chosen deliberately. The opposite
default would leave a build with no sound wired animating forever with no way out; a dance that ends
early is a wrong screen, but a dance that never ends is a stuck game. The tests assert the default
directly so it is not quietly reversed.

### The animation periods live with the code that uses them

The dance seeds each of its ten performers with its own animation period, from a ten-byte table. Every
other exact table the port carries is generated from the disassembly, and the question was whether this
one should join them.

It stays a local constant. It has exactly one consumer, it is ten bytes, and it is pinned entry by entry
in the tests — the same treatment given to the handful of other single-use values the port carries
inline. Promoting it would have meant a new generated artifact for a table nothing else reads.

### The screen loader is local, and its ordering is not shared with the gameplay one

Both screens are drawn by copying a field-shaped tilemap into the board, and the copy also starts the
field wipe animation. The gameplay session has a similar helper — and it arms the wipe *before* filling,
where this one arms it *after*.

They are not the same helper and were not merged. The original writes the wipe step only when the copy
reaches its terminator, so the arm genuinely comes last here; the gameplay one genuinely comes first,
and two of its three callers depend on that. This unit's copy is file-local, so neither can drift into
the other.

### The redraw-only frame is carried as a branch

One frame of the dance — the one where the frame timer reads exactly 20 — redraws the performers and
does nothing else. Redrawing is the renderer's work, so in the port that branch has no body.

It is still there, as an explicit early return, rather than being folded into the timer check below it.
The two are not equivalent statements about the original: this one runs *instead of* the animation on
that frame, and reading the code later without it would suggest a frame the original animates.

## Implementation details

| | |
|---|---|
| Handlers | `typeBVictoryJingle` (`$05`), `initBonusEnding` (`$22`), `dancers` (`$23`) |
| Files | `src/systems/type_b_ending.{h,cpp}`, `tests/test_type_b_ending.cpp` |
| Consumes | the scoreboard and dancers tilemaps, the dancer scene list, the score-row printer, the sprite slots' animation pair, the six Type B jingles |
| Adds | one seam type (`MusicPlayingQuery`) and one ten-byte local table |
| State / data | no change to either |

The scoreboard prints each line-clear kind's value for the round's level, except at level 0 — the stored
screen already carries the level-0 values, and the original skips the whole print block there.

The dance reveals one more performer than the round's starting garbage height, except at height 5, which
reveals all ten rather than six. Only one of the ten moves vertically: the jumping cossack alternates
between two heights as his sprite flips, and the test for that is on the sprite id, so the other nine
flip in place.

Both endings leave the line count at 25 — the original's own value, stored packed-decimal and easy to
misread as 37.

## Open questions / future work

- The Buran launch a height-5 round enters is the ending-scenes work; this unit only writes the state.
- The wipe both screen loads arm is stepped by the line-clear system; nothing here drives it.
- Wiring the music query to the running sound system belongs to whoever assembles the handlers.
