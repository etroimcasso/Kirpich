# Input

How the game reads input, how the player's controls become the bindings the engine samples, and what
to edit to change either. The behavioral specification — what the original game does, line by line —
is in [`../contracts/input.md`](../contracts/input.md); the design rationale is in
[`../features/input-layer.md`](../features/input-layer.md) and
[`../features/controls.md`](../features/controls.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/input.h` / `.cpp` | The `kirpich::systems` input surface — `JoypadState`, `InputSystem`, `keyRepeatFire` and its constants, `heldActions`, `defaultActionMap`. |
| `src/state/controls.h` / `.cpp` | The player's controls — `GbButton`, `ButtonBinding`, `Controls`, `kDefaultControls`, `kCancelKey` — and their save document. |
| `src/systems/controls.h` / `.cpp` | What a Game Boy button means to the game — `actionsFor`, `actionMapFor`, `actionsOnKey` — and the two rebinding calls, `assignKey` and `assignPad`. |
| `src/systems/controls_screen.h` / `.cpp` | The Controls screen's logic: the press capture, the binding, and the release guard. |
| `include/kirpich/action.h` | The `Action` enum — the game's input vocabulary: the five piece-control actions, Start and Select, and the six menu actions. |
| `tests/test_input.cpp`, `tests/test_controls.cpp`, `tests/test_controls_screen.cpp` | The behavioral tests. |

The engine (Polyrhythm) owns physical polling, debounce, and per-tick sampling; it delivers input as
action state keyed by the game's own `Action` enum. This layer turns that per-tick state into the
held/pressed pair the game logic reads and hosts the shared key-repeat core.

## The surface

```cpp
namespace kirpich::systems {

// One frame's snapshot: the actions held this tick and the subset newly pressed since the last tick.
struct JoypadState { retropp::ActionSet held; retropp::ActionSet pressed; };

// Derives the snapshot from a held-action set: pressed = held & ~previouslyHeld, then stores the new
// previous. The live path calls sample(); demo playback derives its own edge against the recording.
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

// The action map for the default controls: actionMapFor(kDefaultControls).
retropp::ActionMap defaultActionMap();

}
```

## The controls

A binding is held per **Game Boy button**, not per action: each of the eight buttons has one keyboard
key and one controller button.

```cpp
namespace kirpich {

enum class GbButton : std::uint8_t { UP, DOWN, LEFT, RIGHT, A, B, START, SELECT };

inline constexpr std::size_t kGbButtonCount = 8;

struct ButtonBinding { SDL_Scancode key; retropp::PadButton pad; };   // defaulted ==
struct Controls {
    std::array<ButtonBinding, kGbButtonCount> buttons;   // in GbButton order; defaulted ==
    ButtonBinding&       operator[](GbButton);
    const ButtonBinding& operator[](GbButton) const;
};

inline constexpr Controls     kDefaultControls;                         // the table below
inline constexpr SDL_Scancode kCancelKey = SDL_SCANCODE_ESCAPE;         // never bindable

inline constexpr std::uint32_t kControlsSchemaVersion = 1;
inline constexpr std::size_t   kControlsRecordBytes   = 3;
inline constexpr std::size_t   kControlsImageBytes    = 24;

std::array<std::uint8_t, kControlsImageBytes> encodeControls(const Controls&);
bool decodeControls(std::span<const std::uint8_t> image, Controls&);   // false, untouched, if unusable
bool saveControls(const Controls&, retropp::SaveStore&);               // the atomic write's result
bool loadControls(retropp::SaveStore&, Controls&);   // false and untouched when absent or unusable

}

namespace kirpich::systems {

std::span<const Action> actionsFor(GbButton);                // the button's actions
retropp::ActionMap      actionMapFor(const Controls&);       // the map the engine samples
retropp::ActionSet      actionsOnKey(const Controls&, SDL_Scancode);  // the actions behind one key

bool assignKey(Controls&, GbButton, SDL_Scancode);                        // false for kCancelKey
bool assignPad(Controls&, GbButton, retropp::PadButton position, retropp::ControllerType family);

}
```

Each button stands for a fixed set of actions — `actionsFor` is the one table:

| Button | Actions |
|---|---|
| Up | `MenuUp` |
| Down | `SoftDrop`, `MenuDown` |
| Left | `MoveLeft`, `MenuLeft` |
| Right | `MoveRight`, `MenuRight` |
| A | `RotateClockwise`, `Confirm` |
| B | `RotateCounterClockwise`, `Back` |
| Start | `Start` |
| Select | `Select` |

`actionMapFor` binds each button's key and controller button to every action in its row, so a rebound
button carries its gameplay action and its menu action together — 26 rows for eight buttons.

`kDefaultControls`:

| Button | Key | Controller |
|---|---|---|
| Up / Down / Left / Right | the arrow keys | `DpadUp` / `DpadDown` / `DpadLeft` / `DpadRight` |
| A | `SDL_SCANCODE_X` | `FaceLabelA` |
| B | `SDL_SCANCODE_Z` | `FaceLabelB` |
| Start | `SDL_SCANCODE_RETURN` | `Start` |
| Select | `SDL_SCANCODE_BACKSPACE` | `Select` |

`defaultActionMap()` is `actionMapFor(kDefaultControls)`.

**Startup.** The host loads the controls beside the settings and hands the derived map to the platform:

```cpp
kirpich::Controls controls = kirpich::kDefaultControls;
kirpich::loadControls(saves, controls);
platform.actions(kirpich::systems::actionMapFor(controls));
```

A change to the controls is a new map handed over the same way; the engine applies it at its next
event pump.

**Rebinding.** `assignKey` and `assignPad` keep every key and every controller button standing for one
Game Boy button at most: taking a source another button holds **swaps** the two. `assignKey` refuses
`kCancelKey` and leaves the controls unchanged.

`assignPad` takes a press as the engine captures it — a **position** (`FaceEast`, never `FaceLabelA`)
and the family of the pad it came from. The defaults name the two face buttons by their printed letter,
and a letter sits in different places on different pads, so before binding, every lettered button in
the controls is replaced by the position it has on that pad. A rebound set names positions throughout
and means the same physical buttons on every pad. A lettered `position` is refused.

**The save document.** `"controls"`, **version 1**, 24 bytes: eight records in `GbButton` order, each
the key as a little-endian 16-bit value and then the controller button. Unlike the settings, a short
or otherwise unusable image is refused whole — wrong length, a key outside SDL's range or equal to the
cancel key, a controller button the engine does not name, or a key or button bound twice — because a
button left without a binding is a button the player cannot press. The loader logs, keeps the
defaults, and leaves the file where it is.

**The fullscreen chord.** Alt+Enter (Cmd+Enter on macOS) is read outside the action map. While its keys
are down the host withholds `actionsOnKey(controls, SDL_SCANCODE_RETURN)`, so the chord never also
presses whichever Game Boy button Enter is bound to.

**The Controls screen** is where a player rebinds (`src/systems/controls_screen.h`; the screen itself
is in [`settings.md`](settings.md#the-controls-screen)). It gets the press to bind from the engine's
capture rather than from the action map, because the press it wants may be bound to nothing:

```cpp
.listen   = [&] { platform.captureRequest(); },      // arm: the next press becomes the answer
.captured = [&] { return platform.capturedSource(); },  // the press, once it arrives
.apply    = [&](const Controls& c) { platform.actions(kirpich::systems::actionMapFor(c)); },
```

A captured key goes to `assignKey`, a captured controller button to `assignPad` with
`CapturedSource::device.family`, and Escape cancels; a press of the kind the chosen column cannot take
asks for another press, since the engine keeps its answer until it is asked again.

**A rebind changes what a held key means mid-press.** The pressed edge is per action, so after a
binding hands over a new map, a key still held from the binding reads on the next tick as a fresh
press of whatever it is now bound to. The screen ignores input after every binding, and after a
confirmed restore of the defaults, until nothing is held. Anything else that rebinds while a key may be down has the same exposure.

## Using the snapshot

Each tick, read the per-tick input state into a held set and turn it into the snapshot:

```cpp
kirpich::systems::InputSystem input;
// once per sim tick, given the engine's InputState `in`:
const auto snapshot = input.sample(kirpich::systems::heldActions(in));
if (snapshot.pressed.test(retropp::actionId(kirpich::Action::RotateClockwise))) { /* … */ }
```

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
- **The default controls** are `kDefaultControls` in `src/state/controls.h`. `tests/test_input.cpp`
  and `tests/test_controls.cpp` pin the map they produce, so change the defaults and those tests
  together.
- **What a button does** is its row in `actionsFor` (`src/systems/controls.cpp`).
- **A new action** is an enumerator in `include/kirpich/action.h`; add it to the `heldActions` walk and
  to the `actionsFor` row of the button that should press it.
- **A new controls field** changes the save document's shape: raise `kControlsSchemaVersion`, register
  a migration from the previous version in `loadControls`, and update `encodeControls` /
  `decodeControls` together.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^(Input|Controls|ControlsScreen)\.'
```

The tests are device-free except the controls' store case, which writes to a temporary directory: the
edge relation, the key-repeat core and the binding laws are pure, and the engine input state is
exercised by synthesizing a sample and feeding it through the engine's documented test seam.
