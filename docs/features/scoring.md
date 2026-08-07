# Scoring

What points are worth. A line clear awards its base score — 40, 100, 300 or 1200 — times one more
than the level; soft-dropping a piece awards a point per row, minus one; a big enough Type-A score
buys a rocket at the game over screen; and every tenth line raises the level. This unit ports the
values and the pure math around them — the award, the rocket selection, and the level-up rule — as
functions the gameplay flow will call, ahead of the state that owns a running score.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `LineClearKind` | `include/kirpich/line_clear_kind.h` | `enum class : uint8_t`, SINGLE/DOUBLE/TRIPLE/TETRIS = 0-3 |
| `LineClearScoreEntry` | `src/data/scoring.h` | `{ kind, points }` |
| `kLineClearScores` | `src/data/scoring.h` | `std::array<LineClearScoreEntry, 4>` |
| `BonusEndingEntry` | `src/data/scoring.h` | `{ min_score, rocket_sprite }` |
| `kBonusEndings` | `src/data/scoring.h` | `std::array<BonusEndingEntry, 3>`, descending |
| `kLevelCap` / `kTypeBLineGoal` / `kSoftDropPointsPerRow` | `src/data/scoring.h` | `uint8_t` = 20 / 25 / 1 |
| `kScoreSaturation` | `src/data/scoring.h` | `uint32_t` = 999 999 |
| `kLinesPerLevel` | `src/data/scoring.h` | `uint8_t` = 10, derived |
| `lineClearAward(kind, level)` | `src/data/scoring.{h,cpp}` | `uint32_t` |
| `softDropAward(rows)` | `src/data/scoring.{h,cpp}` | `uint32_t` |
| `rocketSpriteForScore(score)` | `src/data/scoring.{h,cpp}` | `optional<uint8_t>` |
| `shouldLevelUp(lines, level)` | `src/data/scoring.{h,cpp}` | `bool` |

The exact values and their sources are pinned in [`../contracts/scoring.md`](../contracts/scoring.md).

## Decisions

**Decimal in the surface; BCD stays on the wire.** The original stores every score as
binary-coded decimal — the hex digits are the decimal digits — and its 6-digit score saturates at
999 999. The port stores the decoded numbers (40, not `$0040`; 200 000, not a `$20` top byte) and
leaves the BCD encoding to the parser and the fixture. What survives of BCD at runtime —
saturation on accumulation, digit rendering, the top-score byte walk — is score-*state* behavior
and ports with the gameplay loop that owns the score, not with this data unit.

**`LineClearKind` is minted, and its values are the game's own.** Upstream has no constants file,
but the kind is a real identity: the Type-B scoreboard state machine discriminates 0/1/2/3 in
Single/Double/Triple/Tetris order, and the per-kind clear counters sit at a 5-byte stride in the
same order. Same pattern as `PieceKind`. There is no "none" value — every award is one of the
four.

**The rocket sprite bytes stay raw.** `$58`/`$59`/`$5A` index the sprite master list, a space
that is not typed yet. Typing three bytes now would mint a throwaway surface; they are promoted
when the sprite identity space ports.

**The functions are verbatim math, with the quirks kept.** A soft drop awards one point per row
*minus one* — both original paths pre-decrement, and upstream's own comment asks
`; Why one point less? TODO`. The level-up rule stops forever once the line counter reaches 1000,
because the original returns early on a nonzero thousands digit. Both port as-is: the point of the
port is to match the original's shape, not to fix it.

**Awards return exact products; nothing clamps here.** Saturation lives where awards accumulate
into a score, which is state this unit deliberately does not own. `kScoreSaturation` is
transcribed so that state has its one datum ready.

**Preconditions assert rather than clamp.** Levels above 20, scores above 999 999, and line
counts above 9999 cannot occur — the cap gates levelling, the saturating add caps the score, and
the counter has four digits. As with gravity, the port asserts in debug builds instead of
inventing behavior for inputs the game never produces.

## Keeping it honest

The tables, the constants and the test fixture are generated from the disassembly by
`tools/asm_parser/parse_scoring.py`. The four base scores appear at three independent places in
the original — the Type-A award routine, the Type-B tally, and the Type-B scoreboard screen — and
the parser extracts all three and refuses to emit unless they agree value-for-value in the same
order, alongside the structural checks on each site (upstream's own value comments, the
kind-discriminator chain, the rocket ladder's shape, and BCD validity of every immediate). The
fixture holds raw wire bytes with no port type in it, and the tests decode those bytes themselves,
so the sweeps compare the typed tables against the ROM's own spelling. See
[`../engine/scoring.md`](../engine/scoring.md) for how to regenerate and change it.

## Not here yet

The running score itself — `wScore` and friends, the saturating accumulation, the scoreboard
state machine that replays Type-B tallies one tick at a time, score rendering, and the top-score
table — is state-and-flow behavior and ports with its owners. The level-up *caller* (when the
check runs, and the gravity re-lookup on success) ports with the gameplay loop. The contract
records all of it so those layers have a specification to build against.
