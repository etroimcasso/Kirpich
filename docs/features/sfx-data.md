# Sound-effect data

The game plays short sound effects — a menu tink, a piece rotation, a line clear, a rocket liftoff —
through the same sound driver that plays the music. Each effect is a small routine that writes the
audio channel registers from a block of configuration bytes. This unit ports the data that
*identifies and configures* those effects: the IDs the game triggers effects by, and the register
images, ramps, and tables the effect routines read. It is data only — nothing here makes a sound; the
driver that runs the effects is hosted later, on the engine's audio backend.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `SquareSfxId` | `src/data/sfx.h` | `enum class : uint8_t` — 8 square-channel effects plus `NONE` |
| `NoiseSfxId` | `src/data/sfx.h` | `enum class : uint8_t` — 4 noise-channel effects plus `NONE` |
| `WaveSfxId` | `src/data/sfx.h` | `enum class : uint8_t` — 2 wave-channel effects plus `NONE` |
| `kAudioSectionBase`, the four `…PointersAddr`, the three `…Count` | `src/data/sfx.h` | the audio section origin, the four pointer-table addresses, and the effect counts |

The shipped surface is a single header of identifiers and addresses. Every effect and driver data blob
— the register images, the envelope and frequency ramps, the note-frequency table, the vibrato and
noise-note tables, and the five wave timbre patterns — lives in the test fixture as raw bytes, where it
verifies against the ROM. The pointer tables, the dispatch, and the effect quirks are pinned in
[`../contracts/sfx.md`](../contracts/sfx.md).

## Decisions

**Three ID spaces, three enums.** The game keeps three independent 1-based effect-ID spaces, one per
audio-state variable (square, noise, wave). Rather than fold them into one enum, each becomes its own,
so an ID is always paired with its channel and the `0` no-op is unambiguous. The square and noise names
come from the game's own pointer-table comments; the wave names from the two IDs its dispatch compares.

**The data is configuration, pinned raw.** Unlike the music sequences, an effect's bytes are register
images and envelope/frequency ramps — timing and configuration in the same class as the gravity and
scoring tables, not copyrightable content. Every blob is pinned as raw bytes in the fixture and checked
against the ROM cell for cell. There are no content hashes in this unit.

**The runtime surface is minimal.** The header carries the three enums and the address/count constants
and nothing else — no accessor, no `.cpp`, no runtime table. The driver reads its blobs from the ROM
image at run time, so the blob bytes and their addresses stay in the fixture rather than becoming
native tables the port would never index.

**Addresses are computed, not assembled.** Every blob's address is walked from the disassembly by
summing instruction lengths from the audio section origin. The many labels whose names encode their own
address are checkpoints the walk must hit exactly, so even the effect blobs embedded between code — the
pause-tune notes and the level-up arpeggio — are located and proven without assembling anything.

## Keeping it honest

The header and the fixture are generated from the disassembly by `tools/asm_parser/parse_sfx.py`, which
walks the audio section to place every blob and requires the four pointer tables to carry their
expected routines and tile onto the music pointer table, the wave dispatch to compare its two IDs, each
blob's byte run to be its expected length, and the blobs to be strictly increasing, non-overlapping,
and tiling at their tail into the note-length region; it stops with a source citation if any of them
has moved. The test then reads the player's ROM and compares every blob's bytes at its walked address,
so the two derivations cross-check. See [`../engine/sfx.md`](../engine/sfx.md) for how to regenerate it.

## Not here yet

Playing the effects — hosting the driver on the engine's audio backend and turning ID selections into
sound — is the audio work, and it builds on this data. The game-over effect's `rDIV`-seeded start pitch
and the driver's other runtime behavior are reproduced there. This unit gives that work the identifiers
and configuration to build on and the fixture to check itself against.
