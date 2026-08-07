# Scoring

Scoring is what points are worth and when the level rises. A line clear awards its base score —
40, 100, 300 or 1200 for a Single, Double, Triple or Tetris — multiplied by one more than the
level. Soft-dropping awards one point per dropped row, minus one (an original quirk, kept). A
Type-A game that ends at 100 000 points or more shows a rocket launch, bigger at 150 000 and
200 000. And every tenth cleared line raises the level, up to 20 — unless the line counter has hit
1000, after which the original (and so the port) never levels again.

This page covers the tables and the four scoring functions. The running score — accumulation,
saturation, display — belongs to the gameplay loop and is not written yet.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `include/kirpich/line_clear_kind.h` | `LineClearKind` (SINGLE/DOUBLE/TRIPLE/TETRIS = 0–3) | Hand-written. |
| `src/data/scoring.h` | `LineClearScoreEntry` / `BonusEndingEntry` (the row types), `kLinesPerLevel`, the four accessor declarations | Hand-written. |
| `src/data/scoring.cpp` | The four accessor bodies | Hand-written. |
| `src/data/generated/scoring_data.inc` | `kLineClearScores`, `kBonusEndings`, `kLevelCap`, `kTypeBLineGoal`, `kSoftDropPointsPerRow`, `kScoreSaturation` | **Generated — do not hand-edit.** |
| `tests/fixtures/scoring_expected.h` | The same data as raw wire bytes, for the test sweeps | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`; include `"data/scoring.h"` (the enum header comes with it).

## Using it

```cpp
#include "data/scoring.h"

using kirpich::LineClearKind;

kirpich::lineClearAward(LineClearKind::SINGLE, 0);    // ->    40  (base x (level + 1))
kirpich::lineClearAward(LineClearKind::TETRIS, 4);    // ->  6000
kirpich::lineClearAward(LineClearKind::TETRIS, 20);   // -> 25200  (the largest single award)

kirpich::softDropAward(0);    // -> 0
kirpich::softDropAward(1);    // -> 0   (one point per row, minus one)
kirpich::softDropAward(10);   // -> 9

kirpich::rocketSpriteForScore(99999);    // -> nullopt (no bonus ending)
kirpich::rocketSpriteForScore(100000);   // -> 0x5A    (the smallest rocket)
kirpich::rocketSpriteForScore(200000);   // -> 0x58    (the biggest)

kirpich::shouldLevelUp(90, 8);    // -> true   (floor(90 / 10) = 9 > 8)
kirpich::shouldLevelUp(90, 9);    // -> false  (9 > 9 fails; nothing happens until line 100)
kirpich::shouldLevelUp(1000, 5);  // -> false  (1000+ lines: levelling has stopped for good)

kirpich::kLineClearScores[3].points;   // -> 1200  (the tables are public; index by kind)
kirpich::kBonusEndings[0].min_score;   // -> 200000
```

- **`lineClearAward(kind, level)`** returns the exact product; nothing clamps here. The running
  score saturates at `kScoreSaturation` (999 999) *when awards accumulate into it*, which is the
  score owner's job, not the award's. `level` must be 0 through `kLevelCap` (20) — a debug build
  asserts, and nothing in the game produces more.
- **`softDropAward(rows)`** keeps the original's off-by-one: N rows score N − 1, and zero rows
  score zero (the original skips the award entirely for a zero counter).
- **`rocketSpriteForScore(score)`** walks `kBonusEndings` highest-first and returns the first
  tier's sprite byte, or `nullopt` below 100 000. The bytes (`0x58`/`0x59`/`0x5A`) index the
  sprite master list, which is not typed yet — they are plain `uint8_t` until it is. `score` must
  not exceed `kScoreSaturation`.
- **`shouldLevelUp(lines, level)`** is the Type-A rule: true when `floor(lines / 10)` exceeds the
  level, the level is below `kLevelCap`, and `lines` is below 1000. It is a pure predicate — the
  caller owns incrementing the level and re-seeding gravity (see
  [gravity.md](gravity.md)). `lines` must not exceed 9999 (the original's counter is four digits).
- **`kTypeBLineGoal`** (25) is the line countdown a Type-B game starts from; Type-B completion
  awards no score bonus.

Both tables carry their identity in the rows: a `static_assert` holds every `kLineClearScores`
row at the position its `kind` names, and another holds `kBonusEndings` in strictly descending
threshold order, so a regenerated table that broke either would not compile.

## Regenerating the tables

The rows, the constants and the test fixture are produced from the disassembly by the parser.
Regenerate after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_scoring.py \
  --source-root ../tetris \
  --all \
  --inc-out     src/data/generated/scoring_data.inc \
  --fixture-out tests/fixtures/scoring_expected.h
```

Scores in the original are binary-coded decimal — the hex digits are the decimal digits — and the
parser decodes them, so the generated files hold ordinary decimal numbers. It checks the source's
structure as it reads: the four base scores appear at three independent places in the original
(the award routine, the end-of-round tally, and the scoreboard screen), and the parser refuses to
emit unless all three agree value-for-value in the same order. Each site's own anchors are checked
too — the `; 40 points for a Single` comments, the kind-discriminator chain and its counter
labels, the rocket ladder's exact compare-and-increment shape, the single-site constants, and BCD
validity of every immediate (any nibble above 9 stops the run). It stops with a citation if
anything has moved, rather than emitting a wrong file. Python 3 (standard library only); it is a
development tool and is never needed to build or test Kirpich.

## Changing it

To change a score, a threshold, or a constant, change the source and regenerate — never hand-edit
the generated files, since the next run overwrites them. To change the *math* — the multiplier,
the off-by-one, the tier walk, the level-up predicate — edit `src/data/scoring.cpp`.
`kLinesPerLevel` (10) is the one hand-written number: the original divides by ten implicitly, by
dropping a digit, so there is no ROM literal to transcribe it from.

The exact values, the BCD wire format, both soft-drop paths, the level-up derivation, and every
line in the original that reads or writes them are in
[`../contracts/scoring.md`](../contracts/scoring.md).

## Testing

`tests/test_scoring.cpp` sweeps both tables in full against the generated fixture — which holds
the ROM's raw wire bytes, decoded by the test itself, so the tables are checked against the ROM's
own spelling rather than against themselves — pins every constant, and exercises the four
functions across their whole domains: every kind at every level for the award, every tier edge
for the rockets, the full 10 000 × 21 grid for the level-up rule, and the field-height range for
the soft-drop quirk. The parser has its own tests (`tools/asm_parser/test_parse_scoring.py`, run
with `python3 -m unittest tools.asm_parser.test_parse_scoring`).
