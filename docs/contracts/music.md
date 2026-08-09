# Contract — Music data

Reverse-derived behavioral contract for Kirpich's music data: the identifiers the game selects songs
by, the address map of the song / channel / section graph the sound driver walks, the per-song stereo
table, and the note-length tables. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors below are the authority the tests check against.

Unlike the other data tables, the music **sequences themselves are not committed to this repository in
any form.** The original sound driver reads its data by dereferencing absolute ROM pointers, so when
the driver runs on the engine's audio host it is handed the driver's own contiguous ROM image at its
native origin, and the note/command streams live only inside that image. This unit therefore ports the
*map* — the identifiers, addresses, spans, and structure needed to locate and verify the data — not
the streams. Section content is verified by hashing the ROM at test time (a SHA-1 over the bytes is an
uncopyrightable fact); the bytes are never emitted. Two mechanical-configuration tables are the
exception and **are** pinned as raw bytes, because they are timing/configuration values in the same
class as the gravity and scoring tables, not musical expression: `StereoData` and the note-length
region.

---

## The song table

`MusicPointers` (`audio.asm:37`, at ROM address **`$64B0`**) is 17 `dw` entries, one per song, in a
**1-based** table. Each entry is the address of that song's 11-byte header.

| Index | Song | Header |
|---|---|---|
| 1 | Top Score | `$6F3F` |
| 2 | Stage clear | `$6F4A` |
| 3 | Title screen | `$6F55` |
| 4 | Game over | `$6F60` |
| 5 | Type A (Korobeiniki) | `$6F6B` |
| 6 | Type B | `$6F76` |
| 7 | Type C (Bach, Menuet) | `$6F81` |
| 8 | Danger (Toreadors, Carmen) | `$6F8C` |
| 9 | Multiplayer round over | `$6F97` |
| 10–15 | Type B jingles #1–#6 | `$6FA2`–`$6FD9` |
| 16 | Rocket launch | `$6FE4` |
| 17 | Multiplayer victory | `$6FEF` |

`MusicId` (`src/data/music.h`) names these with the **wire byte the game writes to `wNewMusicID`**:
the 17 songs are `$01`–`$11`, plus two sentinels the driver special-cases — `NONE = $00` and
`STOP = $FF`.

### The index mask quirk

`StartMusic` (`audio.asm:883-897`) dispatches on `wNewMusicID`: `$00` returns immediately (no-op),
`$FF` stops all audio (`_StopAudio`), and any other value is masked with **`and a, $1F`**
(`audio.asm:893`) before indexing `MusicPointers`. The mask admits values `$12`–`$1F`, which index
**past** the 17-entry table into whatever bytes follow; the upstream source flags this with its own
comment ("Curious, there are only $11 songs?"). Those IDs are never written by the game. The port's
data surface neither extends the table to cover them nor narrows the mask — it records the table at
its true length and the mask at its true value, and leaves the out-of-range behavior exactly as the
original defines it. `value & kMusicIdIndexMask` is the 1-based table index.

## Song headers

Each song header is **11 bytes** (`music.asm`, `Song_XXXX`):

- **byte 0** — loaded into `$DF80` by `InitMusicChannels` (`audio.asm:1036-1039`) and, by inspection,
  never read afterward. It is `$00` for every song. Carried in the fixture as `byte0`, recorded here
  as apparently unused.
- **bytes 1–2** — the note-length table pointer (into the note-length region below).
- **bytes 3–10** — four `dw` channel pointers (square 1, square 2, wave, noise). `$0000` marks an
  unused channel; the driver skips it (`PlayMusic`, `audio.asm:1244-1246`).

## Channels and sections

A **channel** (`music.asm`, `ChannelN_XXXX`) is a `dw` list of **section** addresses. It ends one of
three ways, matching the driver's `Command_00` handling (`audio.asm:1193-1232`):

- `$0000` — **stop**: the channel (and, when it is the song's controlling channel, the song) ends.
- `$FFFF` followed by a target address — **repeat**: playback loops to the target.
- **no terminator** — the list runs to the next label. Non-repeating songs stop as soon as one channel
  reaches a `$0000`, so a channel that is not the longest need not carry its own terminator; its extent
  is the gap to the next datum. This is the same adjacency the upstream dumper relies on.

A **section** (`music.asm`, `Section_XXXX`) is a byte stream of driver commands, decoded by
`PlayMusic.readCommand` (`audio.asm:1252-1299`). Sections are **shared** — one section is referenced by
several channels and songs. The command grammar:

| Byte | Meaning |
|---|---|
| `< $92` | a note (pitch index into `NotePitches`); on the noise channel, restricted to `{1,6,11,16}` |
| `$01` | rest |
| `$00` | end of section |
| `$9D` + 3 bytes | load a wave pattern (the three following bytes are operands) |
| `$Ax` | set the note length to note-length-table entry `x` (the low nibble) |

A section may reach its extent without an explicit `$00`; the song ends via another channel's stop.

The whole graph occupies **`[$6F3F, $7FC6)`** — songs, channels, and sections tile that span with no
gap or overlap. `$7FC6` is the end of the music data.

## StereoData

`StereoData` (`audio.asm:988-1005`, ROM address **`$6ABE`**) is **17 rows × 4 bytes**, one row per
song: a mono/stereo mode byte (`1` or `3`), a pan interval, and two `rNR51` channel-enable masks.
`InitStereo` (`audio.asm:899-925`) selects the row by song ID; `PanStereo` (`audio.asm:927-984`)
applies it each frame.

The stereo panning is a **preserved-as-broken** behavior (see [`../DESIGN.md`](../DESIGN.md) §6): the
original's pan logic is faithfully reproduced, quirks included. `StereoData` and the driver's
`PanStereo` path ride the hosted driver image verbatim; the port neither corrects nor "improves" the
panning. The rows are pinned as raw bytes.

## Note-length tables

The note-length region is **`[$6EF9, $6F3F)`**, 70 bytes (`audio.asm:1682-1711`, `Data_6EF9` and the
rows that follow it to the end of the audio section). A song's header points at one of these tables;
the `$Ax` command indexes it by the command's low nibble through the pointer the driver keeps at
`$DF81` (`audio.asm:1258-1276`). The region ends with a stray `dw $783C` (bytes `$3C $78`) carried
along by adjacency. The whole region is pinned as raw bytes.

## Driver entry points

`UpdateAudio` and `InitAudio` (`audio.asm:1713-1717`, at ROM address **`$7FF0`**) are `jp` stubs into
the driver body — the driver's public entry points. They are recorded here for the audio host that
will drive the ROM image; they are not data this unit emits.

---

## Parser-emitted vs. hand-written

- **Parser-emitted** (`tools/asm_parser/parse_music.py`, `--all`): `src/data/music.h` (the `MusicId`
  enum and the address/span constants — every value is an exact transcribed address or wire byte) and
  `tests/fixtures/music_expected.h` (the song/channel/section address graph, per-section
  `{addr, length, SHA-1}` pins, the `StereoData` rows, and the note-length region). Regenerate after
  any upstream repin; do not hand-edit.
- **Hand-written port-design:** `src/data/music.h` is header-only and consumed by later audio work; it
  carries no engine types. `kStereoDataAddr` is the one constant not derived by the parser — the
  `StereoData` label sits inside driver code with no address in its name — so it is entered directly as
  `$6ABE` (the 68-byte block occurs exactly once in the ROM) and guarded by the test reading the ROM at
  that address.

The driver-internal RAM addresses (`$DF80`, `$DF81`, …), the `rNR51`/`rDIV` registers, and the
`$7FF0` entry points describe the DMG memory map and the driver's own runtime — none of which the port
reproduces here — so they are recorded in this contract but do not appear in the port surface.

### Transcription asserts

`parse_music.py` hard-errors (with a `file:line` citation) on any of: `MusicPointers` not exactly 17
entries, or not equal to the 17 `Song_` label addresses in order; the four SFX pointer tables not
summing to the `$64B0` `MusicPointers` address; `StartMusic` missing its `and a, $1F` mask; a
`StereoData` row not 4 bytes, a mode byte not in `{1, 3}`, or not exactly 17 rows; the note-length
region not exactly 70 bytes; a song header not 11 bytes, or a note-length pointer outside
`[$6EF9, $6F3F)`; a channel word that resolves to neither a known section nor a terminator; a section
byte illegal under the command grammar; two labels claiming one address; and — the central check —
songs, channels, and sections failing to tile `[$6F3F, $7FC6)` exactly, which proves every
label-encoded address against the reconstructed byte lengths.

---

## Tested by

`tests/test_music.cpp` — the constants and `MusicId` values pinned against this contract; a device-free
sweep proving the fixture graph resolves and tiles the span; the `StereoData` rows and the note-length
region read from the ROM and compared byte for byte (the read at `$6ABE` is also what guards the
hand-entered address); a re-derivation of every song header and channel list from ROM bytes; and a
per-section SHA-1 of the ROM bytes against the fixture, with every section byte checked legal under the
command grammar. The parser's own structural checks (`tools/asm_parser/test_parse_music.py`) guard the
scan against upstream changes.
