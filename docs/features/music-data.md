# Music data

The game has 17 songs, played by a sound driver that walks a graph of songs, channels, and sections
laid out in the ROM. This unit ports the data that *locates and identifies* that music: the `MusicId`
values the game selects songs by, and the addresses of the graph, the per-song stereo table, and the
note-length tables. It is data only — nothing here makes a sound; the driver that plays the music is
hosted later, on the engine's audio backend.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `MusicId` | `src/data/music.h` | `enum class : uint8_t` — 17 songs (`0x01`–`0x11`) plus `NONE` / `STOP` |
| `kMusicPointersAddr`, `kMusicSongCount`, `kMusicIdIndexMask` | `src/data/music.h` | the song table's address, count, and index mask |
| `kMusicSectionBase`, `kMusicSectionEnd` | `src/data/music.h` | the span the song/channel/section graph occupies |
| `kStereoDataAddr` | `src/data/music.h` | the per-song stereo table's address |
| `kNoteLengthRegionBase`, `kNoteLengthRegionEnd` | `src/data/music.h` | the note-length tables' span |

The shipped surface is a single header of identifiers and addresses. The song/channel/section graph
itself — every song's channels, every channel's sections, and each section's length and content
hash — lives in the test fixture, where it verifies the map against the ROM. The exact grammar and the
driver's behavior are pinned in [`../contracts/music.md`](../contracts/music.md).

## Decisions

**The song sequences are never committed.** The note and command streams are the game's copyrightable
musical expression, so unlike the other data tables they are not stored in the repository in any form.
Each section is pinned instead by its address, length, and a SHA-1 of its ROM bytes — an
uncopyrightable fact — and the test recomputes that hash from the player's own ROM. The driver reads
the streams from its ROM image at run time; the port only needs to locate and verify them.

**Two mechanical tables are the exception.** `StereoData` (17 rows of mode / pan / channel-mask bytes)
and the note-length tables are timing and configuration values in the same class as the gravity and
scoring tables, not musical content, so they *are* pinned as raw bytes in the fixture and checked
against the ROM cell for cell.

**The runtime surface is minimal.** The header carries `MusicId` and the address constants and nothing
else — no accessor, no `.cpp`, no runtime table. The driver dereferences absolute ROM pointers, so
per-song structure that the port never indexes stays in the fixture rather than becoming a native
table. `MusicId` uses the wire byte the game writes to select a song, and the 1-based song-table index
is `value & kMusicIdIndexMask`.

**One address is entered by hand.** Every address here is read straight from a label whose name
encodes it, except `StereoData`, which is a plain label inside driver code. Rather than assemble the
driver to find it, `kStereoDataAddr` is entered directly as `0x6ABE` — the 68-byte stereo block occurs
exactly once in the ROM — and the test that reads the ROM at that address and compares the whole block
is what keeps it honest.

## Keeping it honest

The header and the fixture are generated from the disassembly by
`tools/asm_parser/parse_music.py`, which reconstructs every song, channel, and section from the source
and requires them to tile the music span with no gap or overlap — the check that proves every
label-encoded address against the reconstructed byte lengths. It also requires the song pointer table
to match the 17 song labels in order, every channel reference to resolve to a known section or a
terminator, every section byte to be legal under the driver's command grammar, and the stereo and
note-length data to have the exact shapes above; it stops with a source citation if any of them has
moved. The section hashes are computed from the reconstructed bytes and the test recomputes them from
the ROM, so the two derivations cross-check. See [`../engine/music.md`](../engine/music.md) for how to
regenerate it.

## Not here yet

Playing the music — hosting the driver on the engine's audio backend, handing it the ROM image, and
turning `MusicId` selections into sound — is the audio work, and it builds on this map. The stereo
panning the driver applies is faithfully reproduced there, quirks and all
([`../DESIGN.md`](../DESIGN.md) §6). This unit gives that work the identifiers and addresses to build
on and the fixture to check itself against.
