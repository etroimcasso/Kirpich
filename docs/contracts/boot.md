# Contract — The boot path

**Source of truth:** `tetris.asm` (kaspermeerts/tetris, DMG) at `b95c668`.
**Primary routine:** `Init` (`tetris.asm:264–384`), entered either at its top or at its `.softReset`
label (`:276`).
**Related sites:** the entry point (`:144–146`), `_Start` (`:152–153`), the RST vectors (`:7–10`),
`DMARoutine` (`:6676–6684`), `ClearTilemap9800` / `ClearTilemap` (`:6343–6354`), `InitAudio`
(`audio.asm:1716–1717` → `_InitAudio` `:804–830`), and the two soft-reset chord sites (`:391–394`
in `MainLoop`, `:4441–4444` in `HandleStartSelect`).

This document is the behavioral authority the port's tests are written against. It describes what the
original game does; the port reproduces that observable behavior natively.

---

## 1. One routine, two entry points

The cartridge's entry point is `$0100`, which holds `nop` / `jp _Start` (`:144–146`); `_Start` is one
instruction, `jp Init` (`:152–153`). Everything the boot does is in `Init`.

`Init` has two entry points and they differ in exactly one thing:

| Entry | Reached from | Runs |
|---|---|---|
| `Init` (`:264`) | power-on, through `_Start` | the work-RAM-bank-1 clear at `:265–274`, then falls into `.softReset` |
| `Init.softReset` (`:276`) | the four-button chord, from two sites | everything below `:276` only |

**The consequence is the whole of the "top scores survive a reset" behavior.** The clear at `:265–274`
covers `$D000–$DFFF`, and the top-score tables live at `$D000` (`wTypeBTopScores`, 1620 bytes, through
`$D653`) and `$D654` (`wTypeATopScores`, 270 bytes, through `$D761`). A soft reset enters below that
clear, so those two tables are the only game state that survives one. The upstream comment at
`:267–268` asks exactly this question — *"Why only the upper bank? Bug? Or is this so soft-resetting
doesn't erase the top scores?"* — and the answer is visible in the memory map.

The routine ends by falling through into `MainLoop` (`:386`). It never returns; there is no caller to
return to.

## 2. The six clear loops, and exactly what each covers

Every clear is a descending `ldd` walk with a byte count formed from the `b`/`c` register pair. The
counts matter more than the shape, so they are given as resolved extents:

| # | Lines | Start | Count | Extent | Runs on |
|---|---|---|---|---|---|
| 1 | `:265–274` | `$DFFF` | `$10 × $100` = 4096 | `$D000–$DFFF` | cold boot only |
| 2 | `:311–317` | `$DFFF` | `$100` = 256 | `$DF00–$DFFF` | both |
| 3 | `:319–327` | `$CFFF` | `$10 × $100` = 4096 | `$C000–$CFFF` | both |
| 4 | `:329–338` | `$9FFF` | `$20 × $100` = 8192 | `$8000–$9FFF` | both |
| 5 | `:340–345` | `$FEFF` | `$100` = 256 | `$FE00–$FEFF` | both |
| 6 | `:347–352` | `$FFFE` | `$80` = 128 | `$FF7F–$FFFE` | both |

Two of these are marked as suspected mistakes upstream and both are preserved:

- **Clear 2 is redundant on a cold boot.** `$DF00–$DFFF` is inside the range clear 1 just covered.
  The comment at `:312–313` reads *"Bug? Clears the upper 256 bytes of the upper work RAM bank a
  second time…"*. On a **soft** reset it is not redundant at all — it is the only clear that reaches
  the sound driver's work RAM, which clear 1 would have covered and which a soft reset skips.
- **Clear 6 runs one byte low.** HRAM is `$FF80–$FFFE`, which is 127 bytes; the loop clears 128,
  reaching `$FF7F`. The comment at `:348` reads *"Off by one, bug?"*. `$FF7F` is an unused
  input/output address; nothing reads it.

Clear 5 covers 256 bytes where sprite attribute memory is 160 (`$FE00–$FE9F`); the remaining
`$FEA0–$FEFF` is not addressable memory on this hardware. The comment at `:340` describes the overrun
with the wrong addresses; the extent above is what the registers actually produce.

## 3. What each clear means to the port's state

The port holds the same game state in structs rather than in a flat address space. Every clear maps
onto whole members:

| Clear | Port state it zeroes |
|---|---|
| 1 (`$D000–$DFFF`) | the two top-score tables — `HighScoreState::typeA` and `::typeB`. The rest of the range is padding (`wram.asm:62`) and the sound driver's work RAM. |
| 2 (`$DF00–$DFFF`) | the sound driver's work RAM, including the four cue mailboxes and the pause command the game writes — the port's `AudioCues`. Driver-side; see §6. |
| 3 (`$C000–$CFFF`) | `EngineState` (the object buffer, score, per-kind statistics, the piece ring), `SpriteRendererState` (`$C200`), `PlayingFieldState` (the attack row at `$C400` and the board at `$C800`). |
| 4 (`$8000–$9FFF`) | both background maps — `DisplayState::map` and `::secondMap`. The tile block in the same range has no port counterpart; see §5. |
| 5 (`$FE00–$FEFF`) | nothing. The port models the *source* of the sprite transfer (the object buffer at `$C000`, covered by clear 3), not its destination; see §8. |
| 6 (`$FF7F–$FFFE`) | `GameFlowState`, `MultiplayerState`, `DemoState`, the four high-score bytes at `$FFC6`/`$FFC7`/`$FFC8`/`$FFE8`, and the joypad snapshot at `$FF80`/`$FF81`. |

**`HighScoreState` sits on both sides of the soft-reset line.** Its two tables are in clear 1, which a
soft reset skips; its four bytes are in clear 6, which a soft reset runs. So a soft reset keeps the
tables and returns `newTopScore`, `topScoresRedrawRequested`, `newScoreRank` and `nameEntryColumn` to
their boot values. Treating the whole structure as preserved, or the whole structure as reset, is
wrong in one direction or the other.

## 4. Line-by-line disposition of `:264–384`

Every line of the routine, in order, with what it means natively. The ranges are contiguous, so the
whole routine is accounted for.

| Lines | What it is | Native disposition |
|---|---|---|
| `:265–274` | clear 1 | **Ported.** Zeroes the two top-score tables; cold boot only. |
| `:277–280` | `IEF_VBLANK` into the interrupt flag and enable registers, with interrupts disabled | Hardware. The engine owns interrupts and the frame; the port has no counterpart. |
| `:281–283` | zero the two scroll registers | Hardware. The port never scrolls the background; the visible screen is the map's top-left corner. |
| `:284` | zero `$FFA4` | Recorded, not ported — see §9. |
| `:285–287` | zero the display-status and the two serial registers | Hardware. |
| `:288–293` | turn the display on, then spin until the scanline counter reads `$94` | Hardware. This is the routine waiting for a frame boundary so the next write is safe. |
| `:294–295` | turn the display off with background and object layers selected | Hardware. |
| `:296–300` | the three palette registers | Hardware. The port's palettes are derived at art-upload time from the same values. |
| `:301–306` | sound on, all channels to both outputs, maximum volume | **Already executing** — see §6. |
| `:307–308` | the bank write | Recorded, not ported — see §9. |
| `:309` | the stack pointer to `$CFFF` | Hardware, and already asserted where it matters: the hosted driver's stack must sit below its own work RAM, which the sound contract pins. |
| `:311–317` | clear 2 | **Already executing** — see §6. |
| `:319–327` | clear 3 | **Ported.** |
| `:329–338` | clear 4 | **Ported**, as far as the port models it — see §5. |
| `:340–345` | clear 5 | No counterpart — see §3 and §8. |
| `:347–352` | clear 6 | **Ported.** |
| `:356–364` | the routine copy into high memory | Equivalence with a proof — see §8. |
| `:366` | `call ClearTilemap9800` | **Ported** — see §5. |
| `:367` | `call InitAudio` | **Already executing** — see §6. |
| `:369–370` | serial and frame interrupts enabled | Hardware. |
| `:371–376` | the three values the boot leaves behind | **Ported** — see §7. |
| `:377–378` | the display back on | Hardware. One bit of this register is modelled: the background-map select, which is what `DisplayState::displayed` carries. The value written here selects the first map. |
| `:379` | interrupts enabled | Hardware. |
| `:380–384` | zero the interrupt flag, both window-position registers, and the timer modulo | Hardware. |

The lines with no native counterpart are not omissions. The port draws through a display the engine
owns and runs its frame from the engine's run loop, so registers that configure the original's display
hardware, its interrupt controller, its stack and its timers have nothing to write to.

## 5. The tile-map clear

`:366` calls `ClearTilemap9800` (`:6343–6344`), which loads `$9BFF` and falls into `ClearTilemap`
(`:6345–6354`): a descending walk writing `" "` — the character map's space glyph, `$2F`, **not
zero** — to `$400` bytes, covering `$9800–$9BFF`.

That is the **first** background map only. The second map at `$9C00–$9FFF` was zeroed by clear 4 and
nothing fills it here. So the state the boot leaves is asymmetric and deliberately so:

- the first map holds `$2F` in all 1024 cells,
- the second map holds `$00` in all 1024 cells.

The tile block in the lower part of the same range (`$8000–$97FF`) is cleared by clear 4 and then
loaded by whichever screen runs first. The port has no state for the block's contents — the art is
uploaded once and a map cell's tile index is resolved against whichever set is loaded, which
`DisplayState::sheet` names. After the boot no set has been loaded at all; the first screen loads one
immediately, and `sheet` boots to the set that screen loads.

The order of the walk is unobservable — nothing else writes the map between the first cell and the
last — so the port fills the map rather than reproducing a descending loop.

## 6. The sound startup

Three fragments of this routine belong to the sound driver rather than to the game, and the port
already executes all three, on the machine, as the driver's own startup:

| Lines | What |
|---|---|
| `:301–306` | switch the sound hardware on, route every channel to both outputs, set maximum volume |
| `:311–317` | clear 2 — the driver's work RAM, 256 bytes down from `$DFFF` |
| `:367` | `call InitAudio`, which clears the driver's current-sound bytes and its four channel locks, sets the routing register, and mutes the channels (`audio.asm:804–830`) |

The driver's own image carries none of the first two, which is why hosting it alone leaves it running
silently — the hardware is never switched on and its work RAM is never cleared. The port's driver
startup performs all three, in this order, and a boot asks for that whole startup to be run again.
See [`sound-driver.md`](sound-driver.md).

**This is not the same request the game makes elsewhere, and the difference is audible.** Five or so
sites in the game do `call InitAudio` on their own — the game-over path, the Type B scoreboard, the
rocket's exit — and those are the initialisation entry alone, with no memory wipe. A boot needs the
wipe as well, because `_InitAudio` clears the four current-sound bytes and the four channel locks and
nothing else (`audio.asm:804–830`). In particular it leaves `wPauseTuneTimer` (`$DF7E`) alone, and
that byte is checked at the top of every driver pass (`:69–71`): non-zero sends the driver down its
pause-tune path, which never reaches `PlaySquareSFX` / `PlayNoiseSFX` / `PlayWaveSFX` / `StartMusic` /
`PlayMusic`. It also **latches** — at 16 the routine puts it back (`:145–148`, upstream comment *"Keep
the music paused by keeping wPauseTuneTimer non-zero. Seems like a hack..."*). So a player who pauses
and then resets a port that only initialised would lose every sound effect and all music for the rest
of the session, with the pause tune the one thing still audible.

**A soft reset needs this as much as a cold boot does**, because clear 2 is inside `.softReset`: the
driver's work RAM is wiped and re-initialised on every reset, even though the top scores are not.

## 7. The three values the boot leaves behind

After every clear, `:371–376` writes three bytes and those are the entire result the following screens
read:

| Line | Address | Value | Meaning |
|---|---|---|---|
| `:371–372` | `hGameType` `$FFC0` | `$37` | Type A |
| `:373–374` | `hMusicType` `$FFC1` | `$1C` | music A |
| `:375–376` | `hGameState` `$FFE1` | `$24` | the copyright screen |

The first two double as cursor positions and sprite numbers on the screens that read them, which is
why they are these values rather than a clean index; see [`core-enums.md`](core-enums.md). The third
is the state the frame dispatcher will run on the next pass.

## 8. The routine copy, and the two extra bytes

`:356–364` copies `DMARoutine` into high memory, because the sprite transfer it triggers makes all
other memory unreadable while it runs — so the routine that waits out the transfer has to execute from
the one region that stays readable.

The copy takes its length from `ld b, DMARoutine.end - DMARoutine + 2` (`:357`). `DMARoutine`
(`:6676–6684`) is **10 bytes**, and `hDMARoutine` (`hram.asm:107–108`) is `$FFB6` with 10 bytes
reserved. So the loop writes **12** bytes into a 10-byte space, and the two extra land on `$FFC0` and
`$FFC1`. The upstream comment at `:357` notes this is *"Exact same bug as in Super Mario Land"*.

**Those two addresses are `hGameType` and `hMusicType`, and `:371–374` overwrites both.** Between the
copy and those writes are two calls (`:366`, `:367`), each of which returns, and no branch of any
kind. The path is unconditional, so the two stray bytes cannot be read by anything before they are
replaced — on a cold boot, on a soft reset, on every pass without exception.

The port therefore carries this as an **equivalence**: the two bytes end at `$37` and `$1C` whatever
the copy put there, which is the same observable result, and no stray bytes are modelled. The port has
no high-memory-resident routine to copy in the first place — there is no transfer that blocks memory
access, because there is no memory to block.

## 9. Recorded, not ported

Three things in this routine produce no native code and are listed so a reader does not go looking for
them:

- **The bank write** (`:307–308`, `ld [$2000], a` with `a` = 1). The comment reads *"Bug? Tries to
  enable non-existent MBC Rom Bank"*. This cartridge has no bank controller; the write lands on
  read-only memory and does nothing. The port has no bank register, so the preservation is this record.
- **The two reset vectors** (`:7–10`), both `jp Init`. Nothing in the game issues either of the
  instructions that reach them, so both are unreachable.
- **`$FFA4`** (`:284`), which the disassembly marks *"Unused?"*. This line is its only writer and
  nothing reads it.

The two clear-loop overruns from §2 (one byte below high memory, and the unaddressable tail after the
sprite attributes) are the same class: real, preserved as described, and landing on nothing the port
models.

## 10. The chord, and what the reset does to the frame in progress

Two sites detect Start + Select + B + A and jump to `.softReset`. Both are described where they live —
[`dispatcher.md`](dispatcher.md) §4 for the frame-level check at `:391–394`, and
[`gameplay.md`](gameplay.md) for the duplicate inside `HandleStartSelect` at `:4441–4444`. Two
properties belong here, because they are properties of the reset rather than of the detection:

**The jump abandons the rest of the frame.** Both sites use `jp`, not `call`. From
`HandleStartSelect` that means the remainder of the gameplay frame — the recorded-input substitution,
the piece rotation and drop, the completed-row scan, the lock, the compaction and the score award —
does not run. `.softReset` ends by falling into `MainLoop`, so what follows a chord is a fresh pass of
the loop, not the remainder of the interrupted one. A port that returns from the chord check and
continues the frame would step a piece across a board the reset just cleared.

**The joypad snapshot is cleared, so the next frame reads every held button as newly pressed.** Clear
6 covers `hJoyHeld` and `hJoyPressed` at `$FF80`/`$FF81`, and `MainLoop` resumes at `ReadJoypad`,
which derives the pressed set
against the previous frame's held set — now zero. Two consequences follow, both the original's:

- a chord that is still held matches again on the next frame and resets again, repeatedly, until it is
  released;
- any button still down when the first screen runs reads as a fresh press to that screen.

## 11. How the port composes this

Three functions, in `src/systems/boot.h`:

| Function | Entry it ports |
|---|---|
| `coldBoot` | `Init` at `:264` — clear 1, then everything below |
| `softReset` | `Init.softReset` at `:276` — everything below, with the two tables carried across |
| `bootGame` | no counterpart; see below |

`softReset` is `coldBoot` with the two top-score tables saved and restored around it. It is written
that way rather than as a separate sequence so the two cannot drift apart, which is the same
relationship the original has between its two entry points.

**`bootGame` has no counterpart in the original, because the original has nowhere to keep a top score
between sessions.** The port writes its tables to the player's save file, so a launch loads them after
the boot has cleared them. That ordering is a correctness property — reversed, every launch would
clear the tables it had just loaded — and `bootGame` is where it lives so it is stated in one place
rather than assumed at the call site. A cold boot with no save file present leaves the tables zeroed,
which is exactly what clear 1 produces.

A soft reset performs no load. The original keeps the tables in memory across a reset and so does the
port; reading the file again would be a difference, not a fidelity gain.

## 12. Differences

Two, both stated rather than hidden:

**The sound driver is restarted one frame later.** The original runs its startup inline, part
way through the reset. The port sets the driver-restart request that the frame's sound step consumes,
so the driver is restarted at the next frame's sound step instead. Nothing observes the gap: no
state handler runs between the two points, and the sound step performs the re-initialisation before it
reads the frame's cues, so a cue raised by the first screen cannot be lost to it.

**The reset can run twice in one frame from the gameplay path.** `HandleStartSelect` fires it, the
frame ends, and the frame-level check then matches the same still-held chord and fires it again. The
original cannot do this — its jump leaves the loop entirely — but the two are indistinguishable,
because a reset applied to an already-reset machine restores the same two tables and produces the same
state. The alternative would be a flag threaded through the frame for no observable gain.
