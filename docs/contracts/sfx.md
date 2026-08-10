# Contract — SFX and residual sound-driver data

Reverse-derived behavioral contract for Kirpich's sound-effect data: the identifiers the game triggers
effects by, the four SFX pointer tables and their dispatch, the per-effect register-image data blobs,
and the residual driver data (the note-pitch table, vibrato offsets, the noise-note table, the wave
timbre patterns, and the pause-tune notes). Every value here is transcribed from the
`kaspermeerts/tetris` disassembly (upstream `b95c668`), all of it from `audio.asm`; the line anchors
below are the authority the tests check against.

Like the music data, the SFX "sequences" are driver **code** — small routines that write the APU
registers from the data blobs described here. When the driver runs on the engine's audio host it is
handed its own contiguous ROM image, and those routines ride that image. This unit therefore ports the
three identifier spaces the game writes to its audio-state wire variables, plus the mechanical
configuration the routines read — register images, envelope/frequency ramps, a note-frequency physics
table, and waveform timbre configs. All of it is timing/configuration data in the same class as the
`StereoData` and note-length tables, so it is pinned as raw bytes; there are no hashes in this unit.

## Addresses are computed from the source

Every address in this contract is walked directly from `audio.asm`: an instruction's encoded length is
fixed by its mnemonic and operands, so summing those lengths from the section origin
(`SECTION "Audio", ROM0[$6480]`) yields the address of every label. The disassembly is dense with
labels whose names encode their own address (`Data_659B` at `$659B`, `WavePattern_6EA9` at `$6EA9`,
`.label_660E` at `$660E`), and each one is a checkpoint the walk must hit exactly — so the addresses of
the code-embedded blobs (the pause-tune notes, the level-up arpeggio) are proven by the checkpoints
that bracket them. Nothing is assembled.

---

## The three ID spaces

The game writes an effect ID to one of three wire variables (`wram.asm`); each is an independent
1-based space, so each becomes its own enum in `src/data/sfx.h`. `0` is the no-op in every space.

**`wNewSquareSFXID` → `SquareSfxId`.** A 1-based index into `SquareSFXStartPointers` (`audio.asm:5`,
`$6480`). Eight effects, in table order:

| ID | Name | Effect |
|---|---|---|
| 1 | `TINK` | menu cursor movement |
| 2 | `CHANGE_SCREEN` | menu change screen |
| 3 | `ROTATE_PIECE` | rotate piece |
| 4 | `SHIFT_PIECE` | shift piece |
| 5 | `GARBAGE_ATTACK` | garbage attack sweep |
| 6 | `LINE_CLEAR` | line clear |
| 7 | `TETRIS` | tetris |
| 8 | `LEVEL_UP` | level up |

**`wNewNoiseSFXID` → `NoiseSfxId`.** A 1-based index into `NoiseSFXStartPointers` (`audio.asm:25`,
`$64A0`). Four effects: `STACK_FALL` (1), `LOCK_PIECE` (2), `IGNITION` (3), `LIFTOFF` (4).

**`wNewWaveSFXID` → `WaveSfxId`.** There is **no wave pointer table**: `PlayWaveSFX` (`audio.asm:550`)
direct-compares the ID against `1` → `StartTetrisSweepSFX` and `2` → `StartGameOverSFX`. So the space
is just `TETRIS_SWEEP` (1) and `GAME_OVER` (2).

## The pointer tables and dispatch

Four fixed `dw` tables head the audio section and tile `[$6480, $64B0)`, ending exactly at
`MusicPointers` (`$64B0`):

| Table | Address | Entries |
|---|---|---|
| `SquareSFXStartPointers` | `$6480` | 8 |
| `SquareSFXContinuePointers` | `$6490` | 8 |
| `NoiseSFXStartPointers` | `$64A0` | 4 |
| `NoiseSFXContinuePointers` | `$64A8` | 4 |

Each entry is the address of a `Start…`/`Continue…` routine. Several continue slots share one routine:
square continue slots 2, 4, 5 are all `ContinueGenericSquareSFX`, and noise continue slots 1–3 are all
`ContinueGenericNoiseSFX`, so those table words are equal by value.

`PlaySquareSFX` (`audio.asm:832`) and `PlayNoiseSFX` (`audio.asm:855`) dispatch: a nonzero new ID locks
the channel and jumps through the start table; otherwise a nonzero current ID jumps through the
continue table.

### Quirks

- **No bounds mask on the lookup.** `LookupSoundPointer` (`audio.asm:757`) is 1-based and applies **no**
  range check — a square ID greater than 8 indexes past `SquareSFXContinuePointers` into the noise
  tables and beyond, and a noise ID greater than 4 indexes into `MusicPointers`. The game never writes
  such IDs. The port records the tables at their true lengths; it neither extends nor masks them.
- **Wave IDs ≥ 3 are silently ignored.** `PlayWaveSFX`'s direct-compare dispatch has no `else`; only
  `1` and `2` do anything.
- **`WavePattern_6EB9` is dead data.** The wave timbre at `$6EB9` (`audio.asm:1643`) is referenced
  nowhere in the ROM. It is ported anyway — it is part of the contiguous data the driver image carries —
  and flagged dead.
- **Game-over start pitch is randomized from `rDIV`.** `StartGameOverSFX` (`audio.asm:510`) reads the
  divider register and masks it to seed a start value. That randomness rides the hosted image and
  depends on the host's `rDIV` behavior.
- **The pause tune is driver-internal.** The two 4-byte pause-tune notes (`audio.asm:152-155`) are
  square-2 register images inherited from Super Mario Land, sequenced by `_UpdateAudio` through a
  `wPauseTuneTimer` countdown the upstream source itself comments is "a hack."
- **Upstream code bugs ride the image verbatim.** Several routines carry upstream-annotated mistakes (a
  `jp` that should be a `jr`, a dead `ld a, $00`, a high-bit write to `NR10` that does nothing, and
  unreachable vibrato branches). These are in the driver code, not this data; they are noted, not
  altered.

## The data blobs

Every effect routine reads a small register-image blob — a run of `db`/`dw` bytes copied into the APU
channel registers by `SetupChannel` (`audio.asm:724`). The blobs, in address order, with their walked
address and byte length, are pinned raw in the fixture. They range from 2-byte fragments
(`Data_685A`, a Tetris-sweep stage) to the two 36-byte liftoff ramps (`LiftOffNoiseData` `$6755`,
`LiftOffVolumeData` `$6779`). Representative anchors: the tink register image `Data_659B` (`$659B`) =
`$00 $B5 $D0 $40 $C7`; the four level-up arpeggio notes `LevelUpNote1`–`4` (`$6640`–`$664F`), which
share their first three register bytes and rise in frequency (A6, C#7, E7, A7); the line-clear image
plus its envelope and frequency ramps (`Data_6695`/`Data_669A`/`Data_66A5`).

## Residual driver data

At the tail of the audio section, a contiguous data run holds the driver's shared tables:

| Label | Address | Bytes | Role |
|---|---|---|---|
| `VibratoOffsets` | `$6DCB` | 55 | vibrato nibble-pair offset rows (11 × 5) |
| `NotePitches` | `$6E02` | 146 | 73 `dw` note-frequency table: a leading `$0F00` placeholder + the chromatic scale C2–B7 (uncopyrightable physics; upstream annotates its own off-by-one rows) |
| `Data_6E94` | `$6E94` | 21 | noise-channel note table (1 + 4 × 5) |
| `WavePattern_6EA9` | `$6EA9` | 16 | Tetris-sweep wave timbre |
| `WavePattern_6EB9` | `$6EB9` | 16 | **dead** (referenced nowhere) |
| `WavePattern_6EC9` | `$6EC9` | 16 | Type-A (Korobeiniki) wave timbre |
| `GameOverWavePattern` | `$6ED9` | 16 | game-over wave timbre |
| `DefaultWavePattern` | `$6EE9` | 16 | default wave timbre |

`DefaultWavePattern` ends at `$6EF9` — exactly the start of the note-length region, so the blob run
tiles into the music data with no gap.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_sfx.py`, `--all`): `src/data/sfx.h` (the three `SfxId`
  enums and the table/count/section constants — every value is an exact transcribed wire byte or a
  walked address) and `tests/fixtures/sfx_expected.h` (every blob as raw bytes over one flat pool, with
  its walked `{name, addr, length, poolOffset}`). Regenerate after any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/data/sfx.h` is header-only and consumed by later audio work; it
  carries no engine types. The enum names come from the upstream pointer-table comments and the wave
  dispatch.

### Transcription asserts

`parse_sfx.py` hard-errors (with a `file:line` citation) on any of: the instruction-size walk reaching
an address-encoding label whose name disagrees with the walked address; a pointer table not carrying
its expected routine targets or not summing to the `$64B0` `MusicPointers` address; `PlayWaveSFX` not
direct-comparing IDs 1 and 2 to their routines; `WavePattern_6EB9` being referenced anywhere; a blob
whose byte run is not its expected length; the blobs not being strictly increasing and non-overlapping;
and the blob tail not tiling into the note-length region at `$6EF9`.

The driver-internal RAM addresses, the APU registers, and the `Start…`/`Continue…` routines describe
the DMG memory map and the driver's own runtime — none of which the port reproduces here — so they are
recorded in this contract but do not appear in the port surface.

---

## Tested by

`tests/test_sfx.cpp` — the constants and the three enums pinned against this contract; a device-free
sweep proving the fixture blobs are sorted, non-overlapping, inside the audio section, and tile at their
tail into the note-length region; the pointer-table words read from the ROM with the aliased continue
slots checked equal; every blob's ROM bytes at its walked address compared byte for byte against the
fixture (which proves both the transcription and the address walk); the note-pitch, noise-note, and
wave-pattern pins; and the quirk/cross-link checks (the dead wave pattern still ported, the wave-ID
domain, the rising level-up arpeggio). The parser's own structural checks
(`tools/asm_parser/test_parse_sfx.py`) guard the scan against upstream changes.
