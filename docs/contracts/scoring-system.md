# Contract — scoring system

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routines:** `AddLineClearScore` (`tetris.asm:4992-5037`), `UpdateScoreboard` (`:4880-4907`)
with `Call_25D9` (`:6109-6189`) and `tallySoftDropPoints` (`:4844-4878`), `Call_244B` (the level-up
check, `:5825-5876`), `PrintLineClearScores` (`:4662-4706`), `ClearScoreAndStats` (`:6191-6205`).
**Supporting routines:** `LookupGravity` (`:4240-4260`), `GameState_00` (the gameplay handler beat,
`:4408-4421`), `GameState_0B` (the Type B scoreboard handler, `:4708-4716`), `AddBCD` (the saturating
packed-decimal add).

This document is the behavioral authority the port's tests are written against. It describes what the
original game does; the port reproduces that observable behavior. The award math itself — `lineClearAward`,
`softDropAward`, `shouldLevelUp`, and the tables and ceilings — is the data layer's and is specified in
[`scoring.md`](scoring.md); this contract covers the handlers that call it.

The port implements five free functions in `src/systems/scoring.{h,cpp}` (`kirpich::systems`), plus three
file-local helpers. They own no state — every field lives on the already-ported state structs, and the
functions take the game context by reference.

---

## 1. What the scoring system is

Three award paths turn play into points, each running at a different point in the frame:

1. **The Type A live award** (`addLineClearScore`) folds a finished line clear into the score as the
   field wipes.
2. **The Type B results tally** (`updateScoreboard`) is the end-of-round count-up: it drains the per-kind
   clear counts and the accumulated soft-drop points into the score one unit at a time, animated.
3. **The Type A level-up** (`checkForLevelUp`) bumps the level and gravity when the line count passes the
   next ten.

Plus the Type B scoreboard row printer (`printLineClearScores`) and the scoring reset
(`clearScoreAndStats`).

All BCD collapses to decimal port-side, matching the score surface: the running score is a decimal
integer with a 999,999 ceiling, and the original's repeated saturating `AddBCD` equals a single
`min(sum, ceiling)` by monotonicity. Where the original prints digits to the screen or video RAM, that is
render — recorded here, not carried as sim state.

---

## 2. Execution-context law

The three paths run in three beats of the frame, and the wiring of each is owned by different units:

| Path | Beat | Gate | Wired by |
|---|---|---|---|
| `addLineClearScore` | gameplay handler | Type A · normal gameplay · `wipeCounter == 5` | the `GameState_00` handler (`:4419`) — a later unit |
| `updateScoreboard` | vertical-blank | tally phase armed | the vertical-blank tick (`:233`) — a later unit |
| `checkForLevelUp` | vertical-blank (field wipe) | Type A · normal gameplay · below the level cap · threshold | `playingFieldWipeTick` step 16 — **wired here** |

`checkForLevelUp` is the one path this system wires now: the line-clear system's field-wipe stepper calls
it at wipe step 16, matching the original's call right after that step's row copy (`:5710-5718`). The other
two are called by later units; during the results screen the wipe counter is 0, so `updateScoreboard`'s
position relative to the wipe dispatchers is inert.

The Type B tally cadence is co-produced by the `GameState_0B` handler (`:4708-4716`): whenever `timer1 == 0`
it re-arms the tally phase to 1 and sets `timer1 = 5`, without advancing the game state. That handler
belongs to a later unit; the tests reproduce it in a frame harness.

---

## 3. `addLineClearScore` — the Type A live award (`:4992-5037`)

Gates, in order (`:4993-5001`): return unless `gameType == TYPE_A`, `gameState == NORMAL_GAMEPLAY`, and
`wipeCounter == 5`.

Then the stats walk (`:5002-5024`): singles, doubles, triples, tetrises are checked in that order and the
**first non-empty** one is consumed. Consumed means **zeroed, not decremented** (`ld [hl], $00`, `:5024`) —
so a count above one loses its extra (unreachable in practice; one lock adds one). The award is
`base × (level + 1)`, applied to the score with the ceiling (`:5025-5037`):

```
score = min(score + lineClearAward(kind, level), 999999)
```

If all four counts are empty, nothing is written.

**Which flag the award sets.** The award becomes visible through the wipe-17/18 print path, whose gate
is `$FFE0` — set inside `AddBCD` (`:187-188`) — and not through `$C0CE` (`scoreRedrawRequested`), which
only the soft-drop award and the vertical-blank clear touch. The port has no `AddBCD`, so the award
sets `flow.scorePrintFlag` itself; see [`readouts.md`](readouts.md) §3.

---

## 4. `updateScoreboard` — the Type B results tally (`:4880-4907` + `Call_25D9` + `tallySoftDropPoints`)

Gate: return if `scoreboardTallyPhase == 0` (`:4881-4883`).

Selector on `scoreboardState` (`:4884-4906`): state 4 drains the soft-drop points (§4b); states 0–3 tally
singles/doubles/triples/tetrises respectively — the state IS the kind, in the same order the per-kind
counters sit in RAM. State 5 is unreachable here: the terminal transition (§4a) fires first.

### 4a. The per-kind step (`scoreboardTallyStep`, from `Call_25D9` `:6109-6189`)

Each unit of a kind animates over two vertical-blank passes:

- **Phase 2 — the print pass** (`:6110-6112`, `:6165-6173`): the score redraw is render; the sim effect is
  the count-tick blip (`audioCues.square = CHANGE_SCREEN`, `$02` — the tally tick reuses the menu cue's
  wire value) and disarming the phase (`scoreboardTallyPhase = 0`).
- **Phase 1, count empty — the kind-complete path** (`:6114-6116`): the shared next-state helper (§4c).
- **Phase 1, count non-empty** (`:6117-6163`): move one unit —
  `--stats[kind]`, `++scoreboardDisplayedStats[kind]` — fold its points into the score
  `score = min(score + kLineClearScores[kind].points × (typeBLevel + 1), 999999)`, and arm the phase-2
  print pass (`scoreboardTallyPhase = 2`). The per-kind score accumulator and the on-screen count digits
  the original also writes are render-derived (§5).

### 4b. The soft-drop drain (`tallySoftDropPoints` `:4844-4878`)

Phase is disarmed **first** (`:4845-4846`). Then:

- `softDropPoints == 0` → the shared kind-complete path (§4c). The original reaches it via the upstream
  `jp Call_25D9._nextState` (commented `; What? Bug`): the jump enters below the routine's `pop de`, which
  is correct for a jumped-not-called entry, and lands the same complete sequence. Ported as a shared
  helper call.
- otherwise one point per call (`:4855-4877`): `--softDropPoints`, `++softDropPointsTallied`,
  `timer1 = 0` ("speed this one up" — with the phase already 0, `GameState_0B` re-arms every frame, so the
  drain runs one point per frame), `score = min(score + 1, 999999)`, and `audioCues.square = CHANGE_SCREEN`.
  The two inline prints are render.

### 4c. The shared kind-complete path (`Call_25D9._nextState` `:6177-6189`)

`timer1 = 33`, `scoreboardTallyPhase = 0`, `++scoreboardState`; if that reaches 5,
`gameState = GAME_OVER_SCREEN`.

### Cadence

`GameState_0B` re-arms phase 1 and `timer1 = 5` whenever `timer1 == 0`, so each per-kind unit costs a
phase-1 vertical blank plus a phase-2 vertical blank, then waits out the 5-frame timer; between kinds the
33-frame timer runs; the soft-drop drain zeroes the timer so it moves one point per frame.

---

## 5. The render-derived per-kind accumulator (derivability proof)

The original keeps, per kind, a 3-byte BCD running score in the stat block's bytes +2..+4 (`:6136-6141`),
printed at `:6143-6149`. The port does **not** store it. It equals exactly

```
perKindScore(kind) = kLineClearScores[kind].points × (typeBLevel + 1) × scoreboardDisplayedStats[kind]
```

because the accumulator receives exactly `scoreboardDisplayedStats[kind]` repetitions of the
`(typeBLevel + 1)`-fold base add, and the display count rises in lockstep with each add (§4a). Its `AddBCD`
saturation is unreachable: the worst legal Type B round is on the order of seven tetrises at level 9,
`7 × 1200 × 10 = 84 000 ≪ 999 999`. The render bridge re-derives the printed value from the display count.
This is why `scoreboardDisplayedStats` is carried as state (it is not otherwise recoverable) while the
accumulator is not (it is). See [`engine-state.md`](engine-state.md).

---

## 6. `checkForLevelUp` — the Type A level-up (`Call_244B` `:5825-5876`)

Gates, in order (`:5826-5835`): return unless `gameState == NORMAL_GAMEPLAY` and `gameType == TYPE_A`, and
return if `level == 20` (the cap). The gates live inside the function because wipe step 16 also fires during
non-gameplay wipes (e.g. the game-over field fill); the state gate keeps those inert.

The threshold is the data layer's `shouldLevelUp(lines, level)` — the original's BCD digit compare
(`Call_249D` + the `$9F`/`hLines` nibble assembly + `cp b`, `:5836-5851`), including the 1000-line cutoff
past which the game never levels again. One level per call:

```
++level
audioCues.square = LEVEL_UP           ; $08, :5873-5874
framesPerDrop = dropTimer = framesPerDrop(level, heartMode != 0)   ; LookupGravity, :5875 / :4258-4259
```

`LookupGravity` writes both the countdown and its reload value. The level-digit prints to both tilemaps
(`:5853-5870`) are render. The heart-mode shift rides the gravity data function; its *activation* belongs
to a later unit, and the reload here is a faithful pass-through of `heartMode`.

---

## 7. `printLineClearScores` — the Type B scoreboard rows (`:4662-4706`)

The port signature is `(GameContext&, LineClearKind kind, uint8_t fieldRow, uint8_t fieldCol)`. The original
caller (`:4620-4638`) passes the base score as an immediate and a board address; the port passes the kind
(base = `kLineClearScores[kind].points` — the caller's immediates are the same values, a third occurrence of
the award table) and a field cell. The four call sites resolve to field rows 1/4/7/10, column 5, via the
playing-field address relation.

The value is `base × (typeBLevel + 1)` (`:4664-4678`). Its decimal digits are written **left-aligned with
leading zeros suppressed** (`:4680-4706`) into consecutive field cells from `(fieldRow, fieldCol)`, one raw
digit byte (0–9) per cell (the charmap digit identity — `CharTile` `$00`–`$09` are the digits). Cells beyond
the last digit are untouched. A zero value writes nothing (unreachable for the real award bases).

**The scratch clobber is dropped.** The original computes the value in `wScore` and its caller zeroes
`wScore` right after the four calls (`:4639-4645`). The port computes locally and leaves the score
untouched; the handler beat is atomic in the port's tick model, so a non-clobbering computation is
observation-equivalent. The zeroing itself belongs to the handler that hosts the four calls.

---

## 8. `clearScoreAndStats` (`:6191-6205`)

Zeroes exactly the original's 27-byte span `$C0AC–$C0C6` plus `wScore`: the score, the per-kind stats and
their display counts, the (render-derived) per-kind accumulators, the soft-drop total and its count-up
display, and the two scoreboard state bytes. **Not** touched: `blockSoftDropAfterLock` (`$C0C7`) and
`scoreRedrawRequested` (`$C0CE`) — the span stops at `$C0C6`. Called from the game-init paths (`:536`,
`:1232`, `:3887`, `:4136`) that later units wire.

---

## 9. State-field resolution

| ROM byte | Port field |
|---|---|
| `wScore` (3 B BCD) | `engine.score` (decimal, 999,999 ceiling) |
| `wSinglesCount..wTetrisCount` (+0 of each stride-5 block) | `engine.stats` |
| `$C0AD/$C0B2/$C0B7/$C0BC` (+1 of each block) | `engine.scoreboardDisplayedStats` |
| per-kind BCD accumulators (+2..+4) | render-derived (§5) |
| `wSoftDropPoints` | `engine.softDropPoints` |
| `wSoftDropPointsBCD` (`$C0C2`) | `engine.softDropPointsTallied` |
| `wScoreboardState` (`$C0C5`) | `engine.scoreboardState` |
| `$C0C6` | `engine.scoreboardTallyPhase` |
| `hLevel` (`$FFA9`) | `flow.level` |
| `hLines` (`$FF9E`) | `flow.lines` |
| `hTypeBLevel` (`$FFC3`) | `flow.typeBLevel` |
| `hDropTimer`/`hFramesPerDrop` (`$FF99`/`$FF9A`) | `flow.dropTimer` / `flow.framesPerDrop` |
| `hTimer1` (`$FFA6`) | `flow.timer1` |
| `hWipeCounter` (`$FFE3`) | `flow.wipeCounter` |
| `hGameType`/`hGameState`/`hHeartMode` | `flow.gameType` / `flow.gameState` / `flow.heartMode` |
| `$C802`-page rows (score digits) | `field.board` via `fieldCell` (§7) |
| `$FFE0` print gate | `flow.scorePrintFlag` (see [`readouts.md`](readouts.md)) |
| `$C0CE`+`$FF98` redraw gate, all `Print*` targets | `engine.scoreRedrawRequested` + `flow.pieceLockStage`; the prints are [`readouts.md`](readouts.md)'s |
| cue mailboxes | `game.audioCues` |

---

## 10. Preserved quirks

- **First-non-empty award only** (`addLineClearScore`): one kind consumed per call, zeroed not
  decremented.
- **The soft-drop zero-points early-out** jumps into the shared kind-complete path (upstream's
  `; What? Bug`).
- **The `timer1 = 0` drain speed-up**: soft-drop points drain one per frame, not one per five.
- **The tally tick reuses `CHANGE_SCREEN`'s `$02`** wire value for its count blip.

---

## Tested by

`tests/test_scoring_system.cpp` — seven device-free behavioral cases: the live-award gate matrix and
first-non-empty walk with the award math and clamp; the results-tally per-unit mechanics, phase sequence,
score fold, kind-complete hold, and full drive to the game-over transition; the soft-drop drain with the
tallied+remaining invariant, per-frame timer zero, and the zero-points early-out; the level-up gate matrix,
`shouldLevelUp` threshold vectors (cutoff and one-level-per-call), both timers reloaded (heart mode
included), and the cue; the wipe-16 wiring (levels up at step 16 and only there; inert for non-Type-A); the
row printer's four caller vectors with left-alignment and leading-zero suppression; and the reset span with
its survivors. Every value traces to the line anchors above.
