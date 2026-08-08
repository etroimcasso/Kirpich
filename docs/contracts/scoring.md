# Contract — Scoring

Reverse-derived behavioral contract for Kirpich's scoring data: the line-clear award table, the
soft-drop award, the bonus-ending (rocket) thresholds, and the level-progression constants, plus
the pure math that consumes them. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors below are the authority the tests check
against.

Everything the original scores is a **BCD number**: each nibble is one decimal digit, so the hex
spelling *is* the decimal value — `$1200` is 1200 points. The running score `wScore`
(`wram.asm:5`) is three such bytes, little-endian, six digits, and there is no binary score
anywhere in the game.

---

## The BCD wire format

The port stores every scoring value **decoded** — base scores as 40/100/300/1200, thresholds as
200 000/150 000/100 000 — and treats BCD purely as the ROM's wire format. The decode rule: read
the hex digits as decimal digits, rejecting any nibble above 9. A threshold byte decodes as
`bcd(byte) × 10 000`, because it is compared against the *top* byte of the six-digit score — the
upper two of six digits — so `$20` means 20 in the 10 000s place: 200 000. That top-byte compare
is exactly equivalent to a full-score compare **because all three thresholds are multiples of
10 000**: two scores that differ only in their lower four digits straddle no tier boundary.

`wScore` is mutated exclusively through `AddBCD::` (`tetris.asm:174`), which adds a BCD-encoded
16-bit operand digit-by-digit with `daa`, propagates the carry into the third byte, and on
overflow **saturates the whole number at 999 999** by storing `$99` into all three bytes
(`tetris.asm:190-193`). Saturation is therefore a property of *accumulation into the score*, not
of any single award — the port's award functions return exact products, and the clamp belongs to
the gameplay-loop state that owns the running score (ports later, with that loop). The one
saturation datum consumers need, `kScoreSaturation = 999 999`, is emitted from the `$99`
triple-store anchor.

Score *rendering* (digit printing), the scoreboard tilemap addresses, and the top-score table
walk (`UpdateTopScores::`, `tetris.asm:3737` — compares score bytes most-significant-first) are
consumer behavior and port with their owners, not here.

## Line-clear base scores

40 / 100 / 300 / 1200 for Single / Double / Triple / Tetris. The four BCD immediates appear at
**three independent sites**, all four values at each, and the parser extracts all three and
asserts they agree value-for-value in the same kind order:

| Site | Anchor | Role |
|---|---|---|
| `AddLineClearScore::` | `tetris.asm:5005/5010/5015/5019` | Type-A live award, at wipe completion |
| `UpdateScoreboard::` | `tetris.asm:4887/4892/4897/4902` | Type-B end-of-round tally |
| `GameState_05::` display pairs | `tetris.asm:4627-4638` | Type-B scoreboard screen |

The award site carries upstream's own value comments (`; 40 points for a Single` …
`; 1200 points for a Tetris`), which double-pin each immediate: the comment's number must equal
the decode.

**The award math.** A clear awards `base × (level + 1)`, computed as repeated `AddBCD` — the
multiply loop runs `level + 1` times (`tetris.asm:5025-5036` for Type A against `hLevel`;
`Call_25D9`'s two loops at `tetris.asm:6133-6141` and `:6151-6160` for Type B against
`hTypeBLevel`). The Type-B display screen recomputes the same products into `wScore` used as
scratch space (`PrintLineClearScores::`, `tetris.asm:4662-4678`, then zeroed again at
`:4639-4645`). Maximum single award: 1200 × 21 = 25 200.

**Type-A award gates** (`tetris.asm:4993-5001`): game type `$37` (Type A), gameplay state 0, and
wipe counter 5 — the award lands when the line-clear wipe finishes. These are caller conditions
and port with the gameplay flow.

### Kind identity

The four kinds have a real identity role in the ROM, which is why the port mints
`LineClearKind : uint8_t` with values 0-3:

- `UpdateScoreboard::` discriminates `wScoreboardState` (`wram.asm:42`) with `and a` /
  `cp a, 1` / `cp a, 2` / fall-through (`tetris.asm:4890-4904`) — states 0/1/2/3 are
  Single/Double/Triple/Tetris (state 4 dispatches to the soft-drop tally, `:4885-4886`).
- `wLineClearStats` (`wram.asm:15`) holds the per-kind clear counters at a 5-byte stride
  (`; 5 bytes per type`, `tetris.asm:5003`) in the same order: `wSinglesCount` /
  `wDoublesCount` / `wTriplesCount` / `wTetrisCount` at offsets 0/5/10/15.

There is no "none" enumerator: every award site resolves to exactly one of the four.

## Soft-drop scoring

One point per soft-dropped row — **minus one**. Both paths pre-decrement the row counter before
converting it to points, and the port preserves the off-by-one verbatim:

- **Type A** (`tetris.asm:5283-5296`): at piece lock, `hSoftDropCounter` is converted to BCD by
  an inc/`daa` loop whose `dec c` *precedes* the first increment (`:5286`), yielding
  `rows − 1`, then added to `wScore` in one `AddBCD`.
- **Type B** (`tetris.asm:5244-5257`): at piece lock, `hSoftDropCounter − 1` (the `dec c` at
  `:5251`, upstream's own comment `; Why one point less? TODO`) accumulates into the *binary*
  counter `wSoftDropPoints` (`wram.asm:36`). At the scoreboard, `tallySoftDropPoints::`
  (`tetris.asm:4844-4878`) drains it one point per tick, adding `de = 1` (`:4861` — the
  transcribed `kSoftDropPointsPerRow`) to both `wSoftDropPointsBCD` and `wScore`.

Neither path runs with a zero counter — the lock path skips the whole award when
`hSoftDropCounter` is zero (`tetris.asm:5237-5239`) — so the port's `softDropAward(0) == 0`
matches the guard's no-op. A second upstream quirk, quoted for completeness: a tally tick that
finds the counter already at zero jumps straight to the next scoreboard state
(`tetris.asm:4854`, upstream comment `; What? Bug`); that is scoreboard state-machine behavior
and ports with it.

## Bonus endings (rockets)

At Type-A game over (`GameState_0D::`, gate `cp a, $37` at `tetris.asm:4943-4944`), the game
reads the score's top byte (`ld hl, wScore + 2`, `:4946`) and walks a descending threshold
ladder (`:4948-4956`): sprite base `$58`, `inc b` per missed tier.

| Tier | Threshold byte | Score | Rocket sprite | Anchor |
|---|---|---|---|---|
| 1 | `$20` | ≥ 200 000 | `$58` | `tetris.asm:4949` — `; At least 200k points` |
| 2 | `$15` | ≥ 150 000 | `$59` | `tetris.asm:4952` — `; At least 150k` |
| 3 | `$10` | ≥ 100 000 | `$5A` | `tetris.asm:4955` — `; At least 100k` |

Below 100 000 there is no bonus ending (`.noBonusEnding`, `:4957`). First match wins; the port's
`rocketSpriteForScore()` walks the same order over full decoded scores, which picks the same tier
as the top-byte walk per the ×10 000 equivalence above, and returns the tier's rocket as a
`SpriteId` (`ROCKET_L`/`ROCKET_M`/`ROCKET_S`; see [`sprites.md`](sprites.md)). The `$58`/`$59`/`$5A`
bytes above are the ROM's wire values, preserved in the test fixture.

## Level progression (Type A)

Level-up is a **rule, not a table** (`Call_244B::`, `tetris.asm:5825-5876`; runs only in
gameplay state 0 for game type `$37`, `:5826-5831`): the level increments when
`floor(lines / 10) > level`. The derivation from the digit-shift code:

1. **Cap:** `cp a, $14 / ret z` (`:5834-5835`) — `hLevel` is plain binary, `$14` **is** decimal
   20, and 20 is terminal. (`kLevelCap = 20`; the gravity table ends at the same cap.)
2. **Thousands cutoff:** the high nibble of `hLines`' upper byte is the thousands digit;
   `and a, $F0 / ret nz` (`:5839-5840`) returns forever once it is nonzero — **at 1000+ lines
   the game never levels up again.** Preserved verbatim.
3. **Digit shift:** hundreds and tens digits are packed into one byte —
   `(hundreds << 4) | tens` (`:5841-5848`) — which as a two-digit BCD number is exactly
   `floor(lines / 10)` for lines below 1000.
4. **Compare:** `Call_249D::` (`:5878`) converts the binary level to BCD via an inc/`daa` loop;
   `cp b` then returns on carry (less) or zero (equal) and increments only on greater
   (`:5849-5852`). BCD-vs-BCD `cp` ordering is monotonic in the decimal values — both operands
   are two BCD digits, and byte comparison of `(d1 << 4) | d0` orders identically to
   `10·d1 + d0` — so the whole check is exactly decimal `floor(lines / 10) > level`.

The divisor 10 (`kLinesPerLevel`) is implicit in the digit shift — dropping the ones digit — not
a ROM literal; the port derives it rather than transcribing it. On level-up the original replays
the gravity lookup (`call LookupGravity`, `:5875` — see `docs/contracts/gravity.md`); that
re-seed is caller behavior. The caller-side conditions (Type A only, gameplay state 0) port with
the game loop.

`shouldLevelUp(lines, level)` requires `lines <= 9999` (the counter is four BCD digits) and
`level <= kLevelCap`; within that domain it is
`lines < 1000 && level < 20 && lines / 10 > level`.

## Type B

- **Line goal:** a Type-B game initializes `hLines` to `$25` BCD = **25 lines**, counting down
  (`tetris.asm:4656-4657`, upstream comment `; BCD encoded`). Transcribed as
  `kTypeBLineGoal = 25`.
- **No completion score bonus.** Finishing the 25 lines awards nothing beyond the scoreboard
  tally; completing **level 9** triggers the bonus-ending scene and that is all (`.typeBDone`,
  `tetris.asm:5788-5795`, upstream comment `; Completing level 9 gives a bonus ending`).

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_scoring.py`, `--all`):
  `src/data/generated/scoring_data.inc` (both tables and the four transcribed constants, decoded
  to decimal) and `tests/fixtures/scoring_expected.h` (the same data as raw wire bytes,
  independent of the port types). Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `include/kirpich/line_clear_kind.h` (the minted enum),
  `src/data/scoring.h` (the row types, `kLinesPerLevel`, the order `static_assert`s) and
  `src/data/scoring.cpp` (the four function bodies).

### Transcription asserts

`parse_scoring.py` hard-errors (with a `file:line` citation) on any of: a parsed label missing,
duplicated, or unterminated; **the three base-score sites disagreeing** in value or order; an
award-site immediate whose `; N points for a <Kind>` comment names a different value or kind;
the `ld bc, 5` stride missing its `; 5 bytes per type` comment; a scoreboard kind group whose
stats label or discriminator breaks the Single/Double/Triple/Tetris order; the soft-drop
dispatch (`cp a, 4` to the tally routine) missing; more or fewer than four display pairs, or a
score load not paired with its print call; a rocket ladder that does not yield exactly three
strictly-descending tiers, a rung whose `; At least NNNk` comment disagrees with its threshold,
or a missing top-byte read; a level cap not gated by `ret z`; a missing tally operand or
saturation tail; and **any BCD immediate containing a nibble above 9**.

---

## Tested by

`tests/test_scoring.cpp` — full-corpus sweeps of both tables against the fixture (the test
decodes the fixture's wire bytes itself, so expected values flow from the ROM spelling, never
hand-typed); the constants pinned by value; the award math across all 4 kinds × levels 0-20; the
rocket boundary set at every tier edge; the level-up rule across its entire
10 000 × 21 input domain; and the soft-drop minus-one quirk across the field-height domain. The
parser's own structural checks (`tools/asm_parser/test_parse_scoring.py`) guard the scan against
upstream changes.
