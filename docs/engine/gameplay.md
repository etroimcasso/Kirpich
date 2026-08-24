# Gameplay session

The states a round passes through from setup to game over: the shared init, the per-frame gameplay
loop, the pause, the game-over chain, and the Type B results re-arm. The behavioral specification —
what the original game does, line by line — is in
[`../contracts/gameplay.md`](../contracts/gameplay.md); the design rationale is in
[`../features/gameplay.md`](../features/gameplay.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/gameplay.h` / `.cpp` | The `kirpich::systems` gameplay handlers, the pause family, and their file-local helpers. |
| `src/systems/audio_cues.h` | `AudioPauseCommand` and the `pause` mailbox the pause path writes. |
| `tests/test_gameplay.cpp` | The behavioral tests. |

The functions take a `GameContext&` (`src/systems/game_context.h`) and read or write through it. They
own no state: the board lives on `PlayingFieldState`, the piece sprites on `SpriteRendererState`, the
score and preview flag on `EngineState`, the timers, level, line count, and selections on
`GameFlowState`, the link-cable buffers on `MultiplayerState`, and the audio cues on the `AudioCues`
member.

One init serves every mode. A Type A round, a Type B round, a Type C round and an attract demo all run
`initGame` and `normalGameplay`, forking internally on the game type — the starting level, the line
count, the backdrop and the mode's own opening work (Type B's starting garbage, Type C's rise counter)
all come out of that fork. A two-player round runs its own gameplay state and shares only the pause
family below.

## The surface

```cpp
namespace kirpich::systems {

// The four steps the gameplay frame calls where the original handles a demo. Each does nothing during
// ordinary play, so every default is empty and an empty hook is skipped.
struct GameplayDemoHooks {
    std::function<void(GameContext&)> checkForEndOfDemo;
    std::function<void(GameContext&)> simulateJoypad;
    std::function<void(GameContext&)> recordDemo;
    std::function<void(GameContext&)> restoreSavedJoypad;
};

// Fill `rows` rows of starting garbage, from the fixed demo table when `useDemoTable` is set.
using InitGarbageHook = std::function<void(GameContext& game, std::uint8_t rows, bool useDemoTable)>;

// Fired by the Start+Select+B+A chord. The frame dispatcher carries the same seam.
using SoftResetHook = std::function<void()>;

// Everything the handlers need from systems other than this one. `draw` is required; the rest are inert
// by default.
struct GameplayWiring {
    std::function<std::uint8_t()> draw;
    GameplayDemoHooks             demo{};
    InitGarbageHook               initGarbage{};
    SoftResetHook                 softReset{};
};

// Set a round up: clear the entry state, board, and score; pick the starting level and line count for
// the game type; load the gravity period; fill the piece pipeline; lay the Type B starting garbage or
// arm the Type C rise counter.
void initGame(GameContext& game, const std::function<std::uint8_t()>& draw,
              const InitGarbageHook& initGarbage = {});

// One frame of play: handle Start and Select, then (unless paused) run the demo input substitution,
// the piece, the line-clear scan and compaction, and the score award, in that order.
void normalGameplay(GameContext& game, const GameplayDemoHooks& demo = {},
                    const SoftResetHook& softReset = {});

// Start the game-over curtain: hide the piece sprites, clear the line-clear list, fill the field with
// the curtain tile, and arm the curtain timer.
void initGameOver(GameContext& game);

// The game-over curtain: cue the game-over music, then either hand off to the two-player end jingle or
// paint the solo game-over screen and pick its ending.
void gameOverCurtain(GameContext& game);

// The game-over screen: wait for A or Start, then return to the difficulty screen the round came from.
void gameOverScreen(GameContext& game);

// Re-arm one unit of the Type B results count-up.
void initTypeBScoreboard(GameContext& game);

// Waits for any button and advances to the first bonus-ending scene. Unreachable in play.
void state0CUnknown(GameContext& game);

// The soft-reset chord, then Start to pause or unpause and Select to toggle the preview piece.
void handleStartSelect(GameContext& game, const SoftResetHook& softReset = {});

// The two-player unpause protocol. Returns true when the caller must return immediately.
[[nodiscard]] bool handlePausedMultiplayer(GameContext& game);

// Start a field wipe, then fill every visible field cell with `fill`.
void fillPlayingFieldAndWipe(GameContext& game, std::uint8_t fill);

void installGameplayHandlers(GameStateDispatcher& dispatcher, GameplayWiring wiring);

}  // namespace kirpich::systems
```

## Installing the handlers

`installGameplayHandlers` puts the seven handlers in their dispatch slots. It takes a `GameplayWiring`
because the piece pipeline cannot run without a randomizer — `draw` is required; everything else
defaults to inert:

```cpp
// registerPieceRandom hosts the draw core on the virtual machine and hands back a callable
// (see piece-random.md); it converts straight into the wiring's std::function.
const auto draw = kirpich::vm::registerPieceRandom(vm);

kirpich::systems::GameplayWiring wiring;
wiring.draw = draw;
kirpich::systems::installGameplayHandlers(dispatcher, wiring);
```

Leaving `initGarbage` unset starts every Type B round on an empty field; leaving the `demo` hooks unset
makes the four demo steps no-ops, which is what an ordinary round wants anyway.

## The frame

`normalGameplay` runs twelve steps in a fixed order, and the order is observable:

1. `handleStartSelect` — pause, preview toggle, soft-reset chord
2. stop here if paused
3. check whether the demo has ended *(hook)*
4. substitute the demo's recorded input *(hook)*
5. record input *(hook)*
6. `rotateAndShiftPiece`
7. `dropPiece`
8. `checkForCompletedRows`
9. `lockPieceIntoBackground`
10. `moveBlocksDownAfterLineClear`
11. `addLineClearScore`
12. restore the player's real input *(hook)*

The scan runs before the lock, so a piece is scanned in the position it had on entry to the frame; the
compaction runs before the award, so the award sees the tallies the compaction produced. Steps 6–11 are
the piece, line-clear, and scoring systems — see [`piece-system.md`](piece-system.md),
[`line-clear.md`](line-clear.md), and [`scoring-system.md`](scoring-system.md).

## Pausing

Start pauses and unpauses. Pausing writes `AudioPauseCommand::PAUSE` into the cue mailbox and hides both
piece sprites; unpausing writes `UNPAUSE` and brings the active piece back, plus the preview if the
player has not hidden it with Select. A running demo suppresses all of it — recorded input cannot pause
the game.

The pause family is shared with the two-player round, which calls both `handleStartSelect` and
`handlePausedMultiplayer` each frame. In a two-player game only the master may pause.

## The endings

A Type A round that finishes above 100 000 points earns a rocket ending instead of the plain game-over
screen. `gameOverCurtain` looks the tier up with `rocketSpriteForScore` (see [`scoring.md`](scoring.md)),
stages the sprite, and advances to the bonus scene; below the lowest tier, and in every Type B round, it
goes to the game-over screen.

## Gotchas

- **`fillPlayingFieldAndWipe` arms a wipe as well as filling.** `initGame` disarms it on the next line;
  the two game-over callers let it run. If you add a caller, decide which you want.
- **`handlePausedMultiplayer` returns a flag the caller must honour.** It is `[[nodiscard]]` for that
  reason: ignoring it runs work the original skips.
- **Two behaviours in the two-player unpause look wrong and are deliberate.** The serial-flag test can
  never be taken, and the slave's unpause test reads inverted against the command it names. Both are
  what the original does; the contract explains each. Do not "fix" either without a decision to diverge.
- **`state0CUnknown` cannot be reached.** Nothing writes its dispatch index. It exists because the
  dispatch table has the entry.

## What to change where

| To change | Edit |
|---|---|
| What a round starts with (level, line count, drop timing) | `initGame` in `src/systems/gameplay.cpp` |
| The order of the frame's steps | `normalGameplay` in the same file |
| The pause or preview input law | `handleStartSelect` / `handleSelect` in the same file |
| The curtain, screen, or ending timings | the frame-count constants at the top of the same file |
| Which score earns which rocket | `kBonusEndings` in `src/data/generated/scoring_data.inc` — see [`scoring.md`](scoring.md) |
| Where the game-over text sits on the board | the `printWindowBlock` calls in `gameOverCurtain` |
| The garbage a Type B round starts with | the `InitGarbageHook` the caller installs |
