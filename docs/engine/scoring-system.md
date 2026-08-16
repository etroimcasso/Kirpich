# Scoring system

How play turns into points: the Type A live line-clear award, the Type B end-of-round count-up, the
Type A level-up, the Type B scoreboard row printer, and the scoring reset. The behavioral specification —
what the original game does, line by line — is in
[`../contracts/scoring-system.md`](../contracts/scoring-system.md); the design rationale is in
[`../features/scoring-system.md`](../features/scoring-system.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/scoring.h` / `.cpp` | The `kirpich::systems` scoring functions and their file-local helpers. |
| `src/state/engine_state.h` | `scoreboardDisplayedStats` and `softDropPointsTallied`, the two results-screen count-up fields. |
| `tests/test_scoring_system.cpp` | The behavioral tests. |

The functions take a `GameContext&` (`src/systems/game_context.h`) and read or write through it. They own
no state: the score, the per-kind clear stats and their display counts, and the soft-drop totals live on
`EngineState`; the level, line count, timers, and game-type/state on `GameFlowState`; the board on
`PlayingFieldState`; and the audio cues on the `AudioCues` member. The award math they call
(`lineClearAward`, `softDropAward`, `shouldLevelUp`) lives in `src/data/scoring.h` (see
[`scoring.md`](scoring.md)).

## The surface

```cpp
namespace kirpich::systems {

// Type A live award: consume the first non-empty per-kind clear stat and add base x (level + 1) to the
// score. Does nothing unless a Type A game is in normal gameplay at wipe step 5.
void addLineClearScore(GameContext& game);

// One vertical-blank step of the Type B results count-up: drain one unit of a per-kind count or one
// soft-drop point into the score, animating the tally. Does nothing unless the tally phase is armed.
void updateScoreboard(GameContext& game);

// Type A level-up: when the line count has passed the next ten, advance the level, cue the level-up
// sound, and reload the gravity countdown. One level per call. The field wipe calls this at step 16.
void checkForLevelUp(GameContext& game);

// Print one line-clear kind's Type B scoreboard row: base x (typeBLevel + 1) as decimal digits,
// left-aligned with leading zeros suppressed, one raw digit byte per field cell from (fieldRow, fieldCol).
void printLineClearScores(GameContext& game, LineClearKind kind, std::uint8_t fieldRow,
                          std::uint8_t fieldCol);

// Zero the whole scoring state (score, stats, display counts, soft-drop totals, scoreboard state bytes).
// Leaves the post-lock soft-drop latch and the score-redraw flag alone.
void clearScoreAndStats(GameContext& game);

}
```

**Using them.** The level-up is already wired: the line-clear field-wipe stepper
([`line-clear.md`](line-clear.md)) calls `checkForLevelUp(game)` at step 16, so a Type A game speeds up as
it crosses each ten lines with no extra wiring. The other paths are driven by their handlers when they
land:

```cpp
// In the Type A gameplay handler beat, after the piece and line-clear calls:
kirpich::systems::addLineClearScore(game);   // no-op unless wipeCounter == 5

// In the Type B results handler, once per vertical blank while the tally runs:
kirpich::systems::updateScoreboard(game);    // no-op unless the tally phase is armed
```

`updateScoreboard`'s cadence is co-driven by the Type B scoreboard handler, which re-arms the tally phase
and reloads a 5-frame timer whenever the timer expires. To print a Type B scoreboard row, or to reset the
scoring state at game start:

```cpp
kirpich::systems::printLineClearScores(game, kirpich::LineClearKind::TETRIS, /*row=*/10, /*col=*/5);
kirpich::systems::clearScoreAndStats(game);
```

## Gotchas

- **`addLineClearScore` fires only at wipe step 5.** It gates on `gameType == TYPE_A`,
  `gameState == NORMAL_GAMEPLAY`, and `wipeCounter == 5`; call it every frame in the handler beat and it
  is inert except on that one wipe step. It consumes the **first** non-empty kind (single before double
  before triple before tetris), zeroing that count.
- **The results tally needs its cadence handler.** `updateScoreboard` does nothing unless
  `engine.scoreboardTallyPhase` is armed, and it is the Type B scoreboard handler (not this function) that
  arms it each cycle. In isolation, arm the phase yourself or drive it through the frame harness the tests
  use.
- **`checkForLevelUp` is inert off the gameplay path.** Its own gates keep it a no-op during the
  non-gameplay wipes that also reach step 16 (the game-over field fill, for one), so wiring it into the
  wipe is safe.
- **Two EngineState fields feed render, not sim reads.** `scoreboardDisplayedStats` and
  `softDropPointsTallied` are the count-up displays the results screen shows; nothing reads them back for a
  decision. The per-kind score accumulators the original also keeps are **not** carried — render
  re-derives them (contract §5).
- **Digits are raw bytes 0–9.** `printLineClearScores` writes the charmap digit identity (`CharTile`
  `$00`–`$09`), not encoded glyphs, and leaves cells past the last digit untouched.

## Changing behavior

- **The award gates and the first-non-empty walk** are `addLineClearScore`; the award value is
  `lineClearAward` in `src/data/scoring.h`.
- **The tally state machine** — the per-kind step, the soft-drop drain, the phase 1/2 animation, and the
  terminal transition — is `updateScoreboard` and its file-local helpers `scoreboardTallyStep`,
  `tallySoftDropPoints`, and `scoreboardNextState`.
- **The level-up law** is `checkForLevelUp`: the gates, `shouldLevelUp` (in `src/data/scoring.h`), the
  level bump, and the gravity reload via `framesPerDrop` (see [`gravity.md`](gravity.md)). The wiring point
  is step 16 of `playingFieldWipeTick` in `src/systems/line_clear.cpp`.
- **The row printer's digit layout** is `printLineClearScores`: left-aligned, leading-zero-suppressed raw
  digits into consecutive field cells.
- **The reset span** is `clearScoreAndStats`: exactly the score and the scoring state bytes, leaving the
  post-lock latch and the redraw flag.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^ScoringSystem\.'
```

