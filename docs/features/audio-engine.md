# Audio Engine — Chiptune Backend

**Date:** 2026-05-15; revised 2026-08-03 for engine adoption
**Status:** In design

## Concept

The chiptune backend hosts the original sound engine — the driver code plus the per-song and
per-effect data — on an emulated SM83 and audio unit. The CPU runs the driver at the original
4.194304 MHz clock; the audio unit receives the driver's register writes at the original cadence
and synthesizes PCM; the engine mixer feeds the platform audio sink. The result is chiptune output
identical to the original cartridge.

Under the adopted engine design, the driver registers with the engine's audio system, which hosts
it on an internal VM and owns throttling and rate alignment. The port's job is registering the
driver and its data spans, and supplying the driver's input state each tick.

## Design decisions

### One backend: chiptune only

The port ships only the chiptune path. There is no audio-file replacement backend, no backend
selector, no settings toggle between audio sources.

**Rejected:** carrying dual-backend infrastructure inert ("ships, but only chiptune ever produces
samples"). Same reasoning as the asset-pack rejection — inert infrastructure invites later
activation and complicates the audio architecture for no present value.

### Rate alignment

Three rates must stay in proportion:

1. **Driver-write rate** — the driver inside the VM writing channel registers, with the CPU
   throttled to 4.194304 MHz.
2. **Sample-production rate** — the emulated audio unit emitting PCM.
3. **Sink consumption rate** — the platform audio device pulling samples at its own rate.

When (2) matches (3) no resampling is needed; when they differ the engine resamples. In the adopted
design all three are engine-side concerns.

### Frame-driven driver tick

The game ticks audio once per simulation frame. Each tick:

1. The driver's input state is written into the hosted program's memory — current music selection,
   pending sound-effect requests, mono/stereo flag, pause state.
2. The virtual CPU runs for one frame's worth of cycles at throttled speed.
3. The audio unit emits the PCM corresponding to that interval, which flows into the engine mixer.

The driver's own working state — channel bookkeeping, enable masks, note counters — lives inside
the hosted program's memory. The port never touches those addresses; it sets only the input ones.

### Music versus effect classification

The driver distinguishes music writes from effect writes by which routine path emitted them: the
effect start/continue routines versus the music driver routines. In the default mode this makes no
difference to routing — everything reaches one hosted instance and channel stealing happens exactly
as it did on hardware.

Classification matters only for the anti-channel-stealing option. The design captures the
classification points even in default mode, so that option can route without modifying the driver's
own code — which would mean modifying the upstream reference, and that is forbidden.

### Stereo panning preserved as broken

The ROM carries stereo panning data for the music, but the panning is non-functional in the shipped
game. The hosted driver runs the same code paths the original did, so the output is
broken-stereo-panned identically. No correction is applied.

### Pause

Pause state is driver-controlled. The port sets the input flag; the hosted driver reads it and
stops emitting register writes per the original logic. The sample output reflects whatever the
driver produces — silence during pause.

### Sink configuration

- Format: float32 stereo — the audio unit's output is naturally two-channel, and the broken panning
  is preserved.
- Sample rate: the device's actual rate, read back after the device opens, with production
  configured to match.
- Buffer: a few frames of audio — enough to absorb scheduling jitter without perceptible latency.

## Implementation details

**Files:**

- `src/systems/audio_backend.{h,cpp}` — driver registration and per-tick input marshalling.
- `tests/test_audio_backend.cpp` — boot smoke, pause behavior, effect-request handling, and a
  rate-alignment check that the sample count produced per frame matches the device rate divided by
  the frame rate.

**Constants:**

- Chiptune only; no audio-file replacement backend.
- Cycles per frame: 70,224 at 4,194,304 Hz.
- Sample format: float32 stereo.
- Production rate: matched to the audio device at runtime.
- Driver input addresses: taken from the disassembly's RAM map when the work lands.

## Open questions

- **Verifying output against the original.** Behavior preservation requires identical audio. The
  demo-replay integration test should capture PCM during a deterministic demo and compare it
  against a reference capture taken once from the original ROM running the same demo.
- **Device hot-swap.** If the user changes their default audio device while the game is running,
  the production rate may no longer match. Provisionally out of scope — restart picks up the new
  device. Revisit if it proves annoying in practice.
- **Volume control.** No in-game volume initially; the OS mixer is the user's control. Revisit if
  it turns out to be a usability gap.
