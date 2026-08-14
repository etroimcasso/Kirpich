# Contract — Serial / multiplayer state

Reverse-derived behavioral contract for `MultiplayerState` (`src/state/multiplayer_state.h`): every byte
the original communicates through when two Game Boys play head to head over the link cable, ported as one
C++ struct. Every address, use site, and adjudication below is from `tetris/tetris.asm` (upstream
`b95c668`) unless noted; the layout addresses are `tetris/hram.asm`. The line anchors are the authority
the tests check against.

`MultiplayerState` is an idiomatic surface, not a byte image. It carries the state the two sides set in
one frame (or in the serial interrupt, at arbitrary time) and read frames later: the master/slave role
and the serial protocol bytes, the polymorphic in-round status byte, the received-garbage pipeline, the
match win tally, and the pause save slots. The transport and protocol *logic* — the interrupt handler,
the handshake, the VBlank send, the status exchange, the garbage bit-math, the win promotion, and the
pause sync — is mechanism that reads and writes these fields and is ported with the two-player systems;
this contract records it with anchors but implements none of it.

This unit adds **no parser work**. The HRAM layout+census fixture (`tests/fixtures/hram_expected.h`)
already carries the twelve labelled rows, the five gap rows the unlabelled bytes fall inside, and a
census entry for every raw-accessed byte. The `$FF80` per-byte ownership map that
`test_game_flow_state.cpp` and `test_audio_state.cpp` enforce assigns each of this unit's bytes to it;
those guards stay as-is.

---

## The field map

Twenty-six bytes: twelve carry an `hram.asm` label, fourteen are reached only by a raw numeric operand
(`ldh [$D1], a`) and live inside a gap row.

| Address | Upstream label | Port field | Port type — domain | Anchors |
|---|---|---|---|---|
| `$FFAC` | `hMarioStartHeight` | `marioStartHeight` | `uint8_t` 0–5 | master's Type-B start height (master = Mario) |
| `$FFAD` | `hLuigiStartHeight` | `luigiStartHeight` | `uint8_t` 0–5 | slave's Type-B start height (slave = Luigi) |
| `$FFB1` | — | `outgoingStatus` | `uint8_t` — polymorphic (below) | height into `$B1` `1725-1760`; `$80\|rows` `1830-1831`; end codes `1855`/`1869` |
| `$FFC5` | `hIsMultiplayer` | `isMultiplayer` | `bool` `{0,1}` | link mode active; tested `4923-4924`, `5782-5784`; interrupt-enable switch `406-410` |
| `$FFCB` | `hSerialRole` | `role` | `SerialRole` — boot 0 = unset | elected `82-94` |
| `$FFCC` | `hSerialInterruptTriggered` | `transferCompleted` | `uint8_t` `{0,1,$1B,$1F}` | set 1 by ISR `51-52`; `$1B` quirk `4928-4929`; `$1F` quirk `2086-2088` |
| `$FFCD` | `hSerialState` | `protocolState` | `SerialState` — boot 0 = HANDSHAKE | ISR dispatch index `60-66` |
| `$FFCE` | — | `sendPending` | `uint8_t` — non-zero = send request | VBlank send `201-212`; set `1843`, `891` |
| `$FFCF` | `hSerialTx` | `tx` | `uint8_t` (raw wire buffer) | sent `112`, `126`, `209-210` |
| `$FFD0` | `hSerialRx` | `rx` | `uint8_t` (raw wire buffer) | latched `102`, `107`, `120-121`; `$FF` sentinel `1835-1836` |
| `$FFD1` | — | `roundOutcome` | `RoundOutcome` `{0,$77,$AA}` | `label_C50` `1850-1858`, `label_C64` `1864-1876` (incl. long-form `1869`) |
| `$FFD2` | — | `garbageRowsReceived` | `uint8_t` 1–4 | staged off the wire, masked `& $7F` `1884-1888` |
| `$FFD3` | — | `garbageRowsPending` | `uint8_t` — bit 7 = apply-now latch | staged `1903-1905`; bit-7 set `5109-5113`; consumed `Call_C8C` `1890-1897` |
| `$FFD4` | — | `garbageWipeActive` | `bool` — `{0,2}` | set `1955-1960`; cleared at wipe end `5805-5810` |
| `$FFD5` | — | `linesGoalReached` | `bool` `{0,1}` | set `5782-5786` |
| `$FFD6` | — | `subsequentRound` | `bool` `{0,1}` | set `GameState_1F`; cleared `GameState_1C` |
| `$FFD7` | `hOurWins` | `ourWins` | `uint8_t` 0–5 | tallied; 4 promoted to 5 `Call_1085` `2561-2562` |
| `$FFD8` | `hTheirWins` | `theirWins` | `uint8_t` 0–5 | tallied; 4 promoted to 5 `2548-2550` |
| `$FFD9` | — | `advantageOurs` | `uint8_t` — dead (below) | read `Call_1085` `2533-2534`; init-cleared |
| `$FFDA` | — | `advantageTheirs` | `uint8_t` — dead | read `2536-2537` |
| `$FFDB` | — | `deuce` | `uint8_t` — dead | read `2539-2540` |
| `$FFDC` | — | `garbageRowsToSend` | `uint8_t` `{0,1,2,4}` by clear kind | set `5378-5398`; composed to wire `1827-1833` |
| `$FFEF` | — | `winDoesNotCount` | `bool` `{0,1}` | set `label_C7C` `1878-1881` |
| `$FFF0` | — | `musicSelectionChanged` | `bool` `{0,1}` | slave-side redraw flag, `GameState_2B` |
| `$FFF1` | `hSavedSerialTx` | `savedTx` | `uint8_t` — pause save slot | saved/restored across pause |
| `$FFF2` | `hSavedSerialRx` | `savedRx` | `uint8_t` — pause save slot | saved/restored across pause |

Line numbers not carrying a leading label are absolute `tetris.asm` lines.

---

## The two-player protocol

The protocol runs over the serial interrupt and a VBlank-driven send. Each side polls
`transferCompleted` to know a transfer finished, and the master drives the clock.

**Handshake / role election** (`Handshake`, `tetris.asm:71-98`). At the title/demo screen a side reads
the byte the other clocked in (`rSB`). Reading the `SLAVE` code (`$55`) means the other side is the
slave, so this side becomes `MASTER` (`$29`) and writes the internal-clock code; reading the `MASTER`
code makes it the `SLAVE`. Reading zero means the peer dropped back to the title screen. `role` is set
here and read everywhere the send path branches on who drives the clock.

**Master-driven send** (`VBlank`, `tetris.asm:201-212`). Each VBlank the master checks `sendPending`
(`$FFCE`); if non-zero and `role == MASTER`, it clears the flag, copies `tx` to `rSB`, and starts an
internal-clock transfer. The slave never takes this branch. `sendPending` is the game code's request to
the transport, not a protocol code — the value written is incidental (`1` at `1842-1843`, whatever `A`
holds at `891`).

**Serial dispatch** (`_Serial`, `tetris.asm:45-66`). On the serial interrupt the handler jumps through a
five-entry table indexed by `protocolState` and, on return, sets `transferCompleted` to 1. Index 0 is
`Handshake`; 1–3 are the three transfer phases (`SerialState_01/02/03`, `tetris.asm:100-131`) that latch
`rx` and, on the slave, re-arm the next transfer. The fifth slot points at a bare `ret` (upstream: "XXX
Is this used?") and is never selected.

**Music-select sync** (`GameState_2B`). The master sends its music choice; the slave acknowledges
(`$39`), and `$50` confirms. `musicSelectionChanged` (`$FFF0`) is the slave-side flag that the displayed
selection must be redrawn.

**Height negotiation** (`GameState_17`). `$60` on the wire means "start the game"; the two start heights
are echoed both ways into `marioStartHeight` / `luigiStartHeight`.

**In-round status exchange** (`Call_BF0` / `Call_C8C` region, `tetris.asm:1795-1965`). Each in-round
transfer carries one status byte per side, decoded by context — see the wire vocabulary below.

**Round-end crossing** (`GameState_1A` → `GameState_1B`). The round-end code crosses into `roundOutcome`;
`GameState_1B` selects the victory state on `WE_WON` and the defeat state otherwise
(`tetris.asm:1988-1998`).

**Pause sync.** The `$94` code toggles pause on both sides; `tx`/`rx` are parked in `savedTx`/`savedRx`
across the pause and restored on resume.

---

## The in-round status byte is polymorphic

`outgoingStatus` (`$FFB1`) and the `tx`/`rx` buffers carry different meanings in different exchanges. The
wire vocabulary (contract-only; the port mints no constants for these until the transport is built):

| Code | Meaning | Anchor |
|---|---|---|
| `$29` / `$55` | `MASTER` / `SLAVE` handshake codes (already `SerialRole`) | `82-94` |
| `0`–`18` | stack height, computed into `$FFB1` | `Call_B9B` `1725-1760` |
| `$80\|rows` | garbage attack; low bits = rows | `label_C2E` `1827-1833` |
| `$77` | "I won by lines" (stored as `WE_LOST`) | `label_C50` `1850-1858` |
| `$AA` | "I topped out" (stored as `WE_WON`) | `label_C64` `1864-1876` |
| `$39` | slave music-select ack | `GameState_2B` |
| `$50` | music-select confirmed | `GameState_2B` |
| `$60` | start game | `GameState_17` |
| `$94` | pause toggle | `tetris.asm:4490-4545` |
| `$30` / `$56` | bulk-transfer done handshake | `GameState_19` |
| `$FF` | rx-consumed sentinel | `label_C3A` `1835-1836` |

Because `tx`/`rx` also move raw board rows and the 256-entry piece list during the bulk transfer, they
are genuinely raw `uint8_t`, not an enum surface.

### `roundOutcome` has inverted wire semantics

The *sent* code and the *stored* value are inverses. `label_C50` (reached when the received status is the
"I won by lines" report) stores `$77` into `$FFD1` — but from the receiver's view that means **we lost**,
so `RoundOutcome::WE_LOST == 0x77`. `label_C64` stores `$AA` (via the one long-form
`ldh [$FFD1], a` at `tetris.asm:1869`) meaning **we won**, so `RoundOutcome::WE_WON == 0xAA`. Both byte
values are hand-typed in the enum and test-pinned (a closed identity domain with no upstream constant);
no parser reads code bodies for them.

---

## The received-garbage pipeline

Four fields carry attack garbage from a line clear on one side to field application on the other; the
bit-math is mechanism, ported with the two-player systems.

1. A line clear sets `garbageRowsToSend` (`$FFDC`) by clear kind: singles → 0, doubles → 1, triples → 2,
   Tetris → 4 (`tetris.asm:5378-5398`).
2. The next status send composes `$80 | rows` from `$FFDC` into `outgoingStatus`, then clears `$FFDC`
   (`label_C2E`, `1827-1833`).
3. On receive, the status is masked `& $7F` and range-checked (< 5) into `garbageRowsReceived` (`$FFD2`,
   `1884-1888`).
4. `Call_C8C` stages `$FFD2` into `garbageRowsPending` (`$FFD3`) and sets bit 7 as the "apply at the next
   piece" latch (`5109-5113`); field application consumes it.
5. `garbageWipeActive` (`$FFD4`) is set to `$02` while the garbage-driven field wipe runs
   (`1955-1960`) and cleared when the wipe completes (`5805-5810`); it gates a wipe SFX and suppresses a
   lines redraw.

---

## The advantage / deuce trio is a dead display path

`advantageOurs` / `advantageTheirs` / `deuce` (`$FFD9`–`$FFDB`) are read by the victory screen and by
`Call_1085` (`tetris.asm:2528-2590`, whose own comment is "seems to be unused code having to do with
advantages and deuces"). Every writer that stores a non-zero value into the trio is on a branch reached
only when one of the trio is *already* non-zero (`.d9` / `.da` / `.db`, `2586-2593`), and the init path
clears all three, so no reachable code ever sets any of them. They stay fields — a future victory-screen
port reads them — each carrying this dead-path note. `Call_1085`'s one live effect is the match-length
quirk: a tally reaching 4 is promoted to 5 (`.usFourWins` `2561-2562`, `.themFourWins` `2548-2550`), so
the match is first-to-4 but displayed as 5 stamps. That promotion ports verbatim with the win logic.

---

## Mechanism, not state

The following are transport and game logic that read and write the fields above. Each is recorded with
its role; none becomes a field, the same treatment the audio unit gave the sound driver's private RAM.

| Mechanism | Role | Anchors |
|---|---|---|
| Serial ISR + dispatch table | latch `transferCompleted`, dispatch on `protocolState` | `_Serial` `45-66` |
| Vestigial 5th dispatch slot | dead (`ret`), never selected | `66` |
| `Unused_00D0` | dead (upstream "Unused?") | `133-141` |
| Handshake + transfer phases | all `rSB` / `rSC` traffic, role election | `71-131` |
| `WAIT_FOR_SERIAL_INTERRUPT` | spin on `transferCompleted` | `macros.asm:1-6` |
| `DelayMillisecond` | slave inter-transfer delay | called `128` |
| `GameState_19` bulk transfer | move garbage board + 256-entry piece list; `$30`/`$56` done-handshake. The *data* (`board`, `wPieceList`) lives in the engine state; only the transport is mechanism | `1304-1545` (region) |
| Main-loop interrupt-enable switch | enable `IEF_SERIAL` in link mode | `406-410` |

Inside the `$FFD9` gap (size 8): `$FFDC` is `garbageRowsToSend` (this unit); `$FFDD`–`$FFDF` are dead,
un-censused bytes; `$FFE0` is the game-flow mechanism byte (a score-print flag), **not** this unit's — a
negative guard the test enforces.

---

## Upstream quirks in scope

Preserved verbatim where the behavior lands (ported with the two-player systems):

- **`GameState_2A` write-order bug** (`tetris.asm:890-892`). `RenderCursors` is called, then `sendPending`
  is written *before* `A` is zeroed — upstream flags the two lines as needing to be swapped, "Luckily, A
  is guaranteed to be zero after RenderCursors?". So `sendPending` gets whatever `RenderCursors` left in
  `A` (in practice zero).
- **`$1B` into the transfer latch** (`tetris.asm:4926-4929`). The multiplayer game-over branch writes
  `$1B` into `transferCompleted` (upstream `; XXX`) before the same `A` lands in `hGameState`, so the
  latch's domain includes `$1B`.
- **`$1F` non-zero "clear"** (`tetris.asm:2085-2088`). `GameState_20`'s `.nextState` "clears" the latch
  with `A` still holding `$1F` (upstream `; Bug?`) — a non-zero clear, so the domain also includes `$1F`.
- **Long-form `ldh [$FFD1], a`** (`tetris.asm:1869`). The one 3-byte absolute store into the round-outcome
  byte; the census refCount for `$FFD1` (9) folds it in.

---

## Boot semantics

Boot is all-zero. The startup HRAM clear (`tetris.asm:347-352`) wipes the whole block, so a
default-constructed `MultiplayerState` — every field zero — is the boot state, and `reset()` returns a
live instance to it. `role`'s boot byte 0 is not a valid `SerialRole`; it means "unset until the
handshake elects", exactly as in the original (the `GameType` / `MusicType` precedent). `protocolState`'s
boot 0 *is* a valid enumerator, `SerialState::HANDSHAKE`. `roundOutcome`'s boot 0 is `RoundOutcome::NONE`.

---

## The surface

- **Parser-emitted** (reused, no delta): `tests/fixtures/hram_expected.h` — the twelve labelled rows,
  the five gap rows, and the census entry for every byte this unit reaches. Second state unit with no
  parser work.
- **Hand-written port-design:** `src/state/multiplayer_state.h` (the `RoundOutcome` enum, the
  `MultiplayerState` struct, and `reset()`), the tests, and this contract.

There is no behavioral code in this unit: the handshake, the VBlank send, the status exchange, the
garbage bit-math, the win promotion, and the pause sync build on this struct when the two-player systems
are written.

---

## Tested by

`tests/test_multiplayer_state.cpp` — the HRAM window pins (the twelve labelled rows present at their
addresses at width 1, the five gap rows at their sizes, every unlabelled byte censused, the `$FFCE` / 15,
`$FFD1` / 9, `$FFB1` / 5, `$FFF0` / 3 corner refCounts); the width pins (each labelled row one byte, the
enum-typed members one byte, defaulted `==`); the per-byte field resolution (all twenty-six owned bytes
resolve to exactly one field with no duplicate, each labelled byte carrying its upstream label and each
unlabelled byte inside a gap row, plus the `$FFDD`–`$FFE0` negative guard); the reset-to-boot sweep with
the `protocolState == HANDSHAKE` / `role` unset / `roundOutcome == NONE` boot pins; and the wire-value
pins (`RoundOutcome` `$77` / `$AA`, `SerialRole` `$29` / `$55`). The two shipped census guards elsewhere
(`test_game_flow_state.cpp`, `test_audio_state.cpp`) already own these bytes for this unit.
