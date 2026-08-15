# Input layer

**Date:** 2026-08-15
**Status:** Delivered — the per-frame joypad snapshot, the shared key-repeat (DAS) core, and the
default button bindings. The sixteen consumer sites (the state dispatcher, the piece system, the
menus, the demo playback, the end screens, name entry) read this layer as they land; they are
recorded in the contract, not yet ported.

## Concept

Every input the game consumes flows through one per-frame mechanism: poll the joypad once, pack the
eight buttons into a held byte, and derive the rising edge (the buttons that went down this frame).
This feature ports that mechanism over the engine's action-input system — the snapshot's shape and
its edge relation — plus the shared auto-repeat core and the default bindings. The behavioral
authority is [`../contracts/input.md`](../contracts/input.md).

## Design decisions

**The bridge derives the edge from held levels; the engine's just-pressed is deliberately unused.**
The engine owns device polling and exposes both held levels and a just-pressed signal. That signal
guarantees a press is never dropped — even a tap shorter than one tick registers. The original does
not do that: it samples button *levels* once per frame, so a sub-tick tap is dropped. Behavior
preservation requires the original's semantics, so the bridge samples held levels only and derives
the edge itself (`pressed = held & ~previouslyHeld`). This also makes the live and demo paths
symmetric — both feed a held set through the same call.

- **Rejected — reading the engine's just-pressed.** It would change the observable behavior: taps the
  original drops would register. The never-drop-a-press property is documented in the contract as
  deliberately unused.

**One call for both input sources.** The original's demo mode substitutes a recorded held byte for
the live one and reuses the same edge derivation (`DemoSimulateJoypad`). `InputSystem::sample` is that
one call: live play feeds the engine-sampled held set, and the demo playback feeds the recorded
timeline's held set through the *same* `sample`. Because the edge is derived inside it, demo input
slots in with no consumer distinguishing it from live input.

**The previous-held is mechanism state, not game state.** The original's `hJoyHeld` / `hJoyPressed`
live in the block of HRAM the game-flow state otherwise owns, but the game-flow contract already
assigns those two bytes to the input bridge. `InputSystem` carries the previous-held internally; it is
not a field of any game-state struct.

**The key-repeat core is shared; the site-specific parts are not.** The auto-repeat discipline (press
fires and arms a long delay; held counts down and fires on a short interval) is identical at the two
sites that use it — the piece shift and name entry. That shared core is `keyRepeatFire`, taking the
countdown byte by reference. The parts that differ — the shift's idle re-arm and wall-charge retry,
each site's direction priority — stay with their owning systems and are recorded in the contract. The
unsigned-byte decrement preserves the original's stale-timer wraparound (a 255-frame delay) verbatim.

**No new action enumerators here.** The consumer census spans all eight original buttons, but each
consuming system mints its own semantic actions when it lands (the soft-reset chord, menu
navigation/confirm, the name-entry cycle). This layer binds and reads the five piece-control actions
that already exist and is generic over the enum; the default bindings and the held-action walk grow
as the enum does.

## Implementation details

Files:

- `src/systems/input.h` / `.cpp` — `kirpich::systems`:
  - `JoypadState { held, pressed }` — one frame's snapshot as action sets.
  - `InputSystem::sample(heldNow)` — derives pressed, stores the new previous, returns the pair;
    `reset()` clears the previous to empty.
  - `keyRepeatFire(timer, pressed, held)` — the shared auto-repeat core; returns true on firing
    frames. Constants `kKeyRepeatInitialDelay` (23), `kKeyRepeatRate` (9), `kKeyRepeatBlockedRetry`
    (1), each line-anchored and test-pinned.
  - `heldActions(const retropp::InputState&)` — samples the per-tick engine input state's held level
    of every game action into an action set (the held set the live path feeds `sample`). The one
    place this layer reads the engine input state.
  - `defaultActionMap()` — the default keyboard + gamepad bindings for the five piece actions.
- `tests/test_input.cpp` — six behavioral cases: the edge relation, the key-repeat timeline, the
  key-repeat edge cases (idle, wall-charge retry, wraparound), the constants, the default bindings,
  and the held-action adapter.

Wiring: `src/CMakeLists.txt` adds `systems/input.cpp` to the port library. No new action enumerators;
`include/kirpich/action.h` is unchanged.

The bridge ships testable values and functions — there is no run loop yet. It is wired into the loop
when the state dispatcher lands and begins reading per-tick action state.

## Open questions / future work

- **The consumer sites.** The state dispatcher (and its soft-reset chord), the piece system's DAS, the
  menus, the demo playback substitution, the end screens, and name entry each read this layer and mint
  the actions they need. Recorded in the contract (§1, §4b, §5, §6).
- **A rebind surface.** The bindings are a value a settings screen can edit and resubmit to the engine
  live; a user-facing rebind UI is outside this layer's fidelity scope.
- **No hardware trace.** Verification substitutes the edge relation, the key-repeat timeline, the
  constants, the bindings, and the adapter for a side-by-side trace against the original, which is not
  possible before the main loop exists. Recorded in the contract (§9).
