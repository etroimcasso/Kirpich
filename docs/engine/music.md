# Music

The music data surface: the identifiers the game selects songs by, and the addresses that locate the
song / channel / section graph, the stereo table, and the note-length tables inside the sound driver's
ROM image.

The song sequences themselves are not in this repository. The original sound driver reads its data by
dereferencing absolute ROM pointers, so it runs against the driver's own ROM image, and the note and
command streams live only there. What this page covers is the *map* of that image — the `MusicId`
identifiers and the address constants — plus the fixture that verifies the map against the ROM.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/music.h` | The `MusicId` enum and the eight address/span constants | **Generated — do not hand-edit.** |
| `tests/fixtures/music_expected.h` | The song/channel/section address graph, per-section `{addr, length, SHA-1}` pins, the `StereoData` rows, and the note-length region | **Generated — do not hand-edit.** |

Both are in `namespace kirpich` (the fixture is in `namespace kirpich::fixtures`), included as
`"data/music.h"` (the `src/` tree is on the library's include path). The header is the whole shipped
surface — there is no `.cpp`, no accessor, and no runtime table; it is identifiers and addresses that
later audio work reads.

## Using it

```cpp
#include "data/music.h"

using kirpich::MusicId;

std::uint8_t wire = static_cast<std::uint8_t>(MusicId::TYPE_A);   // -> 0x05, the byte the game writes
std::uint8_t index = wire & kirpich::kMusicIdIndexMask;          // -> 5, the 1-based song-table index

kirpich::kMusicSongCount;        // -> 17
kirpich::kMusicPointersAddr;     // -> 0x64B0, the 17-entry song-header pointer table
kirpich::kMusicSectionBase;      // -> 0x6F3F, start of the song/channel/section data
kirpich::kMusicSectionEnd;       // -> 0x7FC6, one past its end
kirpich::kStereoDataAddr;        // -> 0x6ABE, the 17x4 per-song stereo table
kirpich::kNoteLengthRegionBase;  // -> 0x6EF9
kirpich::kNoteLengthRegionEnd;   // -> 0x6F3F, one past the note-length tables
```

`MusicId` is an `enum class : std::uint8_t` holding the byte the game writes to select a song: the
17 songs are `0x01`–`0x11` (`TOP_SCORE` through `MULTIPLAYER_VICTORY`), plus two sentinels the driver
handles specially — `NONE = 0x00` (selecting nothing) and `STOP = 0xFF` (stopping all audio). The
address constants are `std::uint16_t`; `kMusicSongCount` and `kMusicIdIndexMask` are `std::uint8_t`.
All are `constexpr`, usable at compile time.

The index is masked, not bounds-checked: the driver computes the song-table index as
`value & kMusicIdIndexMask` (`& 0x1F`), which lets values `0x12`–`0x1F` index past the 17-entry
table. The game never writes those; the constant preserves the mask the original uses rather than
narrowing it.

`kStereoDataAddr`, `kMusicSectionEnd`, and the note-length bounds locate data the fixture pins and the
tests verify against the ROM; they exist for the audio host that will drive the ROM image, not for a
runtime lookup here.

## Regenerating the data

The header and the fixture are produced from the disassembly by the parser. Regenerate after repinning
the upstream source:

```sh
python3 tools/asm_parser/parse_music.py \
  --source-root ../tetris \
  --all \
  --header-out  src/data/music.h \
  --fixture-out tests/fixtures/music_expected.h
```

The parser reads `audio.asm` and `music.asm` by structure rather than by line number and stops with a
citation if anything has moved. It reconstructs every song, channel, and section from the source and
requires them to tile `[0x6F3F, 0x7FC6)` with no gap or overlap — the check that proves every
label-encoded address against the reconstructed byte lengths. It also requires the song pointer table
to equal the 17 song labels in order, every channel word to resolve to a known section or a
terminator, every section byte to be legal under the driver's command grammar, the stereo mode byte to
be `1` or `3`, and the note-length region to be exactly 70 bytes. Python 3 (standard library only); it
is a development tool and is never needed to build or test Kirpich.

`kStereoDataAddr` is the one value the parser does not derive: `StereoData` is a plain label inside
driver code with no address in its name, so it is entered directly as `0x6ABE` (the 68-byte block
occurs exactly once in the ROM) and the test reading the ROM at that address is what guards it.

## Changing it

The identifiers and addresses are fixed by the original and are not tuning knobs. The generated files
are overwritten on the next parser run, so never hand-edit them — to change a value you would change
the source and regenerate, but there is no reason to.

The full address map, the command grammar, the stereo panning behavior, and the driver entry points
are in [`../contracts/music.md`](../contracts/music.md).

## Testing

`tests/test_music.cpp` pins the constants and `MusicId` values against the contract, sweeps the fixture
graph to confirm it resolves and tiles the span, reads `StereoData` and the note-length region from the
ROM and compares them byte for byte (the read at `0x6ABE` is what guards the hand-entered address),
re-derives every song header and channel list from ROM bytes, and hashes every section's ROM bytes
against the fixture with each byte checked legal under the command grammar. The parser has its own
tests (`tools/asm_parser/test_parse_music.py`, run with
`python3 -m unittest tools.asm_parser.test_parse_music`).
