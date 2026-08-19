# The number readouts — behavioral contract

Reverse-derived from the Game Boy Tetris disassembly (upstream `b95c668`). Covers the four numbers
the stats panel shows during a round — the score, the level, the lines, and the Type B start height —
the routine that draws all of them, and the second background map most of them are drawn into twice.

Source anchors: `PrintScore` / `PrintSixDigitNumber` / `PrintNumber` (`tetris.asm:6617-6673`),
`AddBCD` (`:174-194`), the vertical-blank redraw (`:233-249`), `Call_243B` (`:5814-5823`), the level
step in `Call_244B` (`:5825-5876`), the field-wipe seams (`:5720-5771`), the round init
(`:4140-4218`), and the pause handler (`:4455-4487`).

## 1. What the panel shows

Both gameplay backdrops carry a stats panel down the right of the screen, with the value cells left
blank for the game to fill. The two panels differ:

| | Type A | Type B |
|---|---|---|
| Score | yes | — |
| Level | yes | yes |
| HIGH | — | yes |
| Lines | yes | yes |

`HIGH` names the **height of the starting garbage**, not a high score — the value the player picks on
the Type B difficulty screen alongside the level. The original's own comment on the write says
*"Print the height number somewhere on the right"* (`:4216`).

Cells, as (row, column) in the background map. Row 0 is the map's top row, which is the top of the
visible screen:

| Readout | Type A | Type B | Digits |
|---|---|---|---|
| Score | row 3, cols 13-18 | — | 6 |
| Level | row 7, col 17, tens at col 16 | row 2, col 16 | 1-2 |
| Lines | row 10, cols 14-17 | row 10, cols 16-17 | 4 / 2 |
| HIGH | — | row 5, col 16 | 1 |

Every one of those cells is blank in the stored backdrop, with two exceptions that confirm the
spans: the Type A backdrop carries a `0` at row 3 col 18 — the last of the score's six — and the
Type B backdrop carries `25` at row 10 cols 16-17, its starting line count.

## 2. The printer — `PrintNumber` (`:6624-6673`)

One routine draws every number. It takes a count of **digit pairs**, a source walking *down* through
packed decimal bytes from the most significant pair, and a destination walking *up* through map
cells. Each byte holds two digits.

The digit law, which is what the port reproduces:

- Leading zeros are drawn as spaces, not zeros.
- Once a nonzero digit has been drawn, every later digit is drawn, zeros included.
- The final digit is always drawn, even when the whole number is zero (`:6647-6650`). A score of
  nothing reads `     0`, not six spaces.

Two entry points fall into it:

- `PrintSixDigitNumber` (`:6621`) — three digit pairs.
- `PrintScore` (`:6617`) — the same, behind the gate in §3.

Port surface: `printNumber(map, row, col, value, digitPairs)` (`src/systems/readouts.h`). The port's
score and lines are ordinary integers rather than packed decimal, so the port emits the digit string
the law above produces — `2 × digitPairs` cells, space-padded on the left, never fewer than one
digit — instead of walking bytes. The two are equivalent for every value the game can reach, and §11
sweeps that equivalence.

## 3. One byte, two roles — the score-print flag

`$FFE0` is the most confusing byte in the readout path, and the port keeps both of its roles because
the call sites are built around their collision.

- **The score changed.** `AddBCD` sets it to 1 after every addition to the score (`:187-188`).
- **Draw the score.** `PrintScore` returns immediately when it is zero (`:6618-6620`). The score is
  only redrawn when it has actually moved.
- **Past the leading zeros.** Inside `PrintNumber` the same byte is the "a nonzero digit has been
  drawn" flag: cleared on entry (`:6625-6626`), set at the first nonzero digit (`:6661-6672`),
  cleared again on exit (`:6657-6658`).

The third role destroys the first. Any print clears the flag, so the *next* `PrintScore` is
suppressed — and every place that draws the score into both maps therefore forces the flag back to 1
between its two calls (`:244-245`, `:5729-5730`). Without that, only the first map would ever be
written.

Port surface: `GameFlowState::scorePrintFlag`, an unlabelled byte in the game-flow window. It is one
field rather than two because splitting the roles apart would remove the interference the call sites
exist to compensate for, and the second map would silently stop updating.

`AddBCD` is the only routine that sets the flag outside a print, and the port has no `AddBCD`: the
score is a decimal integer, so each addition is an ordinary sum. Every one of them sets the flag
instead, at the four sites that correspond to the original's four score-affecting `AddBCD` calls — the
Type A line-clear award (`:5032`), the soft-drop award (`:5296`), and the results tally's two
(`:6138`, `:4864`/`:4872`). Without them nothing would ever request a score draw.

The original has a fifth `AddBCD` call, inside the results-screen row printer (`:4676`), which uses
the score bytes as scratch. The port computes that value locally and never touches the score, so it
does not set the flag there. The difference is unobservable: that routine runs only on the Type B
results screen, and the score draw is gated to Type A during normal gameplay.

## 4. The score

`Call_243B` (`:5814-5823`) draws it, behind two gates:

- the game state is normal gameplay, and
- the game type is Type A.

so the score is never drawn on the Type B panel, which has no score cells, and never drawn outside a
running round. Six digits from the packed score, into row 3 starting at column 13.

It is called from three places, each of which draws into both maps:

| When | Order | Anchor |
|---|---|---|
| The vertical-blank redraw | live map, force the flag, second map | `:242-249` |
| Field wipe step 17 | second map, then force the flag | `:5727-5731` |
| Field wipe step 18 | live map | `:5740-5741` |

The wipe pair is split across two frames: step 17 draws the second map and arms the flag, and step 18
draws the live map on the frame after. The vertical-blank redraw does both in one pass.

The vertical-blank redraw is itself gated (`:236-241`): the score-changed request must be set
**and** the piece-lock process must be at stage 3. It clears the request when it is done (`:248-249`).

Port surface: `redrawScore(game)` for the vertical-blank path, `printScore(game, map)` for the gated
single-map draw.

## 5. The level

Two writers, and they behave differently.

**The round init** (`:4162-4168`) writes the level into the game type's own cell, in both maps. Which
cell is chosen by game type (`:4144-4151`) — Type B col 16 of row 2, Type A col 17 of row 7. The
original keeps the chosen cell in a byte of its own; the port derives it from the game type at the
point of use, since it is a pure function of it and is read only inside the routines that set it.

In heart mode the init also writes a heart glyph into the cell *after* the level, in both maps
(`:4169-4175`).

**The level step** (`:5852-5870`) redraws it when the level increases. This one is Type A only — its
routine returns early for Type B (`:5829-5831`), whose level is fixed for the whole round — and it
writes the tens digit as well once the level reaches 10, into the cell to the left of the ones. Both
digits go to both maps.

Port surface: `printLevel(game)` for the init, and the level step folded into `checkForLevelUp`
(`src/systems/scoring.cpp`), which has carried every other effect of that routine since it was
written and left this one out.

## 6. The lines

**The round init** (`:4183-4194`) seeds the count: 0 for Type A, 25 for Type B, written as its two
digits into row 10 — and only into the live map.

**Field wipe step 19** (`:5760-5771`) redraws it after every clear, again into the live map alone,
with the digit count forking on game type: four digits from column 14 for Type A, two from column 16
for Type B.

**Pause** (`:4464-4476`) copies four digit cells from the live map into the second map.

That copy is the only thing that ever writes lines into the second map, which means **the lines
number on the paused screen is stale until the moment you pause**. The original's own comment says
so (`:5729`): *"for some reason the number of lines is only updated when the pause button is actually
pressed. Bug?"* Ported as written.

Port surface: `printLines(game)` and `copyLinesToSecondMap(game)`.

## 7. The Type B start height

Written once, at round init (`:4214-4218`), into row 5 column 16 of both maps. The value is the
chosen start height, 0 to 5, drawn as a single digit. It never changes during a round.

Port surface: `printStartHeight(game)`.

## 8. The second background map

The hardware keeps two background maps, `$9800` and `$9C00`, and displays one at a time. Pausing
switches to the second (`:4461`); unpausing switches back (`:4487`).

The round init writes the whole gameplay backdrop into the second map as well as the live one
(`:4155-4157`) and stamps the pause message into it (`:4158-4161`). The paused screen is therefore
the same stats panel with no playing field and a `PAUSE` label — and the readouts above are the
reason the panel is not blank: each of them writes the second map at the same time it writes the
live one, so the paused screen carries the score, the level and the height that were current.

The link-cable round init (`:1245-1250`) and the launch scenes (`:2696-2801`) use the same map.
Neither is ported yet; the map they need now exists.

The link-cable pause is different: it does not switch maps, and draws its `PAUSE` label into the
live map instead (`:4562`), erasing it on unpause (`:4547-4554`).

Objects are not affected by the map selection, so anything drawn keeps being drawn over the paused
screen. The pause hides both piece objects on the way in, but Select still toggles the preview while
paused — a preserved quirk, covered in [`gameplay.md`](gameplay.md) §5.

Port surface: `DisplayState::secondMap` and `DisplayState::displayed`
(`src/state/display_state.h`), with `DisplayState::displayedMap()` returning the one the render
bridge should read.

## 9. Where each write lands

Extends the table in `docs/contracts/screen.md` §5.

| Write | Destination | Anchor |
|---|---|---|
| the score, vertical-blank redraw | both maps | `:242-249` |
| the score, wipe step 17 | second map | `:5727-5731` |
| the score, wipe step 18 | live map | `:5740-5741` |
| the lines, round init | live map | `:4183-4194` |
| the lines, wipe step 19 | live map | `:5760-5771` |
| the lines, at pause | copied live → second | `:4464-4476` |
| the level and the heart glyph, round init | both maps | `:4162-4175` |
| the level, level step | both maps | `:5856-5870` |
| the Type B start height | both maps | `:4214-4218` |
| the gameplay backdrop and pause message, round init | second map | `:4155-4161` |
| the link-cable `PAUSE` label and its erase | live map | `:4547-4554`, `:4562-4570` |

## 10. What is not drawn here

- **The top-scores table.** `DrawTopScoresToVRAM` (`:3893-3932`) copies a staged block onto the
  top-scores screen. It is a screen of its own, not a panel readout, and lands with the high-score
  recording it belongs to.
- **The end-of-round tally.** Already drawn, by the scoreboard count-up
  (`docs/contracts/scoring.md`).
- **Palette effects.** Unchanged from `docs/contracts/screen.md` §6.

## 11. What the tests pin

`tests/test_readouts.cpp`

1. The digit law over one, two and three digit pairs: space padding on the left, every digit drawn
   once the number has started, the final digit always drawn, and the boundaries 0, 9, 10, 9999 and
   999999.
2. The score-print flag: a score change sets it, a draw clears it, a draw with it clear writes
   nothing, and the forced set between a site's two calls is what puts the score in the second map.
3. The score's two gates — Type A only, normal gameplay only.
4. The vertical-blank redraw: both gates, both maps written, the request cleared.
5. The wipe seams: step 17 into the second map, step 18 into the live map, step 19's lines with the
   digit-count fork, and the second map left stale.
6. The level step: the digit in the game type's cell in both maps, the tens digit appearing at 10,
   and Type B never redrawing.
7. The round init: the level cell by type, the heart glyph beside it, the lines seed, the start
   height, and the second map carrying the backdrop and the pause message.
8. Pause: the displayed map switches and back, and the four lines digits are copied across.

The picture itself is verified by hand on a development machine: a continuous-integration runner has
neither a display nor the extracted art (`docs/engine/assets.md`).
