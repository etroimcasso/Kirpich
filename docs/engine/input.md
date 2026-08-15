# Input

How the game reads input, and what to edit to change it. The behavioral specification — what the
original game does, line by line — is in [`../contracts/input.md`](../contracts/input.md); the design
rationale is in [`../features/input-layer.md`](../features/input-layer.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/input.h` / `.cpp` | The `kirpich::systems` input surface — `JoypadState`, `InputSystem`, `keyRepeatFire` and its constants, `heldActions`, `defaultActionMap`. |
| `include/kirpich/action.h` | The `Action` enum — the game's input vocabulary (the five piece-control actions today). |
| `tests/test_input.cpp` | The behavioral tests. |

The engine (Retro++) owns physical polling, debounce, and per-tick sampling; it delivers input as
action state keyed by the game's own `Action` enum. This layer turns that per-tick state into the
held/pressed pair the game logic reads and hosts the shared key-repeat core.

## The surface

```cpp
namespace kirpich::systems {

// One frame's snapshot: the actions held this tick and the subset newly pressed since the last tick.
struct JoypadState { retropp::ActionSet held; retropp::ActionSet pressed; };

// Derives the snapshot from a held-action set: pressed = held & ~previouslyHeld, then stores the new
// previous. Both live input and demo playback call sample().
class InputSystem {
public:
    JoypadState sample(retropp::ActionSet heldNow);  // derive pressed, store prev, return the pair
    void reset();                                    // prev := empty (boot state)
};

// The shared key-repeat (DAS) core: press fires and arms the initial delay; held counts down and
// fires on the repeat interval. Returns true on the frames the action fires. `timer` is the caller's
// countdown byte, passed by reference.
bool keyRepeatFire(std::uint8_t& timer, bool pressed, bool held);
inline constexpr std::uint8_t kKeyRepeatInitialDelay = 23;
inline constexpr std::uint8_t kKeyRepeatRate         = 9;
inline constexpr std::uint8_t kKeyRepeatBlockedRetry = 1;

// Sample every game action's held level from the engine's per-tick input state into an action set —
// the held set the live path feeds sample().
retropp::ActionSet heldActions(const retropp::InputState& in);

// The default keyboard + gamepad bindings for the five piece-control actions.
retropp::ActionMap defaultActionMap();

}
```

**Using it.** Hand the default map to the platform once at startup, then each tick read the per-tick
input state into a held set and turn it into the snapshot:

```cpp
platform.actions(kirpich::systems::defaultActionMap());

kirpich::systems::InputSystem input;
// once per sim tick, given the engine's InputState `in`:
const auto snapshot = input.sample(kirpich::systems::heldActions(in));
if (snapshot.pressed.test(retropp::actionId(kirpich::Action::RotateClockwise))) { /* … */ }
```

For the demo playback, feed the recorded timeline's held set into the *same* `sample` call instead of
`heldActions(in)` — nothing downstream changes.

Drive the key repeat off a caller-owned countdown byte (the game-flow state's `keyRepeatTimer`):

```cpp
if (kirpich::systems::keyRepeatFire(flow.keyRepeatTimer, pressed, held)) { /* shift the piece */ }
```

## Changing behavior

- **The edge relation** — `pressed = held & ~previouslyHeld` — is `InputSystem::sample` in
  `src/systems/input.cpp`. It samples held levels only, so a tap shorter than one tick is dropped, on
  purpose (the engine's never-drop-a-press signal is deliberately not used; see the contract).
- **The key-repeat timing** is the three constants in `src/systems/input.h`, each pinned in the tests
  — change a constant and its test together. The firing rule itself is `keyRepeatFire`. The parts that
  differ per site (the piece shift's idle re-arm and wall-charge retry, each site's direction
  priority) live with those systems, not here — see the contract §4b.
- **The default bindings** are `defaultActionMap()`. Each action is a row of sources; add a source by
  adding to its brace-list. The two rotations bind to the pad's printed A / B so the glyph matches the
  original Game Boy button on every pad family.
- **A new action** is an enumerator in `include/kirpich/action.h`; add it to the `heldActions` walk
  and, if it should be bound by default, to `defaultActionMap()`.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^Input\.'
```

The tests are device-free: the edge relation and the key-repeat core are pure, and the engine input
state is exercised by synthesizing a sample and feeding it through the engine's documented test seam.
