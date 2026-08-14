# Contract — Demo state

Reverse-derived behavioral contract for `DemoState` (`src/state/demo_state.h`): every byte the attract-mode
demo machinery persists across frames while a recording plays. Every address, use site, and adjudication
below is from `tetris/tetris.asm` (upstream `b95c668`) unless noted; the layout addresses are
`tetris/hram.asm`. The line anchors are the authority the tests check against.

`DemoState` is an idiomatic surface, not a byte image. It carries the state the demo player sets in one
frame and reads on a later one: which demo is running, the dead recording flag, the run-length countdown,
the cursor into the active timeline, and the two held-button sets. The playback loop, the pressed-edge
derivation, the RLE decode/encode, the demo alternation, the end-of-demo checks, and the save/substitute of
the real joypad are *mechanism* that reads and writes these fields and is ported with the demo systems
later; this contract records it with anchors but implements none of it.

This unit adds **no parser work**. The HRAM layout+census fixture (`tests/fixtures/hram_expected.h`)
already carries the seven labelled rows and a census entry for the two bytes reached by a raw numeric
operand. The `$FF80` per-byte ownership map that `test_game_flow_state.cpp` and `test_audio_state.cpp`
enforce assigns each of this unit's bytes to it; those guards stay as-is. The record / stream / piece-list
*data* semantics are the demo-data unit's — see [`demo.md`](demo.md) — and are cross-referenced here rather
than restated.

---

## The field map

Seven bytes, all carrying an `hram.asm` label; the two pointer halves collapse into one field, so six
fields cover seven bytes.

| Address | Upstream label | Port field | Port type — domain | Anchors |
|---|---|---|---|---|
| `$FFE4` | `hDemoNumber` | `activeDemo` | `ActiveDemo` `{0,1,2}` | countdown select (raw `[$E4]` `575`); alternation `596-614`; real-game clear `695`; gameplay gates `4222` / `4445` / `5090` |
| `$FFE9` | `hDemoRecording` | `recording` | `uint8_t` `{0,$FF}` | set `$FF` `628-629`; title clear `527`; `cp $FF` `778` / `829`; any-non-zero `867-868` |
| `$FFEA` | `hDemoJoypadTimer` | `framesRemaining` | `uint8_t` | decrement `783-784`; load next `801`; record-path increment `857-859`; init 0 `591` |
| `$FFEB` | `hDemoJoypadDataHi` | `nextRecord` (hi half) | `uint16_t` — record index | read `788-805`; init `592-595` / `606-609`; dead-write `836-847` |
| `$FFEC` | `hDemoJoypadDataLo` | `nextRecord` (lo half) | (same field — the pointer pair collapses) | |
| `$FFED` | `hDemoJoypadHeld` | `demoHeld` | `retropp::ActionSet` | edge derive `794-799`; raw `[$ED]` clear `590` |
| `$FFEE` | `hSavedJoyHeld` | `savedHeld` | `retropp::ActionSet` | saved `812-813`; restored `870-871` |

Line numbers not carrying a leading label are absolute `tetris.asm` lines.

---

## Playback

Demos run the normal game loop with recorded input in place of the player's, per the upstream comment at
`tetris.asm:769-772`. `DemoSimulateJoypad` (`tetris.asm:773-816`) is the per-frame driver:

1. Early-out if no demo is running (`activeDemo == NONE`, `774-776`) or the recording path is enabled
   (`recording == $FF`, `777-779`).
2. While `framesRemaining` is non-zero, decrement it and hold the current input (`780-785`).
3. When it reaches zero, load the next record (`.newKeys`, `787-806`): read the held byte through the
   cursor, derive the pressed edge `hJoyPressed = (new ^ old) & new` (`794-797`), store the new held set
   into `demoHeld`, load the record's frame count into `framesRemaining`, and advance the cursor by two
   bytes.
4. Either way, save the player's real held input into `savedHeld` and substitute `demoHeld` into the live
   joypad (`.saveRealKeypresses`, `811-816`). `RestoreDemoSavedJoypad` (`863-872`) undoes the substitution
   after the frame — upstream: "Button presses can otherwise be overridden during a demo".

The pressed-edge derivation, the substitution, and the RLE walk are runtime behavior and port with the
demo-replay system; `DemoState` holds only the persisted slots.

The unused `.releaseKeys` tail (`818-822`, upstream "Unused. Maybe an earlier attempt at RLE?" and "Twice
unreachable?") is dead — no field, contract note only.

## Recording is a dead path

`recording` (`$FFE9`) is `$FF` only when the recording path is armed. `StartRecordingDemo` (`627-630`,
upstream "; Unused?") is the sole writer of the `$FF` magic, and nothing in the shipped game calls it, so
`RecordDemo` (`824-860`) — called every frame during gameplay but gated off by `recording != $FF`
(`828-830`) — never runs. When it did, it would advance the cursor while *writing* the held/timer bytes
back through it; because the cursor points into ROM address space on hardware, those stores are a no-op.
The record path ports later as dead-but-present per `DESIGN.md`. The `$FF` value is
`kirpich::kDemoRecordingEnabledMagic`, the parser-emitted misc constant
(`src/data/generated/misc_data.inc`); `recording` stays `uint8_t` rather than `bool` because its enable
value is `$FF` (not 1) and the three consumers split two ways: `DemoSimulateJoypad` (`778`) and `RecordDemo`
(`829`) compare `== $FF`, while `RestoreDemoSavedJoypad` (`867-868`) tests any non-zero.

## The pointer pair is one cursor

The original walks the demo blob with a 16-bit pointer split across `hDemoJoypadDataHi` / `Lo`
(`$FFEB` / `$FFEC`), advanced two bytes per record. The port's demo stream is the composed record array
(`kTypeADemoInputs` / `kTypeBDemoInputs`, `src/data/demo.h`), selected by `activeDemo`, so the two pointer
halves collapse into one `uint16_t nextRecord` — the index of the next `DemoInputRecord` to load. The exact
relation:

```
pointer = blobBase + 2 * nextRecord
blobBase = TypeADemoData (activeDemo == TYPE_A) | TypeBDemoData (activeDemo == TYPE_B)
```

`StartDemo`'s pointer init (`592-595`, `606-609`) is `nextRecord = 0`. `uint16_t` holds both timelines'
counts (128 / 80). The port stores the record index, not the address — the composed record array is the
stream, so the GB byte pointer is serialization the port does not reconstruct.

---

## Mechanism, not state

The following read and write the fields above. Each is recorded with its role; none becomes a field, the
same treatment the audio and sprite-renderer units gave their call-transient bytes.

| Mechanism | Role | Anchors |
|---|---|---|
| `GameState_06` title init | clears `hDemoRecording` (`527`); the countdown select reads `hDemoNumber` (raw `[$E4]` `575`) and sets the `$C6` countdown byte — `$13` fresh boot / `$04` returning from a demo (`573-579`). `$C6` is the game-flow countdown byte (`coarseCountdown`), not this unit | `524-580` |
| `GameState_07` countdown | decrements the `$FFC6` countdown and launches `StartDemo` at zero | `632-641` |
| `StartDemo` | demo config writes: game-flow fields (`hGameType` `$37`/`$77`, level 9, `hTypeBStartHeight` 2, `hNumPiecesPlayed` 0/17) and demo fields (timer 0, cursor init, `demoHeld` clear via raw `[$ED]` `590`, the `hDemoNumber` alternation) | `582-624` |
| `StartRecordingDemo` | dead (upstream "; Unused?"); only writer of the `$FF` magic | `627-630` |
| mode-select `.nextState` | clears `hDemoNumber` when a real game starts | `690-696` |
| `CheckForEndOfDemo` | piece-count end (demo 2 → 16, demo 1 → 29, upstream "piece 17(!)"); START exit with the `$33` serial broadcast (`746-748`, serial bytes are the serial unit's territory) | `734-767` |
| `DemoSimulateJoypad` | record load, edge derivation, save/substitute; the dead `.releaseKeys` tail | `773-822` |
| `RecordDemo` | dead-but-present; called every frame in gameplay, gated off by `recording != $FF` | `824-860` |
| `RestoreDemoSavedJoypad` | restore the real joypad after substitution | `863-872` |
| three gameplay gates | demo garbage init (`4222`), start/select suppression (`4445`), deterministic piece choice (`5090`) — each gated on `hDemoNumber` non-zero; the piece list itself is the demo-data / engine-state surface | `4222`, `4445`, `5090` |

`hJoyHeld` / `hJoyPressed` (`$FF80` / `$FF81`) remain deferred to the input bridge; this unit types the
demo-side slots only.

---

## Upstream quirks in scope

Preserved verbatim where the behavior lands (ported with the demo systems):

- **The demo-number inversion.** Demo **2** plays first and is **Type A**; demo **1** second and **Type B**
  (`StartDemo` alternation `596-614`). The numbers run `0 → 2 → 1 → 2 → 1 …`. `ActiveDemo`'s enumerators
  carry the game type (`TYPE_A = 2`, `TYPE_B = 1`) because that is the identity role; the play-order lives
  here in the contract, not in the names.
- **`hDemoNumber` survives a demo → title → demo cycle.** `GameState_06` (title init) does *not* clear it
  (only a real game start does, `695`), so it selects the next demo's alternation and shortens the title
  countdown on return.
- **The recording path writes through the pointer into ROM.** `RecordDemo`'s stores (`841-843`) target the
  cursor, which addresses ROM on hardware — a no-op. Dead in the shipped game regardless (`recording` is
  never `$FF`).
- **The `.releaseKeys` dead tail** (`818-822`) — unreachable, upstream-flagged.

---

## Boot semantics

Boot is all-zero. The startup HRAM clear (`tetris.asm:347-352`) wipes the whole block, so a
default-constructed `DemoState` — every field zero — is the boot state, and `reset()` returns a live
instance to it. `activeDemo`'s boot 0 is `ActiveDemo::NONE` (no demo running); `recording`'s boot 0 is
playback mode (≠ the `$FF` magic); `framesRemaining` and `nextRecord` boot to 0; both `ActionSet`s boot
empty.

---

## The surface

- **Parser-emitted** (reused, no delta): `tests/fixtures/hram_expected.h` — the seven labelled rows and the
  census entries for `$FFE4` / `$FFED`.
- **Hand-written port-design:** `src/state/demo_state.h` (the `ActiveDemo` enum, the `DemoState` struct, and
  `reset()`), the tests, and this contract.

There is no behavioral code in this unit: the playback loop, the pressed-edge derivation, the RLE
decode/encode, the demo alternation, the end-of-demo checks, and the save/restore substitution build on
this struct when the demo systems are written.

---

## Tested by

`tests/test_demo_state.cpp` — the HRAM window pins (the seven labelled rows present at their addresses at
width 1, the two raw-accessed bytes `$FFE4` / `$FFED` censused, no other `$FFE9`–`$FFEE` byte censused, and
the neighbour pins for the labels and gap that bracket the unit); the width pins (each labelled row one
byte, the scalar members one byte, `ActiveDemo` one byte, the two pointer halves collapsed into the one
`uint16_t nextRecord`, `demoHeld` / `savedHeld` the same type as `DemoInputRecord::held`, defaulted `==`);
the per-byte field resolution (all seven owned bytes resolve to exactly one field with both pointer halves →
`nextRecord`, plus the negative guard on the bracketing bytes); the reset-to-boot sweep with the boot pins;
and the wire-value pins (`ActiveDemo` `0` / `1` / `2`, the `kDemoRecordingEnabledMagic` `$FF` cross-pin, the
timeline-count fits). The two shipped census guards elsewhere (`test_game_flow_state.cpp`,
`test_audio_state.cpp`) already own these bytes for this unit.
