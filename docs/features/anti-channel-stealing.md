# Anti-Channel-Stealing

**Date:** 2026-05-15; revised 2026-08-03 for engine adoption
**Status:** In design

## Concept

The Game Boy has four hardware audio channels. When music is using all four and a sound effect
fires, the sound engine simply rewrites a channel's registers — the music's note on that channel is
gone. This is *channel stealing*, and it is what the original does.

The option routes music and sound effects to separate hosted driver instances so that an effect
never costs the music a voice. Both instances' output is mixed at the sink. The result: music plays
through effects without interruption.

**Off by default**, which preserves the original behavior byte-for-byte. The user turns it on in
settings. It is not covered by either of the project's two audio/asset reductions — it ships.

## Design decisions

### Two modes over one hosted driver design

- **Default (off).** The music and effect driver runs as a single hosted program. Channel stealing
  is intrinsic to running the original code that way, so the output is byte-identical to the
  cartridge with no special handling.
- **Enhanced (on).** Music and effects are hosted as separate driver instances, with their output
  routed through the engine mixer. The mixer's voice-per-cue model, which does not cut a playing
  voice to start a new one, is the natural substrate for this.

Switching modes changes routing, not the shape of the system.

For this game specifically the effect surface is light — square, wave, and noise effects, and
rarely more than one or two at once. Whether the option is audibly useful here is genuinely
uncertain; it ships regardless, and a user who does not want it can leave it off.

### Classification at the driver level

Each register write is attributed to music or effects by which driver routine emitted it. The
game's audio code keeps separate music and effect state, so the attribution is unambiguous at the
source. The chiptune backend exposes the interception points; this feature consumes them.

Crucially, this happens outside the driver's own code — the upstream reference is never modified.

### Overflow policy

If a new effect arrives with no free voice, the new sound is dropped and a diagnostic is logged
(an assertion in debug builds, a quiet log in release). For this game that is effectively
unreachable, but the policy is explicit rather than emergent. Failing safe — music protected, the
player's audio not corrupted — is the priority.

### Setting

- Key: `audio.anti_channel_stealing`, boolean, default `false`.
- Persisted in the engine's config file under the platform's preferences directory.
- Exposed wherever settings live; provisionally a command-line flag in the first implementation.

### Verification

- **Default mode:** byte-identical audio against the original, verified by the demo-replay
  integration test on the same demos used for the rest of the game.
- **Enhanced mode:** functionally correct (channels behave), and subjectively reasonable. Objective
  check: during overlapping cues, more voices are audible than the hardware's four. Subjective
  evaluation is a manual pass.

### Why this is a small feature here

The bulk of the engineering is the chiptune backend itself. This adds routing configuration on top
of it, and the engine's mixer already provides the substrate. The cost of running an extra hosted
instance is negligible on a modern host.

## Implementation details

**Files:**

- `src/systems/audio_routing.{h,cpp}` — routing layer over the chiptune backend's interception
  points.
- `tests/test_audio_routing.cpp` — routing correctness, voice allocation, overflow drop behavior,
  and the observable difference between the two modes.

**Constants:**

- Default mode: one hosted driver instance; stealing intrinsic.
- Enhanced mode: separate music and effect instances routed through the mixer.
- Overflow policy: drop the new effect, log a diagnostic.
- Setting key `audio.anti_channel_stealing`, default `false`.

## Open questions

- **Whether it is ever useful for this game.** Subjective, and the effect surface is light. Worth
  re-evaluating during manual verification.
- **Cost on low-power hardware.** Negligible on desktop; worth a look on the ARM64 targets.
- **How much the engine already provides.** The mixer may cover most of the routing directly, which
  would leave little for the port beyond configuration. Determined when the work lands.
