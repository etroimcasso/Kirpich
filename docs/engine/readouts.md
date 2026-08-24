# Number readouts

The four numbers the stats panel shows during a round — the score, the level, the line count, and the
Type B starting height — and the printer that draws them all. The behavioral specification of what the
original game does, line by line, is in [`../contracts/readouts.md`](../contracts/readouts.md); the
design rationale is in [`../features/readouts.md`](../features/readouts.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/readouts.h` / `.cpp` | The `kirpich::systems` readout functions. |
| `src/state/display_state.h` | The two background maps, and which one is displayed. |
| `src/state/game_flow_state.h` | `scorePrintFlag`, the byte that decides whether a score draw happens. |
| `tests/test_readouts.cpp` | The behavioral tests. |

The functions take a `GameContext&` and write through it. They own no state: the values come from
`GameFlowState` (level, line count, start height, game type) and `EngineState` (the score), and the
cells they write are on `DisplayState`.

## The panel

Each mode shows different things, on its own backdrop. Cells are (row, column) in a background map;
row 0 is the top of the visible screen.

| Readout | Type A | Type B | Type C |
|---|---|---|---|
| Score | row 3, columns 13-18 | not shown | row 2, columns 13-18 |
| Level | row 7, column 17 (tens at 16) | row 2, column 16 | row 6, column 16 (tens at 15) |
| HIGH | not shown | row 5, column 16 | not shown |
| RISE | not shown | not shown | row 8, columns 16-17 |
| Lines | row 10, columns 14-17 | row 10, columns 16-17 | row 10, columns 14-17 |

`HIGH` is the height of the starting garbage — the value picked on the Type B difficulty screen —
not a high score. `RISE` is the number of pieces left before a Type C round's floor comes up; see
[rising-floor.md](rising-floor.md).

Lines lands on row 10 on every screen, which is why `printLines`, `printLinesSeed` and
`copyLinesToSecondMap` have no Type C case — they reach it through the Type A arm unchanged.

## The surface

```cpp
namespace kirpich::systems {

// Draw the low 2 x digitPairs digits of value into map, left to right from (row, col), one digit per
// cell. Leading zeros are spaces; the final digit always prints. Clears flow.scorePrintFlag.
void printNumber(BackgroundMap& map, GameFlowState& flow, std::size_t row, std::size_t col,
                 std::uint32_t value, std::uint8_t digitPairs);

// Draw the six-digit score into one map. Does nothing unless the game is in normal gameplay, the mode
// has score cells (Type A and Type C do; Type B does not), and the score has changed since the last
// draw.
void printScore(GameContext& game, BackgroundMap& map);

// Draw the score into both maps. Does nothing unless a redraw is requested and the piece-lock process
// is at stage 3. Clears the request.
void redrawScore(GameContext& game);

// Draw the level into its game type's cell in both maps, with a heart beside it in heart mode.
void printLevel(GameContext& game);

// Redraw the level after it increases, in both maps, in its game type's cell. The tens digit appears
// in the cell to the left once the level reaches ten.
void printLevelStep(GameContext& game);

// Draw the round's opening line count into the displayed map.
void printLinesSeed(GameContext& game);

// Redraw the line count into the displayed map: four digits for Type A, two for Type B.
void printLines(GameContext& game);

// Draw the Type B starting height into both maps.
void printStartHeight(GameContext& game);

// Draw the Type C rise countdown into one map, two digits under the label RISE. Takes the map because
// the round init draws it into both and each later change draws only the displayed one.
void printRise(GameContext& game, BackgroundMap& map);

// Copy the four line-count digits from the displayed map into the second one.
void copyLinesToSecondMap(GameContext& game);

}
```

**Using them.** Every one is already wired. `redrawScore` runs from the frame's last beat in
`src/main.cpp`; `printScore` and `printLines` from the field-wipe stepper's steps 17, 18 and 19
([`line-clear.md`](line-clear.md)); `printLevelStep` from `checkForLevelUp`
([`scoring-system.md`](scoring-system.md)); `printRise` from the round init and from the rise itself
([`rising-floor.md`](rising-floor.md)); and `printLevel`, `printLinesSeed`, `printStartHeight` and
`copyLinesToSecondMap` from the round init and the pause handler ([`gameplay.md`](gameplay.md)).

To draw a number somewhere of your own:

```cpp
// 1234 into row 4, columns 6-9, as four digits: "1234".
kirpich::systems::printNumber(game.display.map, game.flow, 4, 6, 1234, /*digitPairs=*/2);
```

## Gotchas

- **A score draw needs the print flag set, and any print clears it.** `printScore` returns without
  drawing when `flow.scorePrintFlag` is zero. Every print — including a line-count print — clears it on
  the way out. That is why `redrawScore` sets it again between its two maps: without that, only the
  first would be drawn. Adding to the score sets it, which is what makes the score appear during play.
- **Most readouts write both maps; the line count does not.** The second map is the paused screen, and
  the line count reaches it only through `copyLinesToSecondMap`, which the pause performs. The count
  shown while paused is the one current at the moment of pausing.
- **The score is Type A only.** The Type B panel has no score cells — it shows HIGH instead — so
  `printScore` is inert in a Type B round however the flag is set.
- **Digits are raw bytes 0-9.** The font's first ten tiles are the digits, so a digit's tile index is
  the digit. Padding uses `CharTile::SPACE`.
- **`printLevel` writes the level as a raw value.** A starting level is a single digit, so it needs no
  conversion; `printLevelStep` is the one that splits tens from ones.
- **Digits above the printed width are dropped.** `printNumber(map, flow, r, c, 123456, 1)` draws
  `56`. The original reads a fixed number of packed-decimal bytes and this matches it.
- **Select works while paused, on purpose.** Switching to the second map does not affect objects, and
  nothing gates Select on the pause, so toggling the preview while paused puts it on the paused
  screen. That is what the original does and it is pinned by a test — do not gate it.

## Changing behavior

- **Where a readout is drawn** — the cell constants at the top of `src/systems/readouts.cpp`.
- **The leading-zero and always-print-the-last-digit rules** — `printNumber`.
- **When the score is drawn** — the gates in `printScore` (game type and state) and `redrawScore` (the
  request and the piece-lock stage).
- **What sets the print flag** — the four score additions, in `src/systems/scoring.cpp` and
  `src/systems/piece.cpp`.
- **What the paused screen looks like** — the round init in `src/systems/gameplay.cpp`, which fills the
  second map with the round's backdrop and stamps the pause message over it.
