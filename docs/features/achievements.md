# Achievements

**Date:** 2026-09-11
**Status:** Complete — the set, the awarding, the screen and the end-of-round notice. Badge art, the
names and the hidden split are data and want a content pass.

## Concept

Thirty-six things worth doing, grouped into nine themed sections, awarded when a round ends and kept
across launches. The original game has nothing like it; this is the port's own.

They are recorded from what the game already knows. Every condition reads a finished round's own
state, the bonus scenes it reached, or the lifetime totals the statistics already keep — so the
feature adds a set of definitions, a check, and a screen, and nothing to the simulation.

The attract demo earns nothing. That falls out of the wiring rather than being a rule of its own: the
round is armed by the same call the statistics use to open a round, which turns a demo away before it
gets there.

## Design decisions

### No points

There is no score, no gamerscore, no completion percentage. An achievement is a thing you did, named
and dated. `AchievementDef::tier` runs 1 to 4 and orders them for display; it is not a value and
nothing sums it.

**Rejected: a point value per tier.** It invites the set to be judged by its total rather than read,
and the total then constrains what can be added later — a set that grows has to keep its arithmetic
honest forever.

### The saved key is the identity, not the position

The unlock records are an array indexed by `AchievementId`, and the save image is those records in id
order. So the set can grow — append an enumerator, widen the array, migrate older images forward —
without disturbing what a player has already earned.

The cost is that **an enumerator's order is fixed once shipped.** Reordering one moves its saved
record onto a different achievement. That is the trade: growth is free, reordering is not, and growth
is the thing a set of achievements actually does.

**Rejected: keying the save by the definition table's position.** Identical while the table never
changes, and silently wrong the first time it does.

### A condition is data, not a function

Each achievement names one `ConditionKind` out of a closed vocabulary of fourteen, with a few plain
fields beside it. The evaluator is one switch over the vocabulary.

That keeps the whole set a literal table a reader can scan, lets each condition shape be tested once
on its own rather than thirty-six times, and means **a new achievement is a new row** — no new code
unless it needs a shape that does not exist yet.

**Rejected: a `std::function` per achievement.** More expressive than anything the set needs, and it
makes the table unreadable and untestable as a table: thirty-six closures with nothing in common to
assert about.

### The check runs once, at the end of a round

By the time a round ends the level has finished climbing and the lines, score, drops and tetrises are
all accumulated, so one pass answers every condition. Running it during play would mean deciding, per
condition, what "so far" means, and paying for it every frame.

Three properties make one call safe from several places: it does nothing unless a round has concluded
and is awaiting the check, it clears the round's observations when it returns, and a round is armed
only when a real one begins. So the game-over screen, the rocket scene, the reset chord and the quit
path can all fire it, and a round is checked exactly once.

The reset and quit paths fire it directly for one reason: a round that had concluded but not yet been
checked — quitting or resetting from the game-over screen rather than pressing on — would otherwise
lose its unlocks.

### The date is injected, because the engine cannot supply one

The engine measures elapsed time; it has no calendar. So the unlock stamp takes a `NowDate` the host
provides, beside the statistics' existing `NowNanos`. A test supplies its own and pins an unlock date
exactly; without one the date is zero, which reads as "no date" rather than as a wrong one.

An unlock also captures the whole-application play time at that moment, which is a figure the
statistics already keep.

### Hidden means it would spoil a discovery

`hidden` is per achievement rather than a property of a tier or a section, and it defaults to shown.
Three of the thirty-six are kept back: the launch scenes, because the rocket and the Buran are the
game's secret endings and naming them on a page tells the player what they were meant to find.

Everything else is a rung on a ladder — a score tier, a line count, a level, a Type B height — and a
ladder is there to be aimed at. A target a player cannot see is not one.

A hidden one that has not been earned still shows its badge and says *hidden — revealed when earned*,
rather than showing the badge alone.

`hidden` decides only what the screen draws. It has no bearing on how an achievement is awarded, so
nothing about the condition or the save changes if one is revealed later.

### One section to a page

Nine sections, nine pages, and a page need not be full. The cursor walks a section's badges and
**turns the page when it walks off the top or the bottom**, so the whole set is one continuous walk
rather than a grid inside a pager the player has to operate separately. The only end stops are the
first section's top and the last section's bottom.

A turn keeps the cursor's column, landing in the first row of the section below or the last row of the
section above, and clamping when that row is shorter. A vertical step therefore means the same thing
at a page boundary as it does inside a page — which is the whole point of making the set one walk.
Dropping the column at the boundary undoes it: the player is walking a grid, and then suddenly is not.

### The cursor is its own object

The selected badge is marked by four corner brackets drawn around it, not by recolouring or
overdrawing the badge itself. A grid then reads the same however it is being walked — the badges keep
their own colours, and earned-versus-locked stays the only thing colour means on that screen.

The brackets are sized to the badge under them, because the emblems are different shapes: a square of
four tiles, a bar of four across, a standing figure three tall. A fixed box fits some and not others.

**Rejected: highlighting by drawing the badge a second time in another palette.** It makes selection
and earned-ness compete for the same channel, and it is the overdraw the engine's model exists to
avoid.

### They are the port's first declarative screens

Every screen before this one is the cartridge's: it writes tiles into a background map and entries
into an object buffer, and a bridge turns those into layers. This one is built the way the engine
wants — components that return what they are, assembled into layers, handed the whole frame while it
is up. See [`../engine/declarative-screens.md`](../engine/declarative-screens.md).

It is deliberately the reference for that model. The parts that are not the achievements' own — the
return types, a run of text as sprites, a plain backdrop, the two layer aggregates, the corner
selector — sit at the render layer where any screen can use them, so the eventual sweep of the older
screens has something already built to copy.

### The player is told at the end of the round, not during it

There is no room to announce an unlock mid-round, and a distraction during a game is not wanted
anyway. So a round that earned something shows it on the way out, after everything else the round
does — after a Type B tally, after a bonus scene — and before the top-score name entry, which each
difficulty screen forks into from its own init.

That lands on one point: the transition out of a finished round. There are two of them, because a
round that flies a rocket never passes through the game-over screen, and the two biggest score
achievements are *defined* by scores that earn a rocket — a notice spliced only at the game-over
screen would never once appear for them.

Both go through one seam that takes where the round was headed and returns where it actually goes.
Neither handler knows a notice exists, and the name-entry fork downstream is untouched.

### One badge at a time, a press, and nothing else

Several achievements can land in one round, and they are shown one after another rather than stacked:
a badge, its title, its criterion, and a press for the next. No sound and no heading: a badge and its
name say what has happened.

### A Type C round that flies the rocket comes back to its own picker

The cartridge sends every rocket round to the Type A difficulty screen, which is right there because
Type A is the only mode it has that earns one. This port gives Type C the same score boundaries, so a
Type C round flies the rocket too and has its own picker to come back to. The exit forks on the mode
that was played. Type B never reaches it: its ending is the Buran, which leaves through the tally.

The fork also decides which leaderboard a new top score enters its name over, since each difficulty
screen paints its own and forks into name entry from its own init.

## Implementation details

| Unit | Where |
|---|---|
| The unlock records and the round's observations | `src/state/achievement_state.h` |
| The save document | `src/state/achievement_persistence.{h,cpp}` |
| The set and the round-end check | `src/systems/achievements.{h,cpp}` |
| The screen's state | `src/state/achievements_screen_state.h` |
| The screen's logic | `src/systems/achievements_screen.{h,cpp}` |
| The screen's components | `src/render/achievements/` |
| The notice's state | `src/state/achievement_notice_state.h` |
| The notice's logic and the round exit | `src/systems/achievement_notice.{h,cpp}` |
| The notice's components | `src/render/achievements/banner.*`, `notice.*`, `notice_layout.h` |
| The shared render layer | `src/render/types.h`, `glyphs`, `backdrop`, `background_layer`, `sprite_layer`, `selection_corners` |

Full surface, wiring table and the recipes for adding one are in
[`../engine/achievements.md`](../engine/achievements.md).

The records live in their own `"achievements"` save document at schema version 1, beside the
settings, the top scores and the statistics in the same store. The reset chord keeps them.

### Where the count shows

The statistics chooser's achievements row opens the screen directly and carries how many have been
earned; the All-Time page's combined view carries the same figure. Both read one pair of accessors,
so the two cannot disagree.

The font has no slash, so the figure reads `12-36`. That constraint runs through every string these
screens draw — the font has the letters, the digits, a period and a hyphen, and nothing else.

## Open questions / future work

- **Badge art is a placeholder.** `artFor` gives one emblem per *section*, so every badge in a section
  wears the same one. It is data in `src/render/achievements/badge.cpp` — changing it touches no logic
  and no saved byte — and it wants a pass that gives each achievement its own.
- **Names and the hidden split want a content pass.** Both are data beside the id and neither is
  load-bearing; the set can be renamed wholesale without a migration.
- **What the screens look like is owed by hand.** The components are tested on what they return, and
  the logic on what it does to state; both are headless. A badge grid appearing, a selector sitting
  where it should, a panel reading correctly, and a banner whose title and description sit well beside
  their badge are verified on a running build.
