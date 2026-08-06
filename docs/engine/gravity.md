# Gravity

Gravity is how fast pieces fall. It is a countdown: a drop timer ticks down once per frame, and when
it reaches zero the piece falls one cell and the timer reloads. The reload value comes from a
21-entry table indexed by level — 52 frames at level 0, 2 frames at level 20 — plus one hidden
modifier, heart mode, that shifts the lookup 10 levels up.

This page covers the table and the lookup. The drop timer itself belongs to the gameplay loop and is
not written yet.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/gravity.h` | `FramesPerDropEntry` (the row type), `kFramesPerDrop` (the table), `kMaxLevel` / `kHeartModeLevelBoost`, the accessor declaration | Hand-written. |
| `src/data/gravity.cpp` | The accessor body | Hand-written. |
| `src/data/generated/gravity_data.inc` | The 21 rows | **Generated — do not hand-edit.** |
| `tests/fixtures/gravity_expected.h` | The same 21 values as raw bytes, for the test sweep | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`, included as `"data/gravity.h"` (the `src/` tree is on the
library's include path).

## Using it

```cpp
#include "data/gravity.h"

kirpich::framesPerDrop(0,  false);   // -> 52  (level 0, about 0.87 s per cell)
kirpich::framesPerDrop(9,  false);   // -> 10
kirpich::framesPerDrop(10, false);   // ->  9  (the sharpest step in the table)
kirpich::framesPerDrop(20, false);   // ->  2  (the fastest the game gets)

kirpich::framesPerDrop(0,  true);    // ->  9  (heart mode: the level-10 interval)
kirpich::framesPerDrop(11, true);    // ->  2  (shifted past the top; capped)

kirpich::kFramesPerDrop[7].frames;   // -> 21  (the table is public; index it by level)
```

`framesPerDrop(level, heartMode)` returns the number of frames a piece waits before gravity pulls it
down one cell.

- **`level` must be 0 through `kMaxLevel` (20).** Higher levels are not defined: a debug build
  asserts, a release build indexes out of range. Nothing in the game produces a higher level — the
  menu offers 0–9 and levelling up stops at 20 — so callers driving this from game state never need
  a check of their own.
- **`heartMode`** shifts the lookup up by `kHeartModeLevelBoost` (10) levels and stops at
  `kMaxLevel`. Levels 0–10 map onto levels 10–20; levels 11–20 all return the level-20 interval of 2
  frames. The displayed level is unaffected — heart mode makes the game faster than it says it is.
- The result is a **reload value, not a countdown.** Storing it into the drop timer is the caller's
  job; this function holds no state and reads none.

`kFramesPerDrop` is a `std::array<FramesPerDropEntry, 21>` you can index or iterate directly. Each
row carries its own `level`, and a `static_assert` in the header holds every row at the position its
`level` names, so `kFramesPerDrop[n].level == n` always.

## Regenerating the table

The rows and the test fixture are produced from the disassembly by the parser. Regenerate after
repinning the upstream source:

```sh
python3 tools/asm_parser/parse_gravity.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/gravity_data.inc \
  --fixture-out tests/fixtures/gravity_expected.h
```

The parser checks the source's structure as it reads — the table label appears exactly once, every
line inside it is a single-value decimal `db`, the row count is 21, and rows 0, 10 and 20 carry the
level annotations the original writes beside them — and stops with a citation if anything has moved,
rather than emitting a wrong file. The annotations are what catch a shifted row: drop a row ahead of
one and every later row slides, so the annotation no longer lands where the source says it should.
Python 3 (standard library only); it is a development tool and is never needed to build or test
Kirpich.

## Changing it

To change a drop interval, change the source and regenerate — never hand-edit the generated files,
since the next run overwrites them. To change the *lookup* — the heart-mode shift, the cap, the
out-of-range behavior — edit `src/data/gravity.cpp`; the table rows stay generated. `kMaxLevel` and
`kHeartModeLevelBoost` are in `src/data/gravity.h` and are the only two numbers the lookup body
spells out.

Changing `kMaxLevel` alone will not resize the table. The array's length derives from it, so a
mismatch fails to compile either way — lowering it leaves excess elements in the initializer, and
raising it leaves a short row the order check rejects. Change the table in the source and regenerate
first; `kMaxLevel` follows.

The exact values, the lookup's arithmetic, and every line in the original that reads or writes them
are in [`../contracts/gravity.md`](../contracts/gravity.md).

## Testing

`tests/test_gravity.cpp` sweeps all 21 rows against the generated fixture (raw bytes, so the table's
own values are checked rather than compared to themselves), pins the boundary values and the
non-increasing property, and exercises both lookup modes across their whole domain — every level in
normal mode, and every level in heart mode, which pins the shift and the cap level by level. The
parser has its own tests (`tools/asm_parser/test_parse_gravity.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_gravity`).
