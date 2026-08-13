# Audio state

**Status:** Complete (boundary contract + census guard)

## Concept

The block of work RAM at `$DF70`–`$DFFF` is where the original game's sound driver keeps its state:
the four cue "mailboxes" the game writes to request sounds, the currently-playing music read-back and
pause command the game reads, and the driver's private working memory (channel state, note timers,
the music workspace, pan/stereo bookkeeping). This unit establishes **where the audio state lives in
the port and where the boundary between "the game's business" and "the driver's business" falls,
byte by byte.**

## Design decisions

**No C++ struct; no `src/` file.** Every other state unit mirrors a block of the original's RAM into
an idiomatic C++ struct. Audio does not, because audio is not reimplemented in C++ at all: the port
plays the original music and sound effects by running the ROM's own sound driver as embedded code on
the engine's virtual sound CPU, and driving it the way the hardware did. The driver keeps its state
in `$DF70`–`$DFFF` inside that machine. A parallel C++ copy of those bytes would be a second source
of truth for state the driver already owns — and would need an imperative shim to stay in sync with
it — so the port keeps exactly one copy: the driver's. The audio state is real, but it is held in
the sound machine's RAM, not in a port struct.

This is a deliberate reshape of the unit's original scope (an "audio state struct" mirroring the
labelled Audio-RAM fields). Two earlier framings were considered and dropped:

- **A C++ `AudioState` mirror with a bridge.** Rejected: it re-introduces the imperative
  read-write-sync layer the driver-hosting design is meant to remove, and duplicates state.
- **"Engine-owned" audio state.** Rejected: the engine hosts the driver but owns none of its state;
  every audio byte is the port's, held in the machine the port creates and controls.

**What the unit delivers instead:** a **boundary contract** (`docs/contracts/audio-state.md`) that
adjudicates every byte of `$DF70`–`$DFFF` into one of three classes — cue lane, slot, or
driver-private — with the game-side access sites enumerated; and a **census guard** that proves the
boundary mechanically.

**The interface is exactly six bytes.** Game code reaches only: the four cue mailboxes
(`wNewSquareSFXID` `$DFE0`, `wNewMusicID` `$DFE8`, `wNewWaveSFXID` `$DFF0`, `wNewNoiseSFXID`
`$DFF8`), the pause command (`wPauseUnpauseSound` `$DF7F`), and the current-music read-back
(`wCurrentMusicID` `$DFE9`). Everything else in the window is driver-private and never touched by
game code. The cue lanes carry the existing id sets (`MusicId`, `SquareSfxId`, `NoiseSfxId`,
`WaveSfxId`); no new types are introduced.

**The boundary is proven, not asserted.** The work-RAM layout parser gained a second pass — a
**census** of every static WRAM operand in `tetris.asm` (`{address, refCount}` rows in the shared
fixture). Because the driver runs as embedded code rather than ported C++, its own accesses are not
in `tetris.asm` and so never appear in the census; the driver-private claim for each byte is proven
by its *absence* from the game-side scan. The census is whole-work-RAM, not audio-only: a single
scan that a downstream test resolves so that **every** byte a game operand names across
`$C000`–`$DFFF` belongs to exactly one state surface. The same census backs the ownership guards of
the sprite, board, and top-score state units.

## Implementation details

- **Files:** `docs/contracts/audio-state.md` (the boundary contract), `tests/test_audio_state.cpp`
  (the census + boundary + interface tests), and — in the parser — a census pass added to
  `tools/asm_parser/parse_wram.py` with cases in `tools/asm_parser/test_parse_wram.py`. The shared
  fixture `tests/fixtures/wram_expected.h` is regenerated to carry the census table beside the
  layout table. No new `src/` file; no build-system change.
- **Census result:** 79 distinct work-RAM addresses across 262 static access sites; in the audio
  window, exactly the six interface bytes (`$DF7F` reached 5 times, `$DFE0` 20, `$DFE8` 13, `$DFE9`
  2, `$DFF0` 2, `$DFF8` 5) plus `$DFFF` (the boot RAM-clear top, a mechanism address).
- **Cue vocabulary:** `MusicId` (`NONE` = 0 no-op, `STOP` = `$FF`, songs `$01`–`$11`),
  `SquareSfxId` / `NoiseSfxId` / `WaveSfxId` (`NONE` = 0, wire values). A mailbox value of 0 is the
  no-op the driver skips.

## Open questions / future work

- **The driver hosting itself is a separate, later unit.** Registering the sound machine, placing the
  driver image, wiring the cue mailboxes and the pause/read-back slots, and playing audio are the job
  of the audio subsystem, built against the engine's sound-hosting surface. This unit only fixes the
  boundary that subsystem transcribes; it plays no sound.
- **The demo cross-read.** The driver reads one byte outside the audio window — the attract-demo
  number — to suppress live sound while a demo plays. When the driver is hosted, that one input has
  to be supplied to it; it is recorded here so the audio subsystem does not miss it.
