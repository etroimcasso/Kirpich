# Contract — Audio state (the Audio-RAM boundary)

Reverse-derived behavioral contract for the audio state of the port. Unlike the other state units,
**this one ships no C++ struct.** The port reproduces the original game's sound by running the ROM's
own sound driver as embedded code on the engine's virtual sound CPU, and driving it exactly as the
hardware did. The driver keeps its state in the `$DF70`–`$DFFF` block of work RAM — the same bytes,
at the same addresses — inside that machine. Mirroring those bytes into a second C++ copy would only
create two sources of truth for one piece of state, so the port does not: the audio state lives in
the driver's RAM, and this document draws the boundary around it.

The boundary matters because the driver and the game share that RAM window. A small, fixed set of
bytes is the **interface** the game uses to ask for sound; the rest is the driver's private working
memory that no game code ever touches. This contract adjudicates every byte of `$DF70`–`$DFFF` into
one of three classes and enumerates every game-side access, so that when the driver is wired up the
interface is transcribed exactly and nothing private is exposed.

Every address, use site, and adjudication below is from `tetris/tetris.asm` and `tetris/audio.asm`
(upstream `b95c668`); the layout is from `tetris/wram.asm`. The line anchors are the authority the
tests check against.

---

## How the game talks to the driver

The driver runs once per frame (`UpdateAudio` at `ROM0[$7FF0]` jumps to `_UpdateAudio`,
`audio.asm:59`) and is initialised by `InitAudio` → `_InitAudio` (`audio.asm:804`). Game code never
calls into the driver's routines directly and never reads its working memory. Instead it uses two
kinds of shared byte:

- **Cue lanes** — four "mailbox" bytes, one per sound channel. To ask for a sound, the game writes a
  sound **id** into the matching mailbox; the driver reads it on its next tick, starts the sound, and
  **zeroes the mailbox** (`audio.asm:90`–`94`). A mailbox value of `0` means "nothing requested"
  (no-op). This is the whole request protocol — write an id, the driver does the rest.
- **Slots** — bytes the game reads (or writes as a command) to observe or steer audio without
  requesting a specific sound: the pause command and the "what music is currently playing" read-back.

Everything else in `$DF70`–`$DFFF` is **driver-private**: channel state, note timers, the 96-byte
music workspace, the pan/stereo bookkeeping, and a handful of scratch bytes past the last named
label. Game code never reaches these; the proof is that a full scan of `tetris.asm` for static
operands touching this window finds **only** the interface bytes (see
`tests/fixtures/wram_expected.h`, the census, and the boundary test).

---

## The interface bytes

Six bytes, and only these six, are reached by game code.

| Address | ROM label | Class | Value domain | Game-side access sites |
|---|---|---|---|---|
| `$DF7F` | `wPauseUnpauseSound` | slot (command) | `1` = pause, `2` = unpause | written `1` at `tetris.asm:1773`/`4463`/`4506`, `2` at `4489`/`4544` (5 sites) |
| `$DFE0` | `wNewSquareSFXID` | cue lane | `SquareSfxId` (`0` = no-op) | 20 write sites, `tetris.asm:1180`–`6170` |
| `$DFE8` | `wNewMusicID` | cue lane | `MusicId` (`0` = no-op, `$FF` = stop) | 13 write sites, `tetris.asm:566`–`5781` |
| `$DFE9` | `wCurrentMusicID` | slot (read-back) | `MusicId` | read at `tetris.asm:1751` (danger-music check, `cp $08`) and `4818` (victory animation) |
| `$DFF0` | `wNewWaveSFXID` | cue lane (+ slot read) | `WaveSfxId` (`0` = no-op) | written at `tetris.asm:5276`; **read** at `1761` (game-over-buzzer check, `cp $02`) |
| `$DFF8` | `wNewNoiseSFXID` | cue lane | `NoiseSfxId` (`0` = no-op) | 5 write sites, `tetris.asm:2831`/`3017`/`3074`/`5306`/`5634` |

**The cue vocabulary is the existing id sets** — `MusicId` (`include`d from the music data),
`SquareSfxId` / `NoiseSfxId` / `WaveSfxId` (from the SFX data). No new types are introduced: a cue is
one of those enum values written to the lane, and `0` (`NONE`) in any lane is the no-op the driver
skips. `MusicId::STOP` (`$FF`) written to the music lane stops all audio.

**Notes on the interface bytes:**

- **`$DF7F` pause command.** The game writes `1` to pause and `2` to unpause; the driver branches on
  `cp a, 1` / `cp a, 2` at entry (`audio.asm:64`–`68`) and clears the byte to `0` at the end of every
  tick (`audio.asm:95`), so a stale command never re-fires.
- **`$DFF0` is both a cue lane and a slot read.** It is the wave-SFX mailbox (written at
  `tetris.asm:5276`), but one site *reads* it back (`1761`) to test whether the game-over buzzer
  (`WaveSfxId::GAME_OVER` = `$02`) is already queued before starting other audio. The read is over the
  pending-cue byte, not a separate "current" byte.
- **`$DFE9` is a read-back slot, not a cue.** The driver copies the started song's id here
  (`StartMusic`, `audio.asm:890`); the game reads it to decide whether to switch to danger music and
  to keep an animation running "until the music stops." The game never writes it.
- **The one computed cue.** Every square-cue write is an immediate id except `tetris.asm:5399`–`5400`
  (`ld a, c` / `ld [wNewSquareSFXID], a`), where `c` was loaded with the constant `$07`
  (`SquareSfxId::TETRIS`) earlier in the same path — inside the enum domain, not an escape from it.

---

## Driver-private bytes

None of the following is game-reachable; each is listed with the driver routine that owns it, so the
"private" claim is checkable. The full window tiles as: driver-private `$DF70`–`$DF7E`, the pause
slot `$DF7F`, the 96-byte music workspace `$DF80`–`$DFDF`, then the four channel groups
`$DFE0`–`$DFFF` in which only the four `wNew*` mailboxes and `wCurrentMusicID` are interface bytes.

| Address(es) | ROM label / region | Owner |
|---|---|---|
| `$DF70` | `wMusicCurrentChannel` | music engine's current-channel index (`PlayMusic`, `audio.asm:1240`) |
| `$DF71`–`$DF74` | gap | driver scratch (`$DF71` "temp" byte, `StartSFXCommon`/`ApplyVibrato`) |
| `$DF75`–`$DF77` | `wPanFrameCounter`, `wPanInterval`, `wPanCounter` | pan/stereo bookkeeping (`PanStereo`) |
| `$DF78` | `wMonoOrStereo` | pan mode — **written only by the driver** (`_InitAudio` sets `$03`, `audio.asm:817`; `InitStereo`, `:915`), read by `PanStereo`; no game access |
| `$DF79`–`$DF7A` | `wChannelEnableMask1`, `wChannelEnableMask2` | pan channel masks |
| `$DF7B`–`$DF7D` | gap | driver scratch |
| `$DF7E` | `wPauseTuneTimer` | pause-jingle countdown (`_UpdateAudio.playPauseTune`) |
| `$DF80`–`$DFDF` | gap | 96-byte music workspace: header block `$DF80` + four 16-byte channel blocks at `$DF90`/`$DFA0`/`$DFB0`/`$DFC0` (each: data pointer at `+4`, note timer at `+2`, channel lock in bit 7 of `+F`); laid out by `InitMusicChannels` (`audio.asm:1031`–`1075`) |
| `$DFE1` | `wCurrentSquareSFXID` | currently-playing square SFX; read only inside the driver (`CheckPlaying*`, `audio.asm:163`–`174`) |
| `$DFE2`–`$DFE4` | `wSquareSFXCounter`, `wSquareSFXNoteLength`, `wSquareSFXNoteCounter` | square-SFX progress |
| `$DFE5`–`$DFE7` | gap | driver scratch |
| `$DFEA`–`$DFEF` | gap | driver scratch |
| `$DFF1` | `wCurrentWaveSFXID` | currently-playing wave SFX; read only inside the driver |
| `$DFF2`–`$DFF3` | `wWaveSFXCounter`, `wWaveSFXNoteLength` | wave-SFX progress |
| `$DFF4`–`$DFF7` | gap | wave-SFX working bytes (`$DFF4`/`$DFF5`/`$DFF6`, the wave-sweep and game-over routines) |
| `$DFF9` | `wCurrentNoiseSFXID` | currently-playing noise SFX; read only inside the driver |
| `$DFFA`–`$DFFF` | *(past the last named label)* | driver working memory: the driver addresses `$DFFC` as the liftoff note counter (`audio.asm:460`). `$DFFF` doubles as the boot RAM-clear top (below) |

The four `wCurrent*SFXID` bytes and `wMonoOrStereo` are called out explicitly because they are easy
to mistake for interface bytes: the driver reads the "current SFX" bytes to decide sound priority,
but that decision is entirely inside `audio.asm` (`CheckPlayingTetrisSweep` / `GarbageAttack` /
`Tetris` / `LevelUp`, `audio.asm:157`–`175`, all called from the SFX-start routines at
`:263`–`:422`). Game code reads the *music* read-back (`wCurrentMusicID`), never the SFX ones.

---

## Boot, demo suppression, and the driver's one outside read

- **Boot is all-zero.** The startup RAM clear zeroes the whole upper work-RAM page down from `$DFFF`
  (`tetris.asm:312`, `ld hl, $DFFF`, "Clears the upper 256 bytes"), alongside the bank-0 clear
  (`:319`) and the stack set (`:309`, `ld sp, $CFFF`). The audio window therefore begins all-zero,
  the same state the driver's fresh RAM has when the port creates it. `$DFFF` is thus both the
  boot-clear top (an engine/mechanism address) and inside the driver's private tail.
- **Demo suppression.** While an attract-mode demo runs, the driver zeroes the four cue mailboxes
  *before* playing (`audio.asm:73`–`80`), so a demo's recorded button presses cannot trigger live
  sound. The gate it reads — `hDemoNumber` — is the driver's **only** read of any state outside the
  audio window, and it is a read, never a write. This is the single cross-boundary dependency the
  driver has on the rest of the machine.

---

## The surface

- **Parser-emitted** (`tools/asm_parser/parse_wram.py`, `--all`): `tests/fixtures/wram_expected.h` —
  the work-RAM layout table and the **census** of every static WRAM operand in `tetris.asm`
  (`{address, refCount}`, sorted). The census is what makes "these six bytes and no others" a
  testable claim: every audio-window byte a game operand names is one of the six, and every
  driver-private byte is absent because the driver's own accesses (in `audio.asm`) are not scanned —
  the driver runs as embedded code, not as ported C++, so its accesses are private by construction.
- **Hand-written:** the tests and this contract. There is no C++ struct and no `src/` file in this
  unit; the audio state is the driver's RAM.

---

## Tested by

`tests/test_audio_state.cpp` — the census-integrity sweep (rows sorted, positive ref counts, every
address inside work RAM); the **whole-map boundary guard** (every census address resolves to exactly
one owner across all of `$C000`–`$DFFF` — an engine-state field, an audio interface byte, a sprite /
board / staging window, the top-score region, or a boot/stack mechanism address; an unowned or
doubly-owned address fails); the audio-window pins (the six interface bytes present, `$DF7F` reached
five times, and **no other** `$DF70`–`$DFFF` byte in the census beyond those six plus the `$DFFF`
boot-clear); and the cue-vocabulary pins (`MusicId::STOP` = `$FF`, each SFX id set's `NONE` = `0`).
The parser's own checks (`tools/asm_parser/test_parse_wram.py`) guard the layout walk and the census
scanner against upstream changes.
