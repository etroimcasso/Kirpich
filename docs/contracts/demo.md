# Contract — Demo data

Reverse-derived behavioral contract for Kirpich's demo data: the two attract-mode joypad recordings and
the piece sequence they share. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors below are the authority the tests check against.

When left alone at the title screen the game plays two **attract-mode demos** — a Type A game and a
Type B game — by running the normal game loop with recorded input in place of the player's, and drawing
pieces from a fixed list instead of the randomizer. Three `INCBIN` binaries hold that recording. They
are algorithmic data — input timelines and piece picks, no expressive content — so unlike the graphics
and audio payloads they compile directly into the binary rather than being extracted from a ROM.

---

## The three blobs

At the tail of the main ROM section (`tetris.asm:7172-7177`), three contiguous `INCBIN` directives:

| Label | File | Size | Contents |
|---|---|---|---|
| `TypeADemoData` | `typeademodata.bin` | 256 B | 128 × 2-byte joypad records — the Type A demo |
| `TypeBDemoData` | `typebdemodata.bin` | 160 B | 80 × 2-byte joypad records — the Type B demo |
| `DemoPieceList` | `demopiecelist.bin` | 48 B | 48 piece-spec bytes, shared by both demos |

Provenance (per the upstream `dump_demo.py`): `TypeADemoData` sits at ROM `$62B0`, `TypeBDemoData` at
`$63B0`, and `DemoPieceList` therefore at `$6450`. These addresses are provenance only — no port
artifact uses them; the port reads the `.bin` files directly.

## The joypad records

Each record is two ROM bytes: **`(held, frames)`**. `held` is the complete held-button state as a Game
Boy joypad byte; `frames` is the number of frames that state persists before the next record loads. The
encoding is the game's own run-length compression — a new record only where the buttons change
(`DemoSimulateJoypad`, `tetris.asm:769-816`, described in the upstream comment at `:769-772`).

Replay, per `DemoSimulateJoypad`:

- While the frame timer is non-zero it counts down; `hJoyPressed` reads 0 and `hJoyHeld` is overridden
  with the record's held byte every frame (the player's real input is saved to `hSavedJoyHeld` and
  restored afterward by `RestoreDemoSavedJoypad`).
- When the timer expires the next record loads: `hJoyPressed = (new ^ oldHeld) & new` (the pressed
  edge), `hDemoJoypadHeld = new`, and the timer is set to the record's `frames`.

The **held byte's bit layout** is the standard Game Boy joypad byte (`hardware.inc:869-876`):
`A=$01, B=$02, SELECT=$04, START=$08, RIGHT=$10, LEFT=$20, UP=$40, DOWN=$80`. Across both recordings
only `A | RIGHT | LEFT | DOWN` bits ever appear — the demos never press B, SELECT, START, or UP.

### The button-to-action resolution

The demos replay **gameplay**, so a held byte is a set of game *actions*. The port surfaces each record's
held state as a set of the game's input actions (`kirpich::Action`), not a raw byte. The mapping is
reverse-derived from the gameplay input handler `RotateAndShiftPiece` (`tetris.asm:5910-6028`) and the
soft-drop path:

| Button | Bit | Action | Anchor |
|---|---|---|---|
| A | `$01` | `RotateClockwise` | `:5922-5930` (`.rotateCW`, decrements the orientation bits) |
| B | `$02` | `RotateCounterClockwise` | `:5920`, `:5938-5944` (`.rotateCCW`, increments them) |
| RIGHT | `$10` | `MoveRight` | `:5973-5987` (piece X `+8`, DAS at 23 / 9 frames) |
| LEFT | `$20` | `MoveLeft` | `:6006-6021` (piece X `−8`) |
| DOWN | `$80` | `SoftDrop` | the soft-drop path |

The demo corpus only presses A, RIGHT, LEFT, and DOWN, so each record resolves to a subset of
`{RotateClockwise, MoveRight, MoveLeft, SoftDrop}` — `RotateCounterClockwise` (B) is defined for
completeness but never appears. B, SELECT, START, and UP have no piece-control action; a held byte that
pressed one is a hard parse error, never silently dropped.

The pressed edge (`hJoyPressed`) is derived at replay from the held-set transitions between steps — the
engine's action input already models held vs. just-pressed — so the records carry only the held set.
This is the data; feeding it into the input path is the demo-replay system's job and ports with the
gameplay logic.

**The streams do not self-terminate.** There is no end sentinel; a demo ends by piece count (see below),
so records past the point the demo ends are simply never reached. Both recordings' trailing bytes are
`$00`.

## The shared piece list

During a demo (and in multiplayer) the next piece is taken from `wPieceList[hNumPiecesPlayed]` instead
of the randomizer roll (`NextPiece.deterministicChoice`, `tetris.asm:5090-5108`). Each list byte is a
`$C200` piece-spec value — `kind × 4 + rotation`, the same representation the piece logic uses. Every
one of the 48 bytes is a multiple of four below 28: seven kinds (0–6), rotation always 0 (spawn
orientation).

**Consumption ranges** (`StartDemo`, `tetris.asm:582-624`; `CheckForEndOfDemo`, `tetris.asm:754-764`):
the demos run demo #2 first, then demo #1.

| Demo | Game | Level / height | Piece indices read |
|---|---|---|---|
| #2 | Type A | level 9 | 0 – 15 |
| #1 | Type B | level 9, height 2 | 17 – 29 |

Demo #2 starts at `hNumPiecesPlayed = 0` and ends when it reaches 16 (`ld b, 16`, `:758`); demo #1
starts at 17 (`ld a, 17`, `:610`) and ends at 29 (`ld b, 29`, `:760`). **Index 16 is never read** — the
upstream comment calls it out as `piece 17(!)`. Both ranges sit inside the 48 bytes.

## Quirks

- **The 256-byte copy overrun.** `GameState_24` (`tetris.asm:479-500`) copies from `DemoPieceList` into
  `wPieceList` (`$C300`) until the destination high byte reaches `$C4` — a full 256 bytes, overrunning
  the 48-byte blob into whatever ROM follows it. The upstream comment flags it: `TODO, might copy way,
  way too much? Including some music?`. The overrun bytes are never observably consumed — the demos read
  indices ≤ 29, and every other `wPieceList` reader operates on the randomizer-filled list, not the demo
  copy — so the port ships only the 48 real bytes.
- **The recording path is dead.** `DemoSimulateJoypad` early-outs when `hDemoRecording == $FF`
  (`tetris.asm:777-779`), but nothing in the shipped game ever sets that flag, so the record path
  (`RecordDemo` / `StartRecordingDemo`) never runs. The `$FF` recording magic is a separate misc
  constant, not part of this unit.

## Deferred to the gameplay logic

Recorded here so the data unit's scope is unambiguous; none of these produce a committed constant in
this unit:

- The demo state machine and its config values (`StartDemo`: game-type bytes `$37`/`$77`, level 9,
  Type B height 2, the demo-number 2→1 sequencing).
- The end-piece counts (16, 29) and the start index (17).
- The `GameState_24` → `GameState_25` → `GameState_35` copyright-screen timer chain.
- The `wPieceList` RAM machinery and its multiplayer fill / serial / garbage-hole uses.
- Feeding the action sets into the input path and deriving the pressed edge from the held-set
  transitions — the demo-replay system's job.
- The binding of physical controls (keyboard / gamepad) to `kirpich::Action` — the input system's job.
  The demo data references the actions; it does not bind them.

---

## The surface

- **Parser-emitted** (`tools/asm_parser/parse_demo.py`, `--all`): `src/data/demo.h` — the
  `DemoInputRecord` struct (an `retropp::ActionSet` of held actions + a frame count), the `heldActions`
  builder, the two composed record arrays, the `Piece`-typed piece list, and the count constants — and
  `tests/fixtures/demo_expected.h` — the three blobs as flat raw bytes, independent of the composed
  surface. Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `include/kirpich/action.h` (the `Action` vocabulary), the tests, and this
  contract. `Action` is the game's own input enum — the engine's action input system reads state keyed
  by a game-owned enum, so the port defines one rather than inventing a button type. It carries the
  gameplay piece-control actions and grows as more input surfaces land.

The record's held state is an action set rather than the raw ROM byte, but byte-equivalence stays under
test: the fixture holds each blob's real bytes, and the sweep resolves each raw held byte through the
button-to-action mapping above and compares it to the composed record — so a wrong mapping fails loudly.

### Transcription asserts

`parse_demo.py` hard-errors (with a `file:line` citation) on any of: a `.bin` file whose size is not
exactly 256 / 160 / 48; a joypad blob with an odd byte count; a held byte that presses a button with no
mapped action; any piece byte that is not a multiple of four below 28; or the three `INCBIN` directives
not appearing as contiguous `Label::` / `INCBIN "file"` pairs, in order, under `TypeADemoData` /
`TypeBDemoData` / `DemoPieceList`. The three `.bin` files are read directly off disk.

---

## Tested by

`tests/test_demo.cpp` — the three counts and the piece size pinned; the full 128- and 80-record sweeps of
each composed array, bridging every raw held byte to the action set the record must carry; the full
48-piece sweep with the spawn-orientation domain checked across the corpus; the corpus shown to press
only the five mapped bits and never to rotate counter-clockwise; and the corners plus the consumed index
ranges pinned to concrete values. The parser's own structural checks
(`tools/asm_parser/test_parse_demo.py`) guard the scan against upstream changes.
