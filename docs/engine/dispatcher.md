# Game-state dispatcher

How the game runs one frame: the state dispatch table, the frame beats, and the state aggregate every
handler reads. The behavioral specification — what the original game does, line by line — is in
[`../contracts/dispatcher.md`](../contracts/dispatcher.md); the design rationale is in
[`../features/dispatcher.md`](../features/dispatcher.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/game_state_dispatcher.h` / `.cpp` | The `kirpich::systems::GameStateDispatcher` — the handler table, the `tick` frame body, the `audioTick` / `softReset` seams, and `kGameStateCount`. |
| `src/systems/game_context.h` | The `kirpich::systems::GameContext` aggregate — the seven state structs plus the joypad snapshot every handler reads and writes. |
| `include/kirpich/game_state.h` | The `GameState` enum — the 54 states the table dispatches on. |
| `tests/test_game_state_dispatcher.cpp` | The behavioral tests. |

The engine (Retro++) run loop calls `tick` once per sim tick; each call is one frame of the game. The
dispatcher itself holds no loop — frame pacing is the engine's.

## The surface

```cpp
namespace kirpich::systems {

// The game's whole in-memory image: the seven ported state structs plus this tick's joypad snapshot.
struct GameContext {
    EngineState engine; GameFlowState flow; PlayingFieldState field;
    SpriteRendererState spriteRenderer; MultiplayerState multiplayer;
    DemoState demo; HighScoreState highScores;
    JoypadState joypad;
    void reset();  // whole-image cold boot (each member to its own boot state)
};

inline constexpr std::size_t kGameStateCount = 54;  // the dispatch domain, states $00–$35

class GameStateDispatcher {
public:
    using Handler = std::function<void(GameContext&)>;

    GameStateDispatcher();                              // every slot a stub; default seams installed
    void setHandler(GameState state, Handler handler);  // install a state's handler
    void tick(GameContext& game, retropp::ActionSet held);  // one frame
    void reset();                                       // input mechanism to boot; table/seams persist

    std::function<void()> audioTick;   // advance the sound driver (default: no-op)
    std::function<void()> softReset;   // perform the soft reset (default: a warning)
};

}
```

`tick` runs five beats in order: sample the joypad into `game.joypad`, dispatch to the current state's
handler, call `audioTick`, check the soft-reset chord, decrement the two frame timers. The dispatch
index (`game.flow.gameState`) is read once, before the handler runs, so a handler that writes a new
state transitions on the next tick. The chord — Start + Select + `RotateClockwise` +
`RotateCounterClockwise` all held — calls `softReset` and ends the tick before the timers; extra held
actions do not block it.

**Using it.** Install the handlers a state needs, install the two seams, then call `tick` each sim
tick with the frame's held-action set:

```cpp
kirpich::systems::GameStateDispatcher dispatcher;
kirpich::systems::GameContext game;

dispatcher.setHandler(kirpich::GameState::TITLE_SCREEN, [](kirpich::systems::GameContext& g) { /* … */ });
dispatcher.audioTick = [] { /* tick the sound driver */ };
dispatcher.softReset = [] { /* reset the machine */ };

// once per sim tick, given the frame's held actions:
dispatcher.tick(game, kirpich::systems::heldActions(inputState));
```

A slot with no handler installed holds a stub that leaves `game` untouched, so an unimplemented state
sits in place rather than crashing.

## Changing behavior

- **Add a state's behavior** — call `setHandler(state, fn)`. The handler takes `GameContext&`, runs
  one frame of that state, and may write `game.flow.gameState` to transition next tick.
- **The frame beats** are the body of `tick` in `src/systems/game_state_dispatcher.cpp`, in order.
  The chord buttons are the four `test` calls there; the timers are `game.flow.timer1` / `timer2`,
  each decremented if non-zero (a zero timer stays zero).
- **The soft reset and the audio tick** are the `softReset` and `audioTick` members — assign the real
  behavior to replace the defaults. `softReset` runs on a chord tick; `audioTick` runs every tick.
- **What a handler can reach** is the members of `GameContext` in `src/systems/game_context.h`. A
  handler that needs an engine surface beyond the joypad snapshot (a renderer, the audio device, the
  virtual machine) receives it separately — the context is state only.
- **A new bound action** (for a future chord or menu) is an enumerator in `include/kirpich/action.h`,
  added to the `heldActions` walk and, if bound by default, to `defaultActionMap()` — see
  [`input.md`](input.md).

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^GameStateDispatcher\.'
```

The tests are device-free: the dispatcher is pure logic over the handler table and the state
aggregate, and handlers are test probes.
