# Type C — the rising floor

**Status:** Complete — the mode, its screens and its scores.

## Concept

A third game type, alongside the original two. Type C is the marathon played over a floor that keeps
coming up: a count of drops sits on the panel, every drop takes one off it, every line cleared puts
one back, and when it reaches zero the whole stack shifts up a row and a fresh garbage row arrives at
the bottom of the field. There is no line goal and no finish — you play until the stack reaches the
ceiling.

Type A rewards endurance and Type B rewards precision against a fixed target. Type C rewards keeping
the field low, because the field is being taken from you at a constant rate whatever you do.

## How it plays

The panel counts down. Under the label `RISE` sits the number of drops left before the floor comes up.
Where it starts is the player's: the difficulty screen offers six, from 16 down to 6, beside the
starting level. What a good stretch earns is banked rather than capped, so a player clearing well
builds a buffer to spend on a bad one.

Everything else behaves as Type A does. Lines accumulate rather than counting down, the score is
awarded as the round runs, the level climbs on the same thousand-line law with the same gravity ramp,
soft drops are worth points, and a high enough score earns the same rocket ending. The round starts
on an empty field: the floor comes to the player rather than being there to begin with.

## Design decisions

**The rise is the player's choice, and it is what the difficulty screen is for.** A fixed rise makes
one judgement about how much pressure the mode should apply, and that judgement is wrong for most
people — ten was too much at a medium skill level, which is the finding that opened this. Type C now
picks a level *and* a rise, the way Type B picks a level and a starting height, and each pair keeps its
own top scores because a score only means anything against others played at the same settings.

*Rejected:* leaving the rise fixed and tuning the single number until it felt right — it would have
made the mode easier or harder for everyone to suit one player. *Rejected:* deriving the rise from the
level, which would have folded two independent choices into one and removed the ability to play a fast
level with room to breathe.

**The range is narrow and evenly spaced: 16, 14, 12, 10, 8, 6.** Every value in it is a rise a player
can actually hold off, so the choice is about how much room they want rather than about whether the
mode is playable at all. *Rejected:* a wide range opening at 25 or above — that much room stops the
floor mattering, which is the whole mode. *Rejected:* an uneven curve with the gaps closing at the hard
end; it read well on paper and played as a cluster of near-identical hard settings and two easy ones
nobody would pick twice.

**The rise is triggered by drops, not by time.** A timed rise can fire while a piece is in flight,
which means moving the field under a falling piece and invalidating a collision test halfway through
a drop. A lock-triggered rise fires at a spawn point — the one moment in a round when nothing is
falling. It also keeps the pressure honest: the gravity ramp already makes seconds scarcer as the
level climbs, so a per-drop count scales with difficulty on its own and a per-second one would have to
chase it.

**The floor comes up after the field has settled.** The rise fires at the spawn point, downstream of
the whole clear pipeline, so a floor that arrives does so only once the flash, the compaction and the
wipe have finished. Under the credit law a clearing drop can never be the one that empties the count,
so the two rarely meet — but the ordering is guaranteed by where the seam sits, not by that
arithmetic, and retuning what a cleared row is worth cannot disturb it.

**The ceiling kills through the existing rule.** Blocks pushed past the top of the field are
discarded, and the round ends the way every round ends — the next piece spawns into an occupied cell
and the top-out count runs out. There is no separate instant-death rule for being crushed.

**Clearing buys drops back, one per line — it does not reset the count.** This is the rule the mode
turns on, and it took three passes to land. A count that never reset was relentless: the floor arrived
on a fixed schedule whatever the player did, and clearing bought nothing. A count that fully reset on
any clear went the other way — a single line every ten drops held the floor off forever, so it never
rose at all. Crediting one drop per cleared line puts the two in tension: a single line is exactly
breaking even, a double gains one, a tetris gains three. Staying ahead of the floor means clearing
more than one line per drop, sustained.

**Ten is both the starting count and the ceiling.** The credit cannot bank past it, so a good run
cannot buy a lead and then coast on it.

**The count is flat across levels.** The gravity ramp already supplies the difficulty curve; a second
curve on top of it would be two things to tune against each other.

**Every arriving row has a gap.** The row is generated one cell at a time from the same source a Type
B round's starting garbage uses, and carries the same guarantee: if the cells would have filled the
row solid, its rightmost one is forced empty. A solid row could never be cleared, so the stack could
only ever climb.

**Its own backdrop.** Type C shows four readouts — score, level, rise, lines — where the two stored
gameplay screens each show three and have room for exactly three. Rather than drop a readout to fit
an existing screen, Type C has a screen of its own, built from the same box, font and wall tiles the
others use, with the next-piece box copied across unchanged.

## Alternatives considered

**A piece-set change.** A different set of shapes was tried first and abandoned: the well's geometry
is load-bearing. Seven tetrominoes tile a ten-wide field without leaving holes, and arbitrary larger
pieces do not — the mode was unplayable in a way no amount of tuning fixes.

**Reusing the Type B backdrop with `RISE` in place of `HIGH`.** The label fits exactly, but that
screen carries no score cells, and Type C scores. The panel would have had to lose its score or grow
a row it does not have.

**A modifier on Type A rather than its own type.** Rejected: the mode has its own starting level, its
own scores worth keeping separately, and its own panel. Everything about it is a peer of the existing
two rather than a setting on one of them.

## Implementation

| File | Role |
|---|---|
| `src/systems/rising_floor.{h,cpp}` | The counter, the shift, the arriving row, and the seam the line-clear pipeline fires |
| `src/data/type_c_tilemap.h` | The Type C gameplay backdrop |
| `src/systems/readouts.{h,cpp}` | Type C's panel cells and the `RISE` countdown printer |
| `src/systems/line_clear.{h,cpp}` | Settles the count on each lock; fires the seam at both spawn points |
| `src/systems/menu_screens.{h,cpp}` | The 2×2 game-type grid behind the master switch, and the Type C difficulty screen |
| `src/systems/settings_screen.cpp`, `src/state/settings.{h,cpp}` | The `new modes` row and the switch it saves (schema 2 → 3) |
| `src/systems/mode_screen.{h,cpp}` | The screen that row opens, which explains the mode |
| `src/state/high_score_state.h`, `src/state/high_score_persistence.{h,cpp}` | Type C's own score table (topscores schema 1 → 2) |
| `src/systems/gameplay.cpp` | The Type C round init: backdrop, starting level, empty field, counter armed |
| `src/systems/scoring.cpp`, `src/systems/piece.cpp` | The award, level-up and soft-drop forks |
| `include/kirpich/game_type.h`, `include/kirpich/game_state.h` | The mode and its two screen states |
| `src/state/game_flow_state.h` | The chosen starting level and the live countdown |

The rise draws its cells from the same source as the piece randomizer, registered on the same virtual
machine, so a round's rises are part of the same divider's history as its piece draws.

## How it is reached

Type C is behind a switch. The settings screen's `new modes` row opens a screen that explains the
mode and carries the switch itself; with it on, the config screen's game-type box grows from one row
of choices to two — A and B on top, C below, and a fourth cell left empty for whatever comes next.
With it off the config screen is the cartridge's, cell for cell, and Type C cannot be reached at all.

Its scores are kept in a table of its own, in the same document as the other two.

## Open questions / future work

- **Ten is unsettled.** It is a starting value for both the count and its ceiling, to be tuned by
  playing rather than argued about.
- **Two-player.** When link-cable play lands, the two-player flow has to decide what a Type C round
  means across a cable — including whether the rise and the attack row interact.
