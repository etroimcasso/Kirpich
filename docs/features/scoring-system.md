# Scoring system

**Date:** 2026-08-16
**Status:** Delivered — the five scoring routines (the Type A live award, the Type B results count-up,
the Type A level-up, the scoreboard row printer, and the scoring reset) as free functions, plus the
level-up wired into the field wipe. No gameplay or results handler is installed yet; the handlers compose
these when they land.

## Concept

Three award paths turn play into points — a Type A game folds each line clear into the score as the field
wipes, a Type B game counts the round's clears and soft-drop points up into the score at the end, and a
Type A game levels up (and speeds up) as its line count passes each ten. This feature ports those paths,
plus the Type B scoreboard row printer and the scoring reset, as native free functions. The behavioral
authority is [`../contracts/scoring-system.md`](../contracts/scoring-system.md).

## Design decisions

**Free functions, not a class.** Like the piece and line-clear systems, the scoring system owns no state —
the score, the per-kind counts, the soft-drop totals, the scoreboard state bytes, the level, and the audio
cues all live on already-ported state structs. The functions take the game context by reference and there
is nothing to construct. Two helpers with no other caller (`Call_25D9`'s per-kind step and the soft-drop
drain) are file-local statics.

**The award math is the data layer's; these are the handlers that call it.** `lineClearAward`,
`softDropAward`, and `shouldLevelUp` are pure functions in `src/data/scoring.h`. The scoring handlers apply
them at the right moment with the right gates. The score stays a decimal integer with a 999,999 ceiling;
the original's repeated saturating packed-decimal add equals a single `min(sum, ceiling)` by monotonicity.

**Two new EngineState fields — a corrected contract.** The engine-state contract had claimed the stride-5
stat pad bytes and `wSoftDropPointsBCD` were dead or a derivable scratch. Reading the tally code shows
otherwise: the results screen keeps a separate on-screen count per kind (byte +1 of each block) and a
count-**up** display of drained soft-drop points, and render must re-derive the results screen from sim
state alone. Neither is recoverable from the existing fields, so `EngineState` gains
`scoreboardDisplayedStats` and `softDropPointsTallied`. The per-kind 3-byte score accumulators *are*
derivable and stay uncarried, with the proof in the contract (§5). The engine-state contract is corrected
to match.

- **Rejected — storing the per-kind accumulators too.** They equal
  `base × (typeBLevel + 1) × displayedCount`, which the render bridge computes; carrying them would
  duplicate derivable state.

**The level-up is wired now; the other two paths are not.** `checkForLevelUp` is called from the
line-clear field-wipe stepper at step 16, matching the original. The live award runs in the gameplay
handler beat and the results tally runs in the vertical-blank pass; those beats belong to later units, so
those two functions ship testable but uninstalled.

**One printer signature, not the caller's immediates.** The original's row printer takes a base-score
immediate and a board address; the port takes the line-clear kind (the base is its award-table entry) and
a field cell. Same values, expressed in the port's vocabulary.

**Cues by identity, never magic bytes.** The tally tick and the level-up write `SquareSfxId::CHANGE_SCREEN`
and `SquareSfxId::LEVEL_UP` into the `AudioCues` mailbox — the count blip genuinely reuses the menu cue's
wire value, expressed as the named identity.

## Implementation details

Files:

- `src/systems/scoring.h` / `.cpp` — `kirpich::systems`:
  - `addLineClearScore(GameContext&)` — the Type A live award (first non-empty kind, base × (level + 1)).
  - `updateScoreboard(GameContext&)` — the Type B results count-up, subsuming the per-kind step and the
    soft-drop drain as file-local helpers.
  - `checkForLevelUp(GameContext&)` — the Type A level-up (threshold, level bump, gravity reload, cue).
  - `printLineClearScores(GameContext&, LineClearKind, uint8_t fieldRow, uint8_t fieldCol)` — a Type B
    scoreboard row.
  - `clearScoreAndStats(GameContext&)` — zero the scoring state.
- `src/state/engine_state.h` — adds `scoreboardDisplayedStats` and `softDropPointsTallied`.
- `tests/test_scoring_system.cpp` — seven behavioral cases (see the contract's Tested-by).

Wiring: `src/CMakeLists.txt` adds `systems/scoring.cpp`. `src/systems/line_clear.cpp` includes
`systems/scoring.h` and its field-wipe stepper calls `checkForLevelUp(game)` at step 16 (previously a
recorded seam). No new `GameContext` member, no dispatcher change, no data or action change.

The routines ship as testable functions; only the level-up is installed (via the field wipe). The gameplay
handlers (Type A / Type B), the results handler, and the game-init paths compose the rest when they land.

## Open questions / future work

- **The handlers that call these.** The Type-A gameplay handler runs the live award in its beat; the Type-B
  scoreboard handler drives the results tally each vertical blank (and re-arms its cadence); the game-init
  paths call the reset. Recorded in the contract (§2).
- **Render re-derivation.** The per-kind accumulator, the on-screen count digits, and every `Print*`
  target are render — the render bridge re-derives them from sim state (§5, §9).
- **No hardware trace.** Verification substitutes the hand-traced gate orders, award math, tally cadence,
  threshold vectors, and the reset span for a side-by-side trace against the original, which is not
  possible before the main loop exists. Recorded in the contract.
