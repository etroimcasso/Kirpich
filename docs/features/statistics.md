# Statistics

**Date:** 2026-08-31
**Status:** Recording and the screens that show it are complete. Achievements are not built.

## Concept

The game keeps a record of what has been played: how many rounds, how long, how many pieces dropped,
what was scored, how many lines went and in what sizes, and the longest single round. It keeps that
record per difficulty combination — a Type A level, a Type B level and starting height, a Type C
level and rise — so a player can look at one particular way of playing rather than at one number for
everything. It also keeps how long the program itself has run.

Recording is always on. Whether a player can *see* any of it is a setting, and that setting is not
built yet; the point of separating the two is that a player who switches the display on a year from
now finds their whole history there rather than an empty table that starts counting from the day
they asked.

## Design decisions

**One table, and every other figure is computed from it.** Only the 130 per-combination slices are
stored — Type A's ten levels, Type B's ten levels by six heights, Type C's ten levels by six rises.
A game type's totals are the sum of its slices; the whole game's are the sum of all of them. The
alternative was to store the rollups as well, which was rejected because three stored levels can
disagree with each other and each one would need its own migration when the format grows.

**The longest round folds the same way, and its label is not stored.** Taking the largest of two is
as much a fold as adding them, so the longest round for a type, and for the game, come out of the
same walk. The combination it was played at — shown as `A / 1`, or `B / 1 / 3` for the two types
picked with a second value — is the slice the walk found it in, not a separate field. Storing the
label beside the length would let the two drift apart; deriving it makes that impossible. A tie goes
to the first slice in the walk, so the answer is stable.

**Whole-application play time is the one figure not derived from the table**, because it counts the
title screen and the menus, which belong to no round.

**Time is stamped, not counted.** A round takes the clock when it starts and again when it ends; the
difference is what it cost. Nothing counts frames, so the feature costs nothing while the game is
running. A pause banks the stretch just played and an unpause starts a new one, so time spent paused
— or on a screen opened from a pause — is never counted as play. That banking is in nanoseconds and
converted to seconds once, at the end: banking whole seconds would throw away a fraction at every
pause, and a round paused often would lose real time.

**A round belongs to the combination it started in.** The level a Type A round is played at rises as
it runs, so reading the selection at the end would file a finished round under a level it was never
started at. The combination is latched when the round begins, which is the same rule the top-score
tables already follow — and both now go through one shared derivation, so a score and the round's
own counts cannot land on different slices.

**Quitting during a round records it.** Every ordinary way of leaving — the settings screen's exit
row, the window's close button, the platform's own quit gesture — ends the run through one close-out
the engine drives before it tears down, and that close-out finishes the round in progress first. A
round abandoned that way counts in full: the score, lines and drops it had earned, and its length.
Quitting is a way of finishing a round, not a way of discarding one. Only a crash or a force-kill
skips it, and then only the play since the last save is lost.

**The attract demo records nothing.** A demo plays through the same round pipeline a player does, so
without a gate a title screen left running overnight would fill the tables. The gate is at the start
— a demo never opens a round, and every later call does nothing while no round is open — which is one
place rather than seven.

**The statistics outlive the reset chord**, as the top scores do. A player's history is not something
a button combination should be able to erase.

**Rejected: gating the recording on the setting.** The setting was going to switch recording on and
off, which would have meant a player who enabled it later had a table that began the day they found
the option. Keeping the record regardless and letting the setting govern only what is shown gives the
untouched game to a player who never opens it and the full history to one who does.

**Rejected: a running round timer on the panel.** There is no counter to show — that is what stamping
instead of counting means — and displaying one would mean adding the per-frame work the design
avoids.

## The screens

The statistics item on the title screen opens the statistics themselves, not a menu on the way to
them: five rows — the whole game's figures, each game type's, and the achievements — so a player is
one press from a number rather than three. Each row opens a branch whose screen is **paged**: related
figures a page at a time, under a heading that names what the page holds. All time has six pages
(play time, rounds, score, clears, pieces, favourites); a game type has two.

**A combination is a position on a picker, not a page of its own.** An earlier design gave every
combination its own page, which is sixty-four page turns for Type B and the same again for Type C.
Rejected: *"64 pages is way too many."* Instead a game type's two pages share one picker — a `level`
row, and a `height` or `rise` row for the two types that have one — and the figures under it change
as it moves. Each axis opens on `all`, which folds that axis away, so the mode's own aggregate, one
level across its variants and a single combination are all the same control scheme and none of them
needs a page. A combination that has never been played shows zeros; nothing is hidden and nothing
moves, so a figure is always in the same place.

The walk down the picker's rows and the turn to the next page are one motion, which is what the
settings screen's own walk across its pages does: down past the last picker row turns the page, and
up past the first row turns back. The top of the first page is an end stop. **B is what leaves a
screen**, here as everywhere else in the game — an early version backed out of a branch on up, which
is a thing nothing else in the game does.

**Every value names what the player picked.** A Type C rise reads as its interval — 16, 14, 12, 10,
8, 6 — and never as the 0-5 index it is stored at, which is a number that appears nowhere else in the
game. That binds the picker's own row, the longest round's label, and any figure naming a
combination.

**The longest round's label is spelled with hyphens** — `a-5`, `b-1-3`, `c-7-12` — because the font
has no slash. It has letters, digits, a period and a hyphen, and text the font cannot spell is not
drawn at all.

## Layout

A figure's label starts three cells in and its value ends two cells short of the last one, so the
numbers line up down the page and the page has a margin down both sides. The width is chosen per
figure, because the two ends share twenty cells: a lifetime score wants ten digits and leaves seven
cells for its label, where a clear count is happy with six and leaves eleven.

The pieces page draws the seven shapes three to a row, each over its own count, with a clear row
between one row of shapes and the next. A shape at its spawn orientation is two cells tall, so a
tighter grid runs them together and the page reads as one shape rather than seven — which is what a
first attempt at a two-column grid did. The shapes are sprites rather than map cells: they are the
game's own piece art, several tiles wide and see-through at their lightest shade, and each is placed
from its own smallest part offsets rather than from the composed origin a falling piece is drawn
from, so the seven line up with each other instead of hanging at different heights.

## Implementation details

| File | What it holds |
|---|---|
| `src/state/stats_state.h` | `StatSlice` (ten counts), the three tables, the application total, and the round in progress |
| `src/state/stats_persistence.{h,cpp}` | The `stats` save document: schema version 1, a 5204-byte image, encode/decode, save/load |
| `src/systems/stats.{h,cpp}` | Recording, the folds that read the tables back, and the duration text |
| `src/state/game_flow_state.h` | `combinationOf` — the one derivation of which combination a round is at |

A slice is ten 32-bit counts: rounds, seconds, longest round, drops, score, lines, and the four clear
kinds. Every count is 32 bits whether it needs to be or not, which makes the image's size a
multiplication and leaves no count as the one that overflows first. Counts saturate rather than wrap.

Drops and clears are counted as they happen; the round, its time, its score and its longest-round
comparison resolve when the round ends. Score is taken at the end because it has a per-round ceiling
of 999,999, and adding each award as it landed would compound that ceiling across a lifetime. Lines
are counted at each clear rather than read from the flow at the end, because Type A counts its lines
up and Type B counts them down.

Time comes from the engine's monotonic clock, handed to the handlers as a seam. Without one every
round is still recorded, at a length of zero.

## Open questions / future work

The screens are not built. What exists is the record and the calls that read it back: a game type's
totals, the whole game's, and the longest round with the combination it was played at. What is still
to come is the way in — a settings row that reveals the feature, a Stats item on the title screen,
and the screens themselves, which share their machinery with the achievements that follow.
