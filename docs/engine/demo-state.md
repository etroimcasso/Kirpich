# Demo state

The state the attract-mode demo carries between frames while it plays — which demo is running, the
recording flag, the run-length countdown, the cursor into the active input timeline, and two held-button
sets (the demo's own held buttons and the player's real held state parked while the demo drives) — held in
one `DemoState` struct. A single instance carries every byte the demo machinery persists; the demo systems
take a reference to it, read the fields they care about, and write the ones they own.

It is an idiomatic C++ surface, not a byte image of the original's high RAM. Which demo is running is a
typed enum, the two pointer halves that walk the recording collapse into one record index, and the two
held-button bytes are action sets rather than raw joypad bytes. The exact mapping back to the original's
`$FFE4` / `$FFE9`–`$FFEE` addresses, the playback and recording narratives, and the quirks the surface
preserves are the behavioral spec in [`../contracts/demo-state.md`](../contracts/demo-state.md). The
recording data the cursor walks — the two input timelines and the shared piece list — is the demo-data
unit; see [`../contracts/demo.md`](../contracts/demo.md).

Everything is in `namespace kirpich`, header-only, included as `"state/demo_state.h"` (the `src/` tree is on
the library's include path). There is no `.cpp`. It is a sibling of `GameFlowState`
([`game-state-machine-state.md`](game-state-machine-state.md)) and the other state blocks, not a member of
any.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/demo_state.h` | `ActiveDemo`, `DemoState`, and `reset()` | Hand-written. Edit here to add or reshape a field. |
| `tests/fixtures/hram_expected.h` | The original's whole high-RAM layout map and the raw-operand access census | **Generated — do not hand-edit.** Shared with the other high-RAM state units. |

## The types

`ActiveDemo` is which attract-mode demo is running:

```cpp
enum class ActiveDemo : uint8_t {
    NONE   = 0,   // no demo running
    TYPE_B = 1,   // the Type B recording (plays second)
    TYPE_A = 2,   // the Type A recording (plays first)
};
```

The enumerator values carry the game type. The play order is the reverse of the numbering — the game plays
`TYPE_A` (2) first, then `TYPE_B` (1) — which the contract records; the names track the type, not the order.

`DemoState` is the demo block — six fields over the original's seven demo bytes:

```cpp
struct DemoState {
    ActiveDemo activeDemo = ActiveDemo::NONE;   // which demo is running; NONE = none
    uint8_t    recording = 0;                   // 0 = playback; $FF = the (dead) recording path
    uint8_t    framesRemaining = 0;             // frames until the next input record loads
    uint16_t   nextRecord = 0;                  // index of the next record in the active timeline
    retropp::ActionSet demoHeld;                // the demo's currently-held buttons
    retropp::ActionSet savedHeld;               // the player's real held state, parked while the demo drives

    void reset();
    friend bool operator==(const DemoState&, const DemoState&) = default;
};
```

Every member is zero-initialised, so a default-constructed instance is the boot state, and the struct has a
defaulted `==`. `demoHeld` and `savedHeld` are `retropp::ActionSet` — the same type a `DemoInputRecord`
(`src/data/demo.h`) carries — so a held record loads straight into `demoHeld`.

## Using it

```cpp
#include "state/demo_state.h"

kirpich::DemoState demo;   // all-zero: the boot state, no demo running

// A demo is playing when activeDemo is not NONE; the gameplay code gates on this.
if (demo.activeDemo != kirpich::ActiveDemo::NONE) { /* deterministic pieces, suppressed start/select */ }

// The active timeline is chosen by which demo is running; nextRecord indexes into it.
const auto& timeline = demo.activeDemo == kirpich::ActiveDemo::TYPE_A
                           ? kirpich::kTypeADemoInputs
                           : kirpich::kTypeBDemoInputs;
const kirpich::DemoInputRecord& record = timeline[demo.nextRecord];

// Return everything to the boot state (e.g. when a real game starts).
demo.reset();
```

`reset()` restores every field to its default (all-zero) value; it is equivalent to assigning a fresh
`DemoState{}`.

## Gotchas

- **`recording` is a `uint8_t`, not a `bool`.** Its enable value is `$FF` (the `kDemoRecordingEnabledMagic`
  constant from the misc data), not 1, and the original's consumers split — two compare `== $FF`, one tests
  any non-zero — so a `bool` would drop information. In the shipped game nothing ever sets it, so the
  recording path is dead; `recording` stays 0 throughout normal play.
- **`nextRecord` is a record index, not an address.** The original walks the recording with a 16-bit byte
  pointer split across two high-RAM bytes; this surface stores the index of the next `DemoInputRecord`
  instead, because the port's demo stream is the composed record array, not a byte blob. The address
  relation is in the contract; you index the timeline directly.
- **`activeDemo`'s numbering is inverted against play order.** `TYPE_A` is 2 and plays first; `TYPE_B` is 1
  and plays second. Read `activeDemo` for the type; do not infer play order from the value.
- **`demoHeld` and `savedHeld` hold actions, not joypad bytes.** They are `retropp::ActionSet` over the
  game's input actions, matching the recording's held sets. The pressed edge a frame produces is derived
  from the held-set transition at replay time; it is not stored here.
- **This is the state block, not the demo player.** Nothing in this struct advances the cursor, decodes the
  run-length encoding, or substitutes the joypad — the playback loop, the recording path, and the
  end-of-demo checks are the demo systems' job, and they take a reference to a `DemoState` to do it.

## The layout + census fixture

`DemoState` shares the high-RAM layout+census fixture with the other high-RAM state units; this unit adds
nothing to it. Regenerate the fixture after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_hram.py \
  --source-root ../tetris \
  --all \
  --fixture-out tests/fixtures/hram_expected.h
```

The parser walks the high-RAM map deriving every label's address and size, then scans the game code for
every raw-operand high-RAM access. Python 3 (standard library only); it is a development tool and is never
needed to build or test Kirpich.

## Changing it

To add or reshape a demo field, edit `src/state/demo_state.h` and give the new member a zero default so
`reset()` and the default constructor stay correct. Each demo byte maps to a labelled address in the
original, so its width is pinned against the generated fixture; if you add a field for a byte the original
reaches by a raw numeric address, that address appears in the census and the ownership check requires the
contract to name its owner — add the mapping to
[`../contracts/demo-state.md`](../contracts/demo-state.md) and the owned-byte table in the test in the same
change.

## Testing

`tests/test_demo_state.cpp` pins the seven labelled rows against the layout fixture, resolves every one of
the seven owned bytes to exactly one field — with both pointer halves resolving to `nextRecord` and a
negative guard on the bytes that bracket the unit — checks `reset()` returns a fully-mutated instance to the
boot state, and pins the wire values (the `ActiveDemo` codes, the recording magic, the timeline-count fit).
The per-byte ownership guard that proves no byte is claimed twice lives in
`tests/test_game_flow_state.cpp` and `tests/test_audio_state.cpp`; both already assign these bytes to this
unit. The fixture parser has its own tests (`tools/asm_parser/test_parse_hram.py`).
