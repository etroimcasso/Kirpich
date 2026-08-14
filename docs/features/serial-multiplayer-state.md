# Serial / multiplayer state

The state two Game Boys share when they play head to head over the link cable — the master/slave role and
the serial protocol bytes, the in-round status the sides exchange, the received-garbage pipeline, the
match win tally, and the pause save slots — ported as one hand-written C++ struct. Where the game-flow
state holds what the single-player main loop reads each frame, this holds what the link mode communicates
through.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `MultiplayerState` | `src/state/multiplayer_state.h` | the twenty-six link-mode bytes scattered through the original's high RAM (`$FFAC`–`$FFF2`): the serial role/protocol bytes, the polymorphic in-round status byte, the received-garbage pipeline, the win tally, the dead advantage/deuce trio, and the pause save slots |
| `RoundOutcome` | `src/state/multiplayer_state.h` | a small port-authored enum for the round-end code (`NONE` / `WE_LOST` / `WE_WON`) |

The behavioral specification — the field-by-field mapping to the original's HRAM addresses, the full
two-player protocol narrative, the wire-code vocabulary, the received-garbage pipeline, and the upstream
quirks the surface preserves — is in
[`../contracts/serial-multiplayer-state.md`](../contracts/serial-multiplayer-state.md).

## Decisions

**One header-only struct, a sibling of the other state blocks.** `MultiplayerState` is a plain struct
with a `reset()` that returns it to the all-zero boot state; there is no `.cpp`. It sits beside the other
state structs rather than inside any of them — combining the state blocks into the running game is later
wiring. The name is `MultiplayerState`, not `SerialState`, because that name is already the serial
protocol-phase enum, and because the unit spans both the serial protocol and the match/round state.

**The bytes are gathered by purpose, not by adjacency.** The link mode's bytes are scattered through high
RAM among single-player bytes; this struct collects the ones the link mode owns into one type. The per-byte
boundary between this surface and the game-flow, sprite-renderer, and top-score surfaces that share the
same RAM is settled in the contract and enforced by the existing census guard.

**Flag widths were traced, not guessed.** Bytes the original reads only as zero / non-zero become `bool`
(`isMultiplayer`, `garbageWipeActive`, `linesGoalReached`, `subsequentRound`, `winDoesNotCount`,
`musicSelectionChanged`). Bytes that hold more than a single non-zero value stay `uint8_t` — notably
`transferCompleted`, whose domain is `{0, 1, $1B, $1F}` because two upstream sites write non-standard
non-zero values into it (both flagged as bugs in the original).

**The role and protocol bytes are the existing enums; the round-end code is a new one.** `role` is
`SerialRole` and `protocolState` is `SerialState`, the enums the core-enums unit already generates.
`roundOutcome` is a new `RoundOutcome` enum minted in this header — a closed three-value identity domain
with no upstream constant. Its two byte values (`$77`, `$AA`) are hand-typed and test-pinned; no parser
reads code bodies for them.

**The wire buffers stay raw bytes.** `tx` and `rx` carry protocol codes in most exchanges but move raw
board rows and the piece list during the bulk transfer, so no enum can type them. The code vocabulary is
recorded as a contract table; the transport mints named constants for it when it is built.

**The dead advantage/deuce trio stays as fields.** `advantageOurs` / `advantageTheirs` / `deuce` are read
by the victory screen but never set to a non-zero value by any reachable code (the original's own comment
calls the surrounding code "unused"). They stay fields so a future victory-screen port can read them, each
carrying the dead-path note in the contract.

## Keeping it honest

The struct does not mirror the original's byte offsets, so its fidelity is checked against the existing
high-RAM layout+census fixture (`tests/fixtures/hram_expected.h`) rather than a byte image. This unit
ships **no fixture and no parser of its own** — the fixture already carries the twelve labelled rows, the
five gap rows the unlabelled bytes fall inside, and a census entry for every raw-accessed byte.
`tests/test_multiplayer_state.cpp` pins the labelled rows and their widths, resolves every one of the
twenty-six owned bytes to exactly one field with a negative guard on the bytes just past the unit's range,
checks `reset()` and the enum boot values, and pins the wire values. The `$FF80` per-byte ownership guard
in `test_game_flow_state.cpp` (and the whole-map guard in `test_audio_state.cpp`) already assign these
bytes to this unit; they need no change. See
[`../engine/serial-multiplayer-state.md`](../engine/serial-multiplayer-state.md) for how to use it.

## Not here yet

The state block is data; the code that reads and writes it — the serial interrupt handler and the
handshake that elects master/slave, the VBlank send path, the in-round status exchange, the received-garbage
bit-math, the win-tally promotion, and the pause sync — is two-player transport and game logic that builds
on this struct. The link transport itself (whatever replaces the interrupt-plus-`rSB`/`rSC` mechanism) is
an engine question to resolve when the two-player systems are built. This unit provides the struct, its
lifecycle, and the contract that specifies the protocol.
