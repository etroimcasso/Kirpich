# Playing field

The playing field is 18 rows by 10 columns. This page covers two things the port knows about it: its
fixed extent, and the schedule that redraws it a row at a time. The redraw is a "wipe" — after a line
clear compacts the stack, and for the game-over curtain, the field is copied back to the screen one
row per frame from the bottom up.

The wipe *behavior* — the row copy itself and the side effects a few rows trigger — belongs to the
gameplay and presentation code and is not written yet. What lives here is the geometry and the mapping
from the wipe counter to the row it redraws.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/playing_field.h` | The four constants and `playingFieldRowForWipeCounter` | Hand-written (the function); the constants are included from the generated file. |
| `src/data/generated/playing_field_data.inc` | The four constants | **Generated — do not hand-edit.** |
| `tests/fixtures/playing_field_expected.h` | The 18 raw address triples, for the test sweep | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`, included as `"data/playing_field.h"` (the `src/` tree is on the
library's include path).

## Using it

```cpp
#include "data/playing_field.h"

kirpich::kPlayingFieldRows;   // -> 18
kirpich::kPlayingFieldCols;   // -> 10

// The wipe counter runs from kPlayingFieldWipeCounterFirst (2) to kPlayingFieldWipeCounterLast (19).
kirpich::playingFieldRowForWipeCounter(2);    // -> 17  (the bottom row, redrawn first)
kirpich::playingFieldRowForWipeCounter(19);   // ->  0  (the top row, redrawn last)
```

`playingFieldRowForWipeCounter(counter)` returns the field row (0 = top, `kPlayingFieldRows − 1` =
bottom) that the wipe redraws at a given counter value. The counter starts at the bottom row and walks
up, so a higher counter is a higher row on screen — a lower row index.

- **`counter` must be `kPlayingFieldWipeCounterFirst` through `kPlayingFieldWipeCounterLast` (2–19).**
  Outside that range the answer is undefined: the original never redraws a row for those values, so
  the port defines no result and callers driving this from a live wipe never produce one.
- The function holds no state and reads none; it is a single subtraction and is `constexpr`, so it can
  be used at compile time.

## Regenerating the constants

The constants and the test fixture are produced from the disassembly by the parser. Regenerate after
repinning the upstream source:

```sh
python3 tools/asm_parser/parse_playing_field.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/playing_field_data.inc \
  --fixture-out tests/fixtures/playing_field_expected.h
```

The parser reads the 18 wipe routines by their shape rather than by line number — the counter gate,
the two address loads, and the shared row-copy call — and stops with a citation if anything has moved:
it checks that the routines are the contiguous run 02–19, that every address obeys the closed form
`base + (19 − counter) × 32`, that the row-copy helper still loads a width of 10 and increments the
counter, and that the per-frame dispatch still calls all 18 in descending order. Python 3 (standard
library only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

The field's size and the counter's range are fixed by the original and are not tuning knobs — to
change them you would change the source and regenerate, but there is no reason to. The generated files
are overwritten on the next run, so never hand-edit them.

To change the *mapping* — how a counter value turns into a row — edit `playingFieldRowForWipeCounter`
in `src/data/playing_field.h`. It is the only hand-written line of behavior in the unit, and it is a
closed form (`kPlayingFieldWipeCounterLast − counter`), so a change there is a change to the geometry
the rest of the port reads.

The exact addresses, the counter's full lifecycle, and every line in the original that reads or writes
the counter are in [`../contracts/playing-field.md`](../contracts/playing-field.md).

## Testing

`tests/test_playing_field.cpp` pins the four constants against the contract, sweeps all 18 fixture
rows against the closed form (both addresses and the lockstep stride, and that the counters are the
contiguous run 2–19), checks the counter→row mapping against every fixture row and that it decreases
as the counter rises, and pins the two ends of the walk to concrete rows and addresses. The parser has
its own tests (`tools/asm_parser/test_parse_playing_field.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_playing_field`).
