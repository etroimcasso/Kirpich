# Demo state

The state the attract-mode demo machinery persists across frames while a recording plays — which demo is
running, the dead recording flag, the run-length countdown, the cursor into the active timeline, and the
demo's held buttons plus the player's real held state parked while the demo drives — ported as one
hand-written C++ struct. Where the game-flow state holds what the single-player main loop reads each frame,
this holds what the demo player carries between frames.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `DemoState` | `src/state/demo_state.h` | the seven demo-machinery bytes in the original's high RAM (`$FFE4`, `$FFE9`–`$FFEE`): which demo runs, the recording flag, the frame countdown, the timeline cursor, and the two held-button sets |
| `ActiveDemo` | `src/state/demo_state.h` | a small port-authored enum for which demo is running (`NONE` / `TYPE_B` / `TYPE_A`) |

The behavioral specification — the field-by-field mapping to the original's HRAM addresses, the playback and
recording narratives, the pointer↔cursor relation, and the upstream quirks the surface preserves — is in
[`../contracts/demo-state.md`](../contracts/demo-state.md). The record / stream / piece-list *data* it
consumes is the demo-data unit, [`../contracts/demo.md`](../contracts/demo.md).

## Decisions

**One header-only struct, a sibling of the other state blocks.** `DemoState` is a plain struct with a
`reset()` that returns it to the all-zero boot state; there is no `.cpp`. It sits beside the other state
structs rather than inside any of them — combining the state blocks into the running game is later wiring.

**Which demo is running is an enum.** `activeDemo` is `ActiveDemo`, a closed three-value identity domain
with no upstream constant. The enumerator values carry the game type — `TYPE_A = 2`, `TYPE_B = 1` — because
that is the identity role. The demo *order* is a separate quirk: the game plays demo 2 (Type A) first, then
demo 1 (Type B), so the numbers alternate `0 → 2 → 1 → 2 → 1 …`. The order lives in the contract; the names
carry the type.

**The recording flag stays a raw byte.** `recording` is `uint8_t`, not `bool`. Its enable value is `$FF`
(the parser-emitted `kDemoRecordingEnabledMagic` from the misc data), not 1, and the three consumers split
two ways — two compare `== $FF`, one tests any non-zero — a distinction a `bool` would erase. The recording
path itself is dead in the shipped game (nothing sets the flag); it ports as dead-but-present with the demo
systems.

**The pointer pair collapses to one cursor.** The original walks the demo blob with a 16-bit pointer split
across two high-RAM bytes. The port's demo stream is the composed `DemoInputRecord` array, so the two
pointer halves become one `uint16_t nextRecord` — the index of the next record to load. The GB address
relation (`pointer = blobBase + 2 × index`) is recorded in the contract; the port never reconstructs the
address.

**The held sets are actions, not joypad bytes.** `demoHeld` and `savedHeld` are `retropp::ActionSet` — the
same action vocabulary a `DemoInputRecord` carries — because the port's live input surface is action-based
(the engine's action input path). `demoHeld` is the demo's current held buttons; `savedHeld` parks the
player's real held state while the demo substitutes its own input. The pressed-edge derivation
(`(new ^ old) & new`) is runtime behavior and ports with the replay system, not here.

## Keeping it honest

The struct does not mirror the original's byte offsets (two pointer halves collapse into one cursor), so its
fidelity is checked against the existing high-RAM layout+census fixture (`tests/fixtures/hram_expected.h`)
rather than a byte image. This unit ships **no fixture and no parser of its own** — the fixture already
carries the seven labelled rows and a census entry for the two bytes reached by a raw numeric operand
(`$FFE4`, `$FFED`). `tests/test_demo_state.cpp` pins the labelled rows and their widths, resolves every one
of the seven owned bytes to exactly one field — with both pointer halves resolving to `nextRecord` and a
negative guard on the bytes that bracket the unit — checks `reset()` and the boot values, and pins the wire
values (the `ActiveDemo` codes, the recording magic, the timeline-count fit). The `$FF80` per-byte ownership
guard in `test_game_flow_state.cpp` (and the whole-map guard in `test_audio_state.cpp`) already assign these
bytes to this unit; they need no change. See [`../engine/demo-state.md`](../engine/demo-state.md) for how to
use it.

## Not here yet

The state block is data; the code that reads and writes it — the playback loop, the pressed-edge derivation,
the RLE decode/encode, the demo alternation, the end-of-demo piece-count checks, and the save/restore of the
real joypad — is the demo state machine, which builds on this struct. Demo playback and the dead-but-present
recording path are separate systems that port later. This unit provides the struct, its lifecycle, and the
contract that specifies the machinery.
