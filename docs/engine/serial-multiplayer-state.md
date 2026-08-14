# Serial / multiplayer state

The state two Game Boys share over the link cable — the master/slave role and the serial protocol bytes,
the in-round status the sides exchange, the received-garbage pipeline, the match win tally, and the pause
save slots — held in one `MultiplayerState` struct. A single instance carries every byte the link mode
communicates through; the two-player systems take a reference to it, read the fields they care about, and
write the ones they own.

It is an idiomatic C++ surface, not a byte image of the original's high RAM. Bytes read only as zero /
non-zero are `bool`, the role and protocol bytes are typed enums, and the round-end code is a small enum.
The exact mapping back to the original's `$FFAC`–`$FFF2` addresses, the two-player protocol, the wire-code
vocabulary, and the quirks the surface preserves are the behavioral spec in
[`../contracts/serial-multiplayer-state.md`](../contracts/serial-multiplayer-state.md).

Everything is in `namespace kirpich`, header-only, included as `"state/multiplayer_state.h"` (the `src/`
tree is on the library's include path). There is no `.cpp`. It is a sibling of `GameFlowState`
([`game-state-machine-state.md`](game-state-machine-state.md)) and the other state blocks, not a member of
any.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/multiplayer_state.h` | `RoundOutcome`, `MultiplayerState`, and `reset()` | Hand-written. Edit here to add or reshape a field. |
| `tests/fixtures/hram_expected.h` | The original's whole high-RAM layout map and the raw-operand access census | **Generated — do not hand-edit.** Shared with the other high-RAM state units. |

## The types

`RoundOutcome` is the round-end code crossed between the two sides:

```cpp
enum class RoundOutcome : uint8_t {
    NONE    = 0x00,   // no round-end code crossed yet
    WE_LOST = 0x77,   // opponent cleared their line goal first
    WE_WON  = 0xAA,   // opponent topped out
};
```

`MultiplayerState` is the link-mode block — twenty-six fields, each mapping to one high-RAM byte:

```cpp
struct MultiplayerState {
    uint8_t marioStartHeight = 0, luigiStartHeight = 0;  // Type-B start heights (master / slave)
    uint8_t outgoingStatus = 0;          // the polymorphic in-round status byte this side sends
    bool    isMultiplayer = false;       // link mode active
    SerialRole  role{};                  // master / slave (boot 0 is "unset")
    uint8_t transferCompleted = 0;       // serial-transfer latch (domain {0,1,$1B,$1F})
    SerialState protocolState{};         // serial dispatch phase (boot HANDSHAKE)
    uint8_t sendPending = 0;             // non-zero requests the master to send tx at next VBlank
    uint8_t tx = 0, rx = 0;              // serial buffers (protocol codes and raw payload)
    RoundOutcome roundOutcome{};         // the round-end code crossed this round
    uint8_t garbageRowsReceived = 0;     // rows of attack garbage taken off the wire
    uint8_t garbageRowsPending = 0;      // staged garbage; bit 7 = apply at the next piece
    bool    garbageWipeActive = false;   // a garbage-driven field wipe is running (domain {0,2})
    bool    linesGoalReached = false;    // this side cleared its Type-B line goal
    bool    subsequentRound = false;     // not the first round of the match
    uint8_t ourWins = 0, theirWins = 0;  // match tally (first to 4, shown as 5 stamps)
    uint8_t advantageOurs = 0, advantageTheirs = 0, deuce = 0;  // dead victory-screen display path
    uint8_t garbageRowsToSend = 0;       // rows to send this clear, by clear kind {0,1,2,4}
    bool    winDoesNotCount = false;     // simultaneous round end; suppress the tally
    bool    musicSelectionChanged = false;  // slave-side music-select redraw flag
    uint8_t savedTx = 0, savedRx = 0;    // tx / rx saved across a pause

    void reset();
    friend bool operator==(const MultiplayerState&, const MultiplayerState&) = default;
};
```

Every member is zero-initialised, so a default-constructed instance is the boot state, and the struct has
a defaulted `==`.

## Using it

```cpp
#include "state/multiplayer_state.h"

kirpich::MultiplayerState link;   // all-zero: the boot state

// The role is elected by the handshake; the send path branches on it.
if (link.role == kirpich::SerialRole::MASTER && link.sendPending) { /* send link.tx */ }

// The round-end state reads roundOutcome to pick victory vs defeat.
if (link.roundOutcome == kirpich::RoundOutcome::WE_WON) { /* victory */ }

// Return everything to the boot state (e.g. when starting a new match).
link.reset();
```

`reset()` restores every field to its default (all-zero) value; it is equivalent to assigning a fresh
`MultiplayerState{}`.

## Gotchas

- **`role` boots to an invalid enumerator.** Its boot value `0` is not a named `SerialRole`; it means
  "unset until the handshake elects," exactly as in the original. `protocolState`'s boot `0`, by contrast,
  *is* a valid enumerator (`SerialState::HANDSHAKE`).
- **`transferCompleted` is a `uint8_t`, not a `bool`.** It reads like a "transfer done" flag, but two
  sites in the original write non-standard non-zero values (`$1B`, `$1F`) — both flagged as bugs there —
  so its domain is `{0, 1, $1B, $1F}` and a `bool` would drop information the original keeps.
- **`roundOutcome`'s stored value is the inverse of the sent code.** A side that *sends* "I won by lines"
  (`$77`) causes the receiver to store `WE_LOST`; a side that *sends* "I topped out" (`$AA`) causes the
  receiver to store `WE_WON`. The value in the struct is from the receiver's point of view. See the
  contract.
- **The advantage/deuce trio is a dead path.** `advantageOurs` / `advantageTheirs` / `deuce` are read by
  the victory screen but never set to a non-zero value by any reachable code in the original. They are
  kept as fields for a future victory-screen port; do not expect them ever to be non-zero.
- **`tx` and `rx` are raw bytes.** They carry protocol codes in most exchanges but move raw board rows and
  the piece list during the bulk transfer, so they are not typed. The wire-code vocabulary is in the
  contract.
- **This is the state block, not the transport.** Nothing in this struct clocks a transfer, elects the
  role, or applies garbage — the serial handler, the handshake, the VBlank send, the status exchange, and
  the garbage math are the two-player systems' job, and they take a reference to a `MultiplayerState` to
  do it.

## The layout + census fixture

`MultiplayerState` shares the high-RAM layout+census fixture with the other high-RAM state units; this
unit adds nothing to it. Regenerate the fixture after repinning the upstream source:

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

To add or reshape a link-mode field, edit `src/state/multiplayer_state.h` and give the new member a zero
default so `reset()` and the default constructor stay correct. If the field maps to a labelled address in
the original, its width is pinned against the generated fixture; if it maps to a byte the original reaches
by a raw numeric address, that address appears in the census and the ownership check requires the contract
to name its owner — add the mapping to
[`../contracts/serial-multiplayer-state.md`](../contracts/serial-multiplayer-state.md) and the owned-byte
table in the test in the same change.

## Testing

`tests/test_multiplayer_state.cpp` pins the twelve labelled rows and the five gap rows against the layout
fixture, resolves every one of the twenty-six owned bytes to exactly one field — with a negative guard on
the bytes just past the unit's range — checks `reset()` returns a fully-mutated instance to the boot state
along with the enum boot values, and pins the wire values (`RoundOutcome` and `SerialRole` codes). The
per-byte ownership guard that proves no byte is claimed twice lives in
`tests/test_game_flow_state.cpp` and `tests/test_audio_state.cpp`; both already assign these bytes to this
unit. The fixture parser has its own tests (`tools/asm_parser/test_parse_hram.py`).
