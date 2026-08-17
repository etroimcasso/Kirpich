# Audio Engine — Chiptune Backend

**Date:** 2026-05-15; revised 2026-08-03 for engine adoption; revised 2026-08-17 on delivery
**Status:** Complete — the driver is hosted and the frame tick drains the cue mailbox

## Concept

The chiptune backend hosts the original sound engine — the driver code plus the per-song and
per-effect data — on an emulated SM83 and audio unit. The CPU runs the driver at the original
4.194304 MHz clock; the audio unit receives the driver's register writes at the original cadence
and synthesizes PCM; the engine mixer feeds the platform audio sink. The result is chiptune output
identical to the original cartridge.

The driver registers with the engine's audio system, which hosts it on an internal virtual machine
and owns throttling and rate alignment. The port's job is describing the driver — where its image
goes, which entry points run, which bytes it shares with the game — and handing it the frame's
requests.

## Design decisions

### One backend: chiptune only

The port ships only the chiptune path. There is no audio-file replacement backend, no backend
selector, no settings toggle between audio sources.

**Rejected:** carrying dual-backend infrastructure inert ("ships, but only chiptune ever produces
samples"). Same reasoning as the asset-pack rejection — inert infrastructure invites later
activation and complicates the audio architecture for no present value.

### Rate alignment

Three rates must stay in proportion:

1. **Driver-write rate** — the driver inside the machine writing channel registers, with the CPU
   throttled to 4.194304 MHz.
2. **Sample-production rate** — the emulated audio unit emitting PCM.
3. **Sink consumption rate** — the platform audio device pulling samples at its own rate.

All three are engine-side concerns. The port declares nothing about timing.

### The engine runs the driver's tick; the port does not

The original calls the driver's per-frame entry from its main loop, after the state handler has run.
Under hosting the engine calls that entry itself, on its own production thread, at the console's
clock.

This re-scoped the port's audio beat. It was originally conceived as the driver's heartbeat — write
the driver's input state, run the CPU for a frame's cycles, collect the PCM. It is none of those:
it hands over the frame's requests and clears the cue mailbox, and the engine applies those requests
immediately before it runs the entry. A request made during a frame is seen by the driver on that
frame, exactly as a handler's memory write was.

**Rejected:** having the port drive the tick to keep the original's call shape. The engine owns the
production thread and the cycle budget; a port-side tick would either duplicate that or fight it,
and the original's *observable* ordering is preserved without it.

### Initialising is a call, not a request

The game initialises the driver at boot and at five points during play, each time as a direct
synchronous call. The driver's own stop routine is nothing but a call to that same entry, so a stop
request left in memory reaches identical code — which made routing the port's initialisation through
the music request byte look equivalent.

It is not equivalent, and the difference is audible. The driver plays effects before it processes a
song request, so an initialisation arriving as a song request runs *after* any effect that frame has
already started. Initialising mutes every channel and clears the channel locks, so it would silence
that effect on the frame it began.

The top-out is the case that proves it: the game initialises the driver and then requests the
game-over sound within one frame. Performing the initialisation as a call keeps it where the
original had it — ahead of the frame's sounds.

**Rejected:** the request-byte route, for the reason above. **Also rejected:** reproducing the
initialisation's effect by writing its outcome directly (the channel-lock and current-effect bytes)
rather than calling it — unnecessary once the call is available, and it would have meant the port
modelling driver-private state it deliberately does not touch.

### Sound effects have no play lane

The three effect channels are request mailboxes, so all three are reached as shared bytes.

**Rejected:** giving music and effects separate play lanes. There are three effect channels and only
two spare lanes, so two would have been lanes and the third a shared byte — an inconsistency at the
call site for no gain.

### The driver needs the machine its host prepared

The driver's image is not a standalone program, and this was the costliest thing the work found. It
ran inside a machine the game's startup had already set up, and it depends on three things that live
in that startup rather than in the audio section: the sound hardware switched on and routed at
volume (`tetris.asm:301`–`306`), the stack placed at `$CFFF` (`:309`), and its work RAM cleared
(`:311`–`317`).

Hosting the image and its entry points alone produced a driver that ran, advanced its own
bookkeeping, reported a song playing — and made no sound; or diverted to its pause-tune path and
never played; or halted the machine on an illegal instruction after following a garbage channel
pointer. With the stack defaulted to the top of work RAM, its per-pass register pushes landed on
`$DFF8`/`$DFF9`, its own noise mailbox, so exactly one channel failed while the others worked, which
reads as a broken effect rather than a corrupted machine.

The port performs all three in `src/vm/audio_boot.asm`, placed as a second image and run once as the
driver starts, ending with the driver's own initialisation.

**Rejected:** performing the hardware writes as slot writes from outside. Switching the sound chip on
is an effect of the processor's write reaching it; setting the same bytes from the host sets bytes and
powers nothing on. They have to be executed by the machine.

**Rejected:** performing the startup as handle gestures after `host()` returns. Gestures are
delivered to a live voice; issued before one exists they are discarded silently. The binding's
declared startup runs while the driver is being placed, which is the only point guaranteed to precede
its first pass.

### The demo gate is re-sent when it changes

While an attract-mode demo runs, the driver silences the frame's requests so recorded button presses
make no sound. The gate it reads is the game's own demo-number byte — the driver's only read of any
state outside its RAM window.

Under hosting the driver has its own machine, and that byte does not exist there unless the port
writes it. It is sent when it changes: a write applies once and is never re-asserted, but the byte
persists in the driver's memory once written, so re-sending an unchanged value every frame would be
inert and would queue a write per frame for no effect.

This is the one shared byte outside the driver's RAM window, and it widens the boundary the
audio-state contract drew. That contract records the widening.

### Music versus effect classification

The driver distinguishes music writes from effect writes by which routine path emitted them. In the
shipped mode this makes no difference to routing — everything reaches one hosted driver and channel
stealing happens exactly as it did on hardware.

Classification matters only for the anti-channel-stealing option, which is an engine capability
rather than a port one: it would split one driver's channel writes across parallel sound chips. The
port's side needs no change for it, and the driver's own code is never modified.

### Stereo panning preserved as broken

The ROM carries stereo panning data for the music, but the panning is non-functional in the shipped
game. The hosted driver runs the same code paths the original did, so the output is
broken-stereo-panned identically. No correction is applied.

### Pause

Pause state is driver-controlled. The port writes the command byte; the hosted driver reads it and
suspends or resumes the current song per the original logic.

## Implementation details

**Files:**

- `src/systems/sound.{h,cpp}` — the registration (`soundDriverId`), the per-frame decision
  (`gesturesFor`), the sound system that performs it (`SoundSystem`), and the dispatcher wiring
  (`installSoundTick`).
- `src/vm/audio_boot.asm` — the startup routine.
- `tests/test_sound.cpp` — the registration pinned field by field, the six shared bytes pinned at
  their addresses and directions, the two gestures pinned, the frame decision swept, the demo gate,
  the top-out ordering, the startup routine assembled and pinned instruction by instruction, and the
  placement arithmetic against the real cartridge.
- `tools/audio_check/` — a listening and measuring harness, built only under
  `-DKIRPICH_BUILD_AUDIO_CHECK=ON` and never in CI. Its `measure` mode counts non-silent frames, so
  "does this make sound" is answerable without a person listening. It found all three startup
  defects; the tests did not, and could not.

**Constants:**

- Cartridge image: one span at `$6480` through the end of the cartridge, read from
  `assets/audio/default/sound_driver.bin` rather than compiled in.
- Startup routine placed at `$6000`, compiled in (it is the port's own code).
- Per-frame entry `$7FF0`; initialisation entry `$7FF3`; stack top `$CFFF`.
- Shared bytes: `$DF7F`, `$DFE0`, `$DFE8`, `$DFE9`, `$DFF0`, `$DFF8`, and `$FFE4`.
- No bank controller; the cartridge is 32 KiB and unbanked.
- Cycles per frame: 70,224 at 4,194,304 Hz — the engine's, not the port's.

## Open questions / future work

- **The current-song read-back has no consumer yet.** Two screens read it in the original — one to
  decide when to switch to the danger music, one to hold an animation until the music ends. The
  accessor is in place; those screens arrive with their own work.
- **Verifying output against the original.** Behavior preservation requires identical audio. The
  tests prove the wiring, not the sound. A capture of PCM during a deterministic attract demo,
  compared against a reference capture from the original ROM running the same demo, is the check
  that would close this; until then the check is listening to a running build.
- **Device hot-swap.** If the user changes their default audio device while the game is running, the
  production rate may no longer match. Out of scope — restart picks up the new device.
- **Volume control.** No in-game volume; the OS mixer is the user's control. The engine exposes a
  level for the hosted driver's own bus if one is ever wanted.
