# CPU Fidelity

**Date:** 2026-05-15; superseded as port-side work 2026-08-03
**Status:** Dropped as a port feature — the engine provides it

> **Superseded.** This document originally specified a port-local SameBoy integration under
> `src/vm/`. That will never be built: the engine owns the virtual-machine host and its preset
> routines, and `src/vm/` does not exist. What remains valid is the analysis below — *why* these
> two routines cannot be re-implemented natively, and what any host of them has to guarantee. It
> stands as the requirements record the engine surfaces satisfy.

## Why anything is virtualized at all

The port is native C++ throughout. Two behaviors resist that treatment, and only those two run the
original machine code:

**Piece randomization.** The routine folds the DMG's divider register, which increments at 16384 Hz
independently of the program counter. The value read therefore depends on how many cycles have
elapsed since power-on, and the resulting byte stream depends on cycle-exact execution. There is no
arithmetic to re-implement — the timing *is* the algorithm.

**The music and sound driver.** The driver writes audio-channel registers on a cycle-driven
cadence. Reproducing the output requires the CPU to run at the right rate *and* the audio unit to
interpret those writes with the same timing behavior. Re-implementing the driver natively would
reproduce the notes but not the artifacts that make the output identical.

Everything else in the game — collision, rotation, scoring, line clears, menus, multiplayer,
rendering — is deterministic given its inputs and is written as ordinary C++.

## Backend

[SameBoy](https://sameboy.github.io), MIT-licensed, providing cycle-accurate SM83 and audio-unit
emulation. One backend covers both routines. It is owned and vendored by the engine; the port never
touches it directly.

**Alternatives rejected:**

- **A hand-written minimal SM83 interpreter** — would require writing audio-unit emulation as well,
  doubling the surface. Bugs in homemade audio emulation are subtle output bugs, hard to detect and
  harder to attribute.
- **QEMU** — no first-class SM83 support. Its Z80 support does not transfer; the instruction sets
  differ.
- **Other emulator cores** — licensing and extraction effort weighed against SameBoy's clear
  permissive license and library-friendly C API.

## Throttling classes

The two routines have opposite requirements, and conflating them produces wrong output in one
direction or the other:

- **Randomization is output-cadence-irrelevant.** The ratio between CPU cycles and divider ticks is
  preserved at any clock rate, so the routine can run at host speed. The byte stream is the same.
- **The sound driver is output-cadence-relevant.** Register writes must occur at the original
  4.194304 MHz cadence, or the chiptune is wrong. The host must throttle it.

## Rate alignment for the audio path

Three rates must stay in proportion:

1. **Driver-write rate** — the driver, running on the virtual CPU throttled to 4.194304 MHz.
2. **Sample-production rate** — the emulated audio unit emitting PCM.
3. **Output-device rate** — the platform audio device consuming samples.

Mismatches produce wrong-pitch output, underflow glitches, or drift. In the adopted design these
are engine concerns, not port concerns. Details in [`audio-engine.md`](audio-engine.md).

## Byte input

The virtual machine needs the original ROM's bytes for the routines it hosts. Those arrive through
the asset-acquisition route — extracted on the user's machine, never shipped in the binary, since
they are copyrighted expression. See [`asset-acquisition.md`](asset-acquisition.md).

## What replaced this

The engine's VM host, its randomization preset, and its audio system. The remaining port-side work
is registration and the native logic around the virtualized calls, covered by the randomization and
audio-backend features rather than here.
