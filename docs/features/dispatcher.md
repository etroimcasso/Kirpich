# Game-state dispatcher

**Date:** 2026-08-15
**Status:** Delivered — the state dispatch table, the per-frame beat ordering, the soft-reset chord
detection, and the state aggregate every handler binds against. Ships with zero real handlers: every
state slot holds a not-ported stub, and the handlers fill in as their systems land. The behavioral
authority is [`../contracts/dispatcher.md`](../contracts/dispatcher.md).

## Concept

Every frame of the original is one pass of `MainLoop`: poll the joypad, dispatch through a pointer
table on the current game state to that state's handler, tick the sound driver, check the four-button
soft-reset chord, and decrement two frame timers. Every gameplay, menu, demo, and scene behavior in
the game is a handler that hangs off that table. This feature ports the framework — the dispatch, the
beat ordering, the chord detection, and the state aggregate — so those handlers can be written and
wired in one at a time. It deliberately ships no handlers of its own; it fixes the contract they are
all written against.

The engine's run loop drives the frame timing (one call per sim tick); this unit is one frame's game
logic, not a loop. The class carries the ROM routine's name for traceability, but it is a dispatcher
the run loop calls, not a loop of its own.

## Design decisions

**The dispatcher is a class; the game context is a plain aggregate.** The state a handler reads and
writes — the seven ported state structs plus this tick's joypad snapshot — is one aggregate held by
value, so it is trivially default-constructed to boot and compared in tests. The dispatcher is a
separate object because it owns things the game state is not: the 54-entry handler table, the input
mechanism (the previous-held byte the edge relation needs is per-run mechanism state, not game
state), and the two seams. Keeping them apart keeps the context a pure state image.

- **Rejected — folding the input mechanism into the context.** The previous-held byte is not game
  state; the original keeps it in engine-owned HRAM the input bridge manages, and the port keeps it
  on the dispatcher for the same reason.

**Handlers are `std::function` slots.** Tests install capturing probes; later systems install free
functions or lambdas that close over their own systems; and the two seams already need
`std::function`. One vocabulary throughout, at the cost of an indirection per dispatch that is
irrelevant at frame cadence.

**Unported states sit in place, they do not crash.** Every slot starts as a stub that leaves the
context untouched. An unimplemented state therefore holds rather than transitioning or faulting —
correct for incremental bring-up, and identical to the one already-bare handler in the original
(`GameState_09` is a bare return). The alternative — asserting or transitioning on an unhandled state
— would make the partially-ported game unrunnable for no benefit.

**The chord is detected here; the reset is composed elsewhere.** `MainLoop` only detects the
four-button soft-reset chord and jumps to the reset routine. The reset itself is a separate mechanism
(it notably preserves the top-score tables, because it enters below the cold-boot work-RAM clear), so
this unit fires a seam and the boot path installs the real reset. Splitting detection from action
keeps this unit free of boot-path concerns.

**The two frame beats with no native counterpart are dropped.** `MainLoop` also rewrites the serial
interrupt-enable in multiplayer and busy-waits for the frame boundary. The first belongs to the
serial system; the second is the engine run loop's frame pacing and has no observable native form.
Both are recorded in the contract and absent from the per-tick body.

**Start and Select join the action vocabulary.** The chord needs them, and this is the first consumer
that does. They bind to Enter / Right Shift on the keyboard (the emulator convention) and to the pad's
own Start / Select buttons. The action enum grows by an enumerated update the input layer already
anticipated; the default bindings and the held-action walk grow with it.

## Implementation details

Files:

- `src/systems/game_context.h` — `kirpich::systems::GameContext`: the seven state structs
  (`EngineState`, `GameFlowState`, `PlayingFieldState`, `SpriteRendererState`, `MultiplayerState`,
  `DemoState`, `HighScoreState`) plus `JoypadState`, all by value, with a `reset()` (whole-image cold
  boot) and defaulted `operator==`.
- `src/systems/game_state_dispatcher.h` / `.cpp` — `kirpich::systems::GameStateDispatcher`:
  - `kGameStateCount` (54) — the dispatch domain; the over-read 55th table slot is not modelled.
  - `setHandler(GameState, Handler)` — install a state's handler.
  - `tick(GameContext&, retropp::ActionSet held)` — one frame: sample → dispatch (index read once) →
    audio → chord → timers.
  - `reset()` — return the input mechanism to boot; the table and seams persist.
  - `audioTick` / `softReset` — the two seams, with safe defaults (a silent audio no-op and a warning
    soft reset) until their systems install the real behavior.
- `include/kirpich/action.h` — adds `Start` and `Select`.
- `src/systems/input.cpp` — `heldActions` and `defaultActionMap` grow by the two new actions.
- `tests/test_game_state_dispatcher.cpp` — six behavioral cases (the dispatch sweep, transition
  semantics, beat ordering, the chord vectors, the timer law, the new action rows).

Wiring: `src/CMakeLists.txt` adds `systems/game_state_dispatcher.cpp` to the port library
(`game_context.h` is header-only). The dispatcher is not yet driven by anything — the run loop wires
`tick` at startup when the boot path lands.

## Open questions / future work

- **The handlers.** Every state slot is a stub. The piece system, the menus, the gameplay states, the
  demo playback, the multiplayer states, the scenes, and the end screens each install their handlers
  as they land; the two seams (audio tick, soft reset) get their real targets from the audio system
  and the boot path.
- **The run-loop wiring.** `tick` is the per-frame callback; connecting it to the engine run loop is
  boot-path work.
- **No hardware trace.** Verification substitutes the dispatch sweep, the beat ordering, the chord
  vectors, and the timer law for a side-by-side trace against the original, which is not possible
  before the handlers exist. Recorded in the contract (§8).
