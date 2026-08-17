# Contract — Hosting the sound driver

Reverse-derived behavioral contract for running the game's original sound driver as a resident
machine and driving it from the port.

Kirpich does not reimplement the game's music and sound effects. It runs the driver the cartridge
shipped with — the same code and the same data, at the same addresses — on the audio engine's
virtual sound hardware, and talks to it the way the game did. Everything about how a song or an
effect sounds is therefore the driver's own behavior. What this contract fixes is the surface
around it: where the driver's image sits, which of its entry points run and when, which bytes the
game and the driver share, and the order a frame's requests reach it in.

The companion document [`audio-state.md`](audio-state.md) adjudicates every byte of the driver's RAM
window into interface and private classes. This document is the hosting side of that boundary: what
the port declares, what it performs each frame, and why the ordering matters.

Every address, entry, and line anchor below is from `tetris/tetris.asm` and `tetris/audio.asm`
(upstream `b95c668`). The line anchors are the authority the tests check against.

---

## The image and its entry points

The driver is one contiguous span of the cartridge, placed at the address it occupied there.

| Fact | Value | Source |
|---|---|---|
| Image base | `$6480` | `audio.asm:3` — `SECTION "Audio", ROM0[$6480]` |
| Image end | `$8000` (end of cartridge) | the image runs to the top |
| Per-frame entry | `$7FF0` | `audio.asm:1713`–`1715` — `UpdateAudio: jp _UpdateAudio` |
| Initialisation entry | `$7FF3` | `audio.asm:1716`–`1717` — `InitAudio: jp _InitAudio` |
| Stack top | `$CFFF` | `tetris.asm:309` — `ld sp, $CFFF` |
| Bank controller | none | the cartridge is 32 KiB and unbanked |

One span rather than two, because the driver reaches its own data by absolute address: the song
pointer table holds raw addresses into the sequence region, and the effect pointer tables hold raw
addresses of driver routines. Code and data cannot be separated. The two entry points sit near the
top of the cartridge with inert padding between them and the driver's body; taking everything from
the section base to the end carries both.

The image is the player's own cartridge content, so it ships as a file beside the binary
(`assets/audio/default/sound_driver.bin`, written by the ROM extractor) and is never compiled in.

A **second image** sits beside it: the startup routine described below. It is the port's own code,
so it is compiled in rather than read from disk, and it is placed clear of the driver's span so it
overwrites none of the cartridge's bytes.

---

## What the driver needs from the game's startup

This is the part that is easy to get wrong, because none of it is in the driver's own image and
none of it is obvious from reading the driver.

The driver was never a standalone program. It ran inside a machine the game had already prepared,
and it depends on three things the game's startup does before it is ever called. Hosting the image
and its entry points alone is not enough: the driver runs, its bookkeeping advances, a song
"plays" — and nothing is heard, or the machine executes an illegal instruction and halts.

| What | Where the game does it | What goes wrong without it |
|---|---|---|
| Switch the sound hardware on, route every channel to both outputs, set master volume | `tetris.asm:301`–`306` (`$FF26` ← `$80`, `$FF25` ← `$FF`, `$FF24` ← `$77`) | The chip is powered down at zero volume. Every register the driver writes is discarded; it plays to silence. |
| Put the stack at `$CFFF` | `tetris.asm:309` | The stack defaults to the top of work RAM. The driver pushes four register pairs on every pass, and they land on `$DFF8`/`$DFF9` — its own noise request mailbox — overwriting the request before it is read. Only the channels nearest the top of the block fail, which makes it look like one broken effect rather than a corrupted machine. |
| Clear the work RAM the driver lives in — 256 bytes down from `$DFFF` | `tetris.asm:311`–`317` | A machine's memory does not start zeroed. The driver keeps all of its state in that block: the pause command and pause-tune countdown it reads at the top of every pass, the four request mailboxes, and one data pointer per music channel. Started on garbage it diverts to its pause-tune path and never plays, consumes requests that were never made, or follows a garbage channel pointer into memory holding no code. |

The port performs all three in a small routine of its own (`src/vm/audio_boot.asm`), placed as the
second image and run once as the driver starts. It ends by calling the driver's own initialisation
at `$7FF3`, and that order matters: with the hardware still off, the register writes that
initialisation makes have no effect.

Note the hardware writes must be **executed by the machine**, not performed from outside. Switching
the sound chip on is an effect of the processor's write reaching it; setting the same bytes in memory
from the host sets bytes and powers nothing on.

---

## The frame relationship

The original calls `UpdateAudio` once per frame, from the main loop, after the current state's
handler has run (`tetris.asm:386`–`417`). The handler leaves its requests in memory; the driver
picks them up on that call.

The port keeps the same shape with one difference in who does the calling: **the audio engine runs
the per-frame entry itself**, at the console's clock, on its own thread. The port never calls it.
What the port does each frame is hand over the frame's requests, which the engine applies
immediately before it runs that entry.

The consequence worth stating plainly: a request handed over during frame N is seen by the driver
on frame N, exactly as a handler's memory write was. Nothing is deferred by a frame.

---

## The shared bytes

Six bytes cross between the game and the driver. Five live in the driver's RAM window; the sixth
lives with the game's own high-memory variables and is the one byte of the game's state the driver
reads.

| Byte | ROM label | Direction | Meaning |
|---|---|---|---|
| `$DF7F` | `wPauseUnpauseSound` | game writes | `1` suspends the current song, `2` resumes it |
| `$DFE0` | `wNewSquareSFXID` | game writes | square-channel effect to start |
| `$DFE8` | `wNewMusicID` | game writes | song to start |
| `$DFE9` | `wCurrentMusicID` | driver publishes | the song now playing |
| `$DFF0` | `wNewWaveSFXID` | both | wave-channel effect to start; also read back by the game |
| `$FFE4` | `hDemoNumber` | game writes | which attract-mode demo is running |

Each written byte is applied **once**, on the driver's next pass, and is never held or re-asserted —
which is exactly how the original's mailboxes behave: the driver consumes a request and zeroes the
byte (`audio.asm:90`–`95`).

`$DFE8` is not declared as a shared byte but as the song-start gesture (below); the two are the same
address reached two ways, and declaring it once keeps a song request from also being expressible as
a raw byte write.

### The demo gate is a genuine widening

`_UpdateAudio` reads `hDemoNumber` before it plays anything and, when it is non-zero, zeroes all
four request mailboxes so an attract-mode demo's recorded button presses make no sound
(`audio.asm:73`–`80`). The audio-state contract calls this the driver's only read of any state
outside its own window.

On the original both live in the same machine, so the driver simply reads the byte. Under hosting
the driver has its own machine and that byte does not exist there unless the port puts it there.
The port therefore sends it, **when it changes**. A write is applied once and never re-asserted, but
the byte itself persists in the driver's memory once written — so re-sending an unchanged value
every frame would be inert, and would put a write on the queue every frame for no effect.
`ActiveDemo`'s values are the game's own, written unaltered.

This is the one shared byte outside the driver's RAM window, and it widens the boundary the
audio-state contract drew. That contract records the widening.

---

## The two gestures

A request reaches the driver as one of two things.

**Starting a song** leaves the song number in `$DFE8`. The driver's `StartMusic` reads it, and the
stop id (`$FF`) is a legal value there: `StartMusic` branches on it into the stop routine
(`audio.asm:888`–`889`). A song request and a stop request are therefore the same gesture with
different values, and the port does not distinguish them.

**Initialising the driver** calls `$7FF3`. The game does this at boot (`tetris.asm:367`) and at five
points during play (`tetris.asm:901`, `2647`, `3059`, `4655`, `5272`), each time as `call InitAudio`
— a synchronous call, not a request left in memory. The routine clears the four current-effect bytes
and the four channel locks, resets the stereo mode, and mutes every channel (`audio.asm:804`–`830`).

At startup this call is the last thing the port's own startup routine does, so that it runs with the
hardware already on. During play it is performed directly.

Sound effects are not gestures. All three effect channels are request mailboxes, so all three are
reached as shared bytes.

---

## The order within a frame

A frame's requests are performed in a fixed order, and the order is the original's:

1. **The demo gate**, before anything else — the driver reads it before it plays.
2. **The initialisation**, if the frame asked for one — the game code that asks for one runs it
   before the frame's sounds are requested.
3. **The song**, if the frame asked for one.
4. **The frame's effect and pause requests.**

Steps 2 and 4 are the order that matters, and the top-out is the case that proves it. When the
second piece locks at the spawn position the game initialises the driver and then requests the
game-over sound, in that order, within one frame (`tetris.asm:5272`–`5276`). The driver's own pass
plays effects before it processes a song request (`audio.asm:83`–`86`), so an initialisation
performed as a song request would run *after* the game-over sound had already started — and since
initialisation mutes every channel and clears the locks, it would silence that sound on the frame it
began. Performing it as a call keeps it where the original had it: before the sound is requested.

This is why the initialisation is a call and not a `$FF` written to `$DFE8`, even though the
driver's stop routine is nothing but a call to the same entry (`audio.asm:879`–`881`). The two reach
identical code; they differ in *when* within the frame.

A request written and then withdrawn during the same frame never reaches the driver at all — the
piece rotation that turns out to collide does exactly this. Only the last value written in a frame
is handed over.

---

## What the port does not model

The driver's working state — channel bookkeeping, note timers, the music workspace, the pan
counters — has no representation in the port. It lives in the driver's own RAM inside the hosted
machine, at the addresses it always did. The port performs writes into that machine; it does not
mirror any of it. Mirroring would create two sources of truth for one piece of state.

The stereo panning the ROM carries is non-functional in the shipped game. The hosted driver runs the
same code, so the output is panned identically wrongly. No correction is applied.

---

## The surface

- **Hand-written:** `src/systems/sound.{h,cpp}` — the registration, the per-frame decision, and the
  sound system that performs it; `src/vm/audio_boot.asm` — the startup routine. There is no generated
  artifact in this unit; the driver's bytes are the cartridge's and are read from disk at run time.
- The song and effect identifier spaces are the existing ones (`src/data/music.h`,
  `src/data/sfx.h`); no new identifiers are introduced here.

---

## Tested by

`tests/test_sound.cpp` — the registration pinned field by field (both images with their bases and
policies, the load-from-disk posture for cartridge content, the per-frame entry, no bank controller,
and the stack top stated and proven to sit clear of the driver's working memory); the six shared
bytes pinned at their addresses, widths, directions, and declaration order; the two gestures pinned
(a song is a write to `$DFE8` carrying the value; the initialisation is a call; no effect lane is
declared, so asking for one is a loud error); the per-frame decision swept over an idle frame, a
fully-loaded frame, the stop id, and a withdrawn request; the demo gate proven to be re-sent when it
changes; the top-out frame proven to order its initialisation ahead of its sound; the **startup
routine assembled and pinned instruction by instruction** — the three hardware writes in the game's
order and values, the descending clear from `$DFFF`, the call to `$7FF3`, and that the hardware is
switched on before that call; and — against the real cartridge — the placement arithmetic, that
putting the image at its declared base lands both entry points where they are declared and both are
jumps.

What the tests deliberately do not cover is whether the result *sounds* right. That is a listening
check on a running build.
