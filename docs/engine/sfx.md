# SFX

The sound-effect data surface: the three identifier spaces the game triggers effects by, and the
data blobs the driver's effect routines read — register images, envelope and frequency ramps, the
note-frequency table, the wave timbre patterns, and the noise-note and vibrato tables.

Like the music data, the effect routines themselves are driver code that runs against the sound
driver's own ROM image; the note-writing sequences live there, not in this repository. What this page
covers is the identifiers the game writes to its audio-state variables, plus the fixture that verifies
every data blob against the ROM.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/sfx.h` | The `SquareSfxId`, `NoiseSfxId`, and `WaveSfxId` enums and the eight table/count/section constants | **Generated — do not hand-edit.** |
| `tests/fixtures/sfx_expected.h` | Every effect and driver data blob as raw bytes over one flat pool, with each blob's walked `{name, addr, length, poolOffset}` | **Generated — do not hand-edit.** |

Both are in `namespace kirpich` (the fixture is in `namespace kirpich::fixtures`), included as
`"data/sfx.h"` (the `src/` tree is on the library's include path). The header is the whole shipped
surface — there is no `.cpp`, no accessor, and no runtime table; it is identifiers and addresses that
later audio work reads.

## Using it

```cpp
#include "data/sfx.h"

using kirpich::SquareSfxId;
using kirpich::NoiseSfxId;
using kirpich::WaveSfxId;

std::uint8_t wire = static_cast<std::uint8_t>(SquareSfxId::TETRIS);  // -> 7, the byte the game writes

kirpich::kSquareSfxCount;                // -> 8
kirpich::kNoiseSfxCount;                 // -> 4
kirpich::kWaveSfxCount;                  // -> 2
kirpich::kAudioSectionBase;              // -> 0x6480, start of the audio section
kirpich::kSquareSfxStartPointersAddr;    // -> 0x6480
kirpich::kSquareSfxContinuePointersAddr; // -> 0x6490
kirpich::kNoiseSfxStartPointersAddr;     // -> 0x64A0
kirpich::kNoiseSfxContinuePointersAddr;  // -> 0x64A8
```

The game keeps three separate 1-based effect-ID spaces — one per audio-state variable — so there are
three enums. `SquareSfxId` (`0`–`8`) indexes the square-channel effect tables; `NoiseSfxId` (`0`–`4`)
the noise-channel tables; `WaveSfxId` (`0`–`2`) the wave channel, which has no table (the driver
direct-compares its two IDs). `0` is the no-op in every space. Each is an `enum class : std::uint8_t`
holding the byte the game writes; the constants are `std::uint16_t` addresses and `std::uint8_t`
counts, all `constexpr`.

The square and noise ID lookups are 1-based and not bounds-checked, so an out-of-range ID would index
past its table; the game never writes one, and the enums stop at the real effect counts rather than
padding to the mask. The address constants locate the pointer tables the fixture pins and the tests
read from the ROM; they exist for the audio host that will drive the ROM image, not for a runtime
lookup here.

## Regenerating the data

The header and the fixture are produced from the disassembly by the parser. Regenerate after repinning
the upstream source:

```sh
python3 tools/asm_parser/parse_sfx.py \
  --source-root ../tetris \
  --all \
  --header-out  src/data/sfx.h \
  --fixture-out tests/fixtures/sfx_expected.h
```

The parser reads `audio.asm` by structure rather than by line number and stops with a citation if
anything has moved. It walks the section from its origin, summing each instruction's encoded length,
to compute every blob's address — the disassembly's address-encoding labels are checkpoints the walk
must hit exactly, so a wrong size fails loudly. It also requires the four pointer tables to carry
their expected routines and tile onto the music pointer table, the wave dispatch to direct-compare its
two IDs, each blob's byte run to be its expected length, and the blobs to be strictly increasing,
non-overlapping, and to tile at their tail into the note-length region at `0x6EF9`. Python 3 (standard
library only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

The identifiers, addresses, and data bytes are fixed by the original and are not tuning knobs. The
generated files are overwritten on the next parser run, so never hand-edit them.

The full ID spaces, the pointer tables and dispatch, the data blobs, the residual driver data, and the
effect quirks are in [`../contracts/sfx.md`](../contracts/sfx.md).

## Testing

`tests/test_sfx.cpp` pins the constants and the three enums against the contract, sweeps the fixture to
confirm the blobs are sorted, non-overlapping, in the audio section, and tiling into the note-length
region, reads the pointer-table words from the ROM with the aliased continue slots checked equal,
compares every blob's ROM bytes at its walked address against the fixture byte for byte (which proves
both the transcription and the address walk), and pins the note-pitch, noise-note, wave-pattern, and
level-up-arpeggio data. The parser has its own tests (`tools/asm_parser/test_parse_sfx.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_sfx`).
