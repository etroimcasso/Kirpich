#!/usr/bin/env python3
"""Parser for Kirpich's music data surface - the song/channel/section address map the ROM's sound
driver walks, plus the per-song stereo table and the note-length tables.

The music data is NOT native constexpr arrays. The engine-hosted sound driver dereferences absolute
ROM pointers, so the load unit is the driver's own contiguous image at its native origin, and
per-song structure exists here only as metadata + verification fixtures. The note/command streams
themselves are the canonical copyrightable class and are NEVER committed in any form: sections are
pinned by {address, length, SHA-1 of the ROM bytes} - the reconstructed bytes are hashed and
discarded, never emitted. The two mechanical-configuration exceptions, pinned as raw bytes because
they are timing/config values in the same class as the gravity and scoring tables: StereoData (17x4)
and the note-length region ([$6EF9, $6F3F)).

Two source files, both read as text (the parser never reads the ROM - the C++ tests do that,
independently, so the two derivations cross-check):

  audio.asm   the sound driver. Yields: MusicPointers (17 song-header addresses at $64B0), the
              StartMusic index mask ($1F), the StereoData rows, and the note-length region bytes.
  music.asm   the (dump_music.py-generated) data: Song_/ChannelN_/Section_ labels whose names encode
              their own ROM addresses. Every label is reconstructed to bytes so the graph tiles
              [$6F3F, $7FC6) with no gap or overlap - the same first-pass-pointers / adjacency-extent
              traversal dump_music.py uses, done over the asm text instead of the ROM.

Emission set (single `--all` invocation):

  src/data/music.h                   MusicId enum (wire values) + span/address constants. Fully
                                     parser-emitted (every value is an exact transcribed address or
                                     wire byte), CharTile/SpriteId header precedent.
  tests/fixtures/music_expected.h    song records, channel records over a flat section-address pool,
                                     section {addr, length, sha1} rows, StereoData raw rows, and the
                                     70-byte note-length region.

kStereoDataAddr is the one constant NOT derived by the parser: StereoData is a plain label inside
driver code with no address-encoding name, and this parser uses no assembled-symbol table. It is
hand-entered (0x6ABE - the 68-byte StereoData block occurs exactly once in the ROM) and guarded by
tests/test_music.cpp reading the ROM at that address and asserting the full block equals the fixture.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Any deviation from the
expected structure is a hard error with a source citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

# audio.asm anchors.
AUDIO_SECTION_ORIGIN = 0x6480          # SECTION "Audio", ROM0[$6480]
MUSIC_POINTERS_ADDR = 0x64B0           # derived by walking the four SFX dw tables; asserted
MUSIC_SONG_COUNT = 17
MUSIC_ID_INDEX_MASK = 0x1F             # StartMusic: `and a, $1F`; asserted present
STEREO_ROW_COUNT = 17
STEREO_ROW_BYTES = 4
STEREO_MODE_VALUES = (1, 3)            # mono (1) or stereo (3); asserted per row

# The four fixed-size `dw` pointer tables that precede MusicPointers in the Audio section. Walking
# them (rather than decoding the code that never appears before MusicPointers) yields $64B0.
SFX_POINTER_TABLES = (
    ("SquareSFXStartPointers", 8),
    ("SquareSFXContinuePointers", 8),
    ("NoiseSFXStartPointers", 4),
    ("NoiseSFXContinuePointers", 4),
)

# The note-length region is the tail of the Audio section: [$6EF9, $6F3F), 70 bytes, ending exactly
# where the Music section begins.
NOTE_LENGTH_REGION_BASE = 0x6EF9
NOTE_LENGTH_REGION_END = 0x6F3F         # == kMusicSectionBase; asserted by tiling
NOTE_LENGTH_REGION_LEN = NOTE_LENGTH_REGION_END - NOTE_LENGTH_REGION_BASE  # 70
NOTE_LENGTH_FIRST_LABEL = "Data_6EF9"

# music.asm anchors.
MUSIC_SECTION_ORIGIN = 0x6F3F          # SECTION "Music", ROM0[$6F3F]
MUSIC_SECTION_END = 0x7FC6             # dump_music.py's end sentinel; asserted by tiling
SONG_LEN = 11                          # db(1) + dw length-table(2) + 4x dw channel(8)
SONG_CHANNELS = 4

# StereoData: a plain label in driver code with no address-encoding name. Hand-entered and
# test-guarded (the 68-byte block occurs exactly once in the ROM); the parser emits, not derives, it.
STEREO_DATA_ADDR = 0x6ABE

# The music-section grammar (dump_music.py / audio.asm command dispatch at $6C5C).
CMD_END_SECTION = 0x00
CMD_REST = 0x01
CMD_WAVE_LOAD = 0x9D                    # $9D + 3 operand bytes
NOTE_MAX = 0x92                         # a note byte is < $92
NOISE_NOTES = (1, 6, 11, 16)            # noise-channel note bytes ({1,6,11,16})
CHANNEL_STOP = 0x0000
CHANNEL_REPEAT = 0xFFFF

# music_macros.asm note names -> pitch value (Notes macro: db name + (octave - 2) * 12).
NOTE_NAME_VALUE = {
    "C_": 1, "C#": 2, "D_": 3, "D#": 4, "E_": 5, "F_": 6,
    "F#": 7, "G_": 8, "G#": 9, "A_": 10, "A#": 11, "B_": 12,
}

# MusicId enumerators (port-authored, names from MusicPointers' comments; values are the wire byte
# the game writes to wNewMusicID). The 17 songs are $01..$11; NONE/STOP are the sentinels StartMusic
# special-cases ($00 = no-op, $FF = stop).
MUSIC_ID_ENUM = [
    ("NONE", 0x00, "no-op (StartMusic returns)"),
    ("TOP_SCORE", 0x01, "Top Score"),
    ("STAGE_CLEAR", 0x02, "Stage clear"),
    ("TITLE", 0x03, "Title screen"),
    ("GAME_OVER", 0x04, "Game over"),
    ("TYPE_A", 0x05, "Type A - Korobeiniki"),
    ("TYPE_B", 0x06, "Type B"),
    ("TYPE_C", 0x07, "Type C - Bach, Menuet"),
    ("DANGER", 0x08, "Danger - Toreadors, Carmen"),
    ("ROUND_OVER", 0x09, "Multiplayer round over"),
    ("TYPE_B_JINGLE_1", 0x0A, "Type B Jingle #1"),
    ("TYPE_B_JINGLE_2", 0x0B, "Type B Jingle #2"),
    ("TYPE_B_JINGLE_3", 0x0C, "Type B Jingle #3"),
    ("TYPE_B_JINGLE_4", 0x0D, "Type B Jingle #4"),
    ("TYPE_B_JINGLE_5", 0x0E, "Type B Jingle #5"),
    ("TYPE_B_JINGLE_6", 0x0F, "Type B Jingle #6"),
    ("ROCKET_LAUNCH", 0x10, "Rocket launch"),
    ("MULTIPLAYER_VICTORY", 0x11, "Multiplayer victory"),
    ("STOP", 0xFF, "stop all audio (_StopAudio)"),
]

_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
_MUSIC_LABEL_RE = re.compile(r"^(Song|Channel[1-4]|Section)_([0-9A-Fa-f]{1,4})::$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_music"


# --- Small helpers ------------------------------------------------------------------------------

def _strip_comment(line: str) -> str:
    return line.partition(";")[0].strip()


def _parse_byte(token: str, path: Path, lineno: int) -> int:
    """A `db` operand: `$hex` or decimal. Range-checked to a byte."""
    token = token.strip()
    try:
        value = int(token[1:], 16) if token.startswith("$") else int(token, 10)
    except ValueError as exc:
        raise ParseError(f"{path}:{lineno}: not a byte value: {token!r}") from exc
    if not 0 <= value <= 0xFF:
        raise ParseError(f"{path}:{lineno}: value {value} out of byte range: {token!r}")
    return value


def _resolve_word(token: str, path: Path, lineno: int) -> int:
    """A `dw` operand: `$hhhh`, or a Song_/ChannelN_/Section_ label whose name encodes its address."""
    token = token.strip()
    if token.startswith("$"):
        try:
            value = int(token[1:], 16)
        except ValueError as exc:
            raise ParseError(f"{path}:{lineno}: not a word literal: {token!r}") from exc
    else:
        m = re.match(r"^(?:Song|Channel[1-4]|Section)_([0-9A-Fa-f]{1,4})$", token)
        if not m:
            raise ParseError(f"{path}:{lineno}: unresolvable dw operand: {token!r}")
        value = int(m.group(1), 16)
    if not 0 <= value <= 0xFFFF:
        raise ParseError(f"{path}:{lineno}: word {value} out of range: {token!r}")
    return value


def _find_sole_label(lines: list[str], label: str, path: Path) -> int:
    hits = [i for i, ln in enumerate(lines) if ln.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label}:: defined more than once (lines {found})")
    return hits[0]


# --- audio.asm: MusicPointers, index mask, StereoData, note-length region -----------------------

def parse_music_pointers(lines: list[str], path: Path) -> list[int]:
    """The 17 song-header addresses at $64B0, and the derived address, asserted == $64B0."""
    # Derive MusicPointers' address by walking the four fixed dw tables from the Audio section origin.
    addr = AUDIO_SECTION_ORIGIN
    for label, count in SFX_POINTER_TABLES:
        start = _find_sole_label(lines, label, path)
        dw = 0
        for offset, raw in enumerate(lines[start + 1:], start=start + 2):
            body = _strip_comment(raw)
            if not body:
                continue
            if _LABEL_RE.match(body):
                break
            if not body.startswith("dw "):
                raise ParseError(f"{path}:{offset}: expected dw inside {label}: {raw.strip()!r}")
            dw += len(body[3:].split(","))
        if dw != count:
            raise ParseError(f"{path}: {label} has {dw} words, expected {count}")
        addr += dw * 2
    if addr != MUSIC_POINTERS_ADDR:
        raise ParseError(
            f"{path}: computed MusicPointers address ${addr:04X}, expected ${MUSIC_POINTERS_ADDR:04X}"
        )

    start = _find_sole_label(lines, "MusicPointers", path)
    values: list[int] = []
    for offset, raw in enumerate(lines[start + 1:], start=start + 2):
        body = _strip_comment(raw)
        if not body:
            continue
        if _LABEL_RE.match(body):
            break
        if not body.startswith("dw "):
            raise ParseError(f"{path}:{offset}: expected dw inside MusicPointers: {raw.strip()!r}")
        for token in body[3:].split(","):
            values.append(_resolve_word(token, path, offset))
    if len(values) != MUSIC_SONG_COUNT:
        raise ParseError(
            f"{path}: MusicPointers has {len(values)} entries, expected {MUSIC_SONG_COUNT}"
        )
    return values


def assert_index_mask(lines: list[str], path: Path) -> None:
    """StartMusic masks the ID with `and a, $1F` before the table lookup. Assert it is present."""
    start = _find_sole_label(lines, "StartMusic", path)
    for raw in lines[start + 1:]:
        body = _strip_comment(raw)
        if _LABEL_RE.match(body) and not body.startswith("StartMusic"):
            break
        m = re.match(r"^and\s+a,\s*\$([0-9A-Fa-f]+)$", body)
        if m:
            if int(m.group(1), 16) != MUSIC_ID_INDEX_MASK:
                raise ParseError(
                    f"{path}: StartMusic masks with ${int(m.group(1), 16):02X}, "
                    f"expected ${MUSIC_ID_INDEX_MASK:02X}"
                )
            return
    raise ParseError(f"{path}: StartMusic has no `and a, ${MUSIC_ID_INDEX_MASK:02X}` mask")


def parse_stereo_data(lines: list[str], path: Path) -> list[list[int]]:
    """StereoData: 17 rows x 4 bytes (mode, pan interval, two rNR51 masks). Mode in {1, 3}."""
    start = _find_sole_label(lines, "StereoData", path)
    rows: list[list[int]] = []
    for offset, raw in enumerate(lines[start + 1:], start=start + 2):
        body = _strip_comment(raw)
        if not body:
            continue
        if _LABEL_RE.match(body):
            break
        if not body.startswith("db "):
            raise ParseError(f"{path}:{offset}: expected db inside StereoData: {raw.strip()!r}")
        cells = [_parse_byte(tok, path, offset) for tok in body[3:].split(",")]
        if len(cells) != STEREO_ROW_BYTES:
            raise ParseError(
                f"{path}:{offset}: StereoData row has {len(cells)} bytes, expected {STEREO_ROW_BYTES}"
            )
        if cells[0] not in STEREO_MODE_VALUES:
            raise ParseError(
                f"{path}:{offset}: StereoData mode byte {cells[0]} not in {STEREO_MODE_VALUES}"
            )
        rows.append(cells)
    if len(rows) != STEREO_ROW_COUNT:
        raise ParseError(
            f"{path}: StereoData has {len(rows)} rows, expected {STEREO_ROW_COUNT}"
        )
    return rows


def parse_note_length_region(lines: list[str], path: Path) -> list[int]:
    """The 70 bytes [$6EF9, $6F3F): db/dw data from Data_6EF9 to the end of the Audio section."""
    start = _find_sole_label(lines, NOTE_LENGTH_FIRST_LABEL, path)
    values: list[int] = []
    for offset, raw in enumerate(lines[start + 1:], start=start + 2):
        body = _strip_comment(raw)
        if not body:
            continue
        if body.startswith("SECTION"):
            break
        if body.startswith("db "):
            for tok in body[3:].split(","):
                values.append(_parse_byte(tok, path, offset))
        elif body.startswith("dw "):
            for tok in body[3:].split(","):
                word = _resolve_word(tok, path, offset)
                values.append(word & 0xFF)
                values.append((word >> 8) & 0xFF)
        else:
            # Data labels (Data_6F05 etc.) inside the region are fine; other content is not.
            if _MUSIC_LABEL_RE.match(body) or re.match(r"^[A-Za-z_][A-Za-z0-9_]*::$", body):
                continue
            raise ParseError(
                f"{path}:{offset}: unexpected line in the note-length region: {raw.strip()!r}"
            )
    if len(values) != NOTE_LENGTH_REGION_LEN:
        raise ParseError(
            f"{path}: note-length region collected {len(values)} bytes, "
            f"expected {NOTE_LENGTH_REGION_LEN}"
        )
    return values


# --- music.asm: the song/channel/section graph --------------------------------------------------

@dataclass
class Element:
    """A single reconstructed label body in the Music section, with its ROM address and bytes."""
    name: str
    kind: str            # "Song" | "Channel1".."Channel4" | "Section"
    addr: int
    body_lines: list[tuple[int, str]] = field(default_factory=list)  # (lineno, stripped text)
    data: bytes = b""


@dataclass
class Song:
    addr: int
    byte0: int
    length_table_addr: int
    channel_addrs: list[int]   # 4 entries; 0 = unused


@dataclass
class Channel:
    addr: int
    terminator: str            # "none" | "stop" | "repeat"
    repeat_target: int         # valid iff terminator == "repeat"; else 0
    sections: list[int]        # section addresses in order


@dataclass
class Section:
    addr: int
    length: int
    sha1: str


def _line_bytes(kind: str, body: str, path: Path, lineno: int) -> list[int]:
    """Reconstruct one Music-section body line to its raw ROM bytes (the macros of music_macros.asm)."""
    head = body.split()[0]
    if head == "db":
        return [_parse_byte(tok, path, lineno) for tok in body[3:].split(",")]
    if head == "dw":
        out: list[int] = []
        for tok in body[3:].split(","):
            word = _resolve_word(tok, path, lineno)
            out += [word & 0xFF, (word >> 8) & 0xFF]
        return out
    if head == "Notes":
        args = [a.strip() for a in body[len("Notes"):].split(",")]
        if len(args) % 2 != 0:
            raise ParseError(f"{path}:{lineno}: Notes needs name/octave pairs: {body!r}")
        out = []
        for i in range(0, len(args), 2):
            name, octave = args[i], args[i + 1]
            if name not in NOTE_NAME_VALUE:
                raise ParseError(f"{path}:{lineno}: unknown note name {name!r}")
            value = NOTE_NAME_VALUE[name] + (int(octave) - 2) * 12
            if not 0 <= value < NOTE_MAX:
                raise ParseError(
                    f"{path}:{lineno}: note {name}{octave} = {value} out of range [0, ${NOTE_MAX:02X})"
                )
            out.append(value)
        return out
    if head == "Noise":
        out = []
        for tok in body[len("Noise"):].split(","):
            idx = int(tok.strip())
            if not 0 <= idx <= 3:
                raise ParseError(f"{path}:{lineno}: Noise arg {idx} not in 0..3")
            out.append(NOISE_NOTES[idx])
        return out
    if head == "Rest":
        return [CMD_REST]
    if head == "EndSection":
        return [CMD_END_SECTION]
    raise ParseError(f"{path}:{lineno}: unknown Music body directive: {body!r}")


def parse_music_asm(text: str, path: Path) -> tuple[list[Element], dict[int, Element]]:
    """Collect every Song_/ChannelN_/Section_ label, reconstruct its bytes, and verify the section
    origin. Returns the address-sorted element list and an address->element map."""
    lines = text.splitlines()

    if not any(f"ROM0[${MUSIC_SECTION_ORIGIN:04X}]" in ln.upper() for ln in lines):
        raise ParseError(f"{path}: no `SECTION \"Music\", ROM0[${MUSIC_SECTION_ORIGIN:04X}]`")

    elements: list[Element] = []
    current: Element | None = None
    for lineno, raw in enumerate(lines, start=1):
        body = _strip_comment(raw)
        if not body or body.startswith("INCLUDE") or body.startswith("SECTION"):
            continue
        m = _MUSIC_LABEL_RE.match(body)
        if m:
            name = body[:-2]
            current = Element(name=name, kind=name.split("_")[0], addr=int(m.group(2), 16))
            elements.append(current)
            continue
        if _LABEL_RE.match(body):
            raise ParseError(f"{path}:{lineno}: unexpected non-music label: {body!r}")
        if current is None:
            raise ParseError(f"{path}:{lineno}: body line before any label: {raw.strip()!r}")
        current.body_lines.append((lineno, body))

    # Reconstruct bytes per element. Two labels can never claim the same address; the name-encoded
    # address is proven against the reconstructed byte lengths by assert_tiling (adjacency).
    by_addr: dict[int, Element] = {}
    for el in elements:
        if el.addr in by_addr:
            raise ParseError(
                f"{path}: two labels at ${el.addr:04X}: {by_addr[el.addr].name} and {el.name}"
            )
        raw_bytes: list[int] = []
        for lineno, body in el.body_lines:
            raw_bytes += _line_bytes(el.kind, body, path, lineno)
        el.data = bytes(raw_bytes)
        by_addr[el.addr] = el

    elements.sort(key=lambda e: e.addr)
    return elements, by_addr


def assert_tiling(elements: list[Element], path: Path) -> None:
    """Every byte of [$6F3F, $7FC6) belongs to exactly one label: addr[i] + len == addr[i+1], and the
    last label reaches the end sentinel. This proves the reconstructed byte lengths against the
    label-encoded addresses (the adjacency-extent contract dump_music.py relies on)."""
    if elements[0].addr != MUSIC_SECTION_ORIGIN:
        raise ParseError(
            f"{path}: first label at ${elements[0].addr:04X}, expected ${MUSIC_SECTION_ORIGIN:04X}"
        )
    for cur, nxt in zip(elements, elements[1:]):
        end = cur.addr + len(cur.data)
        if end != nxt.addr:
            raise ParseError(
                f"{path}: {cur.name} (${cur.addr:04X}) + {len(cur.data)} bytes = ${end:04X}, "
                f"but next label {nxt.name} is at ${nxt.addr:04X} (gap or overlap)"
            )
    last = elements[-1]
    end = last.addr + len(last.data)
    if end != MUSIC_SECTION_END:
        raise ParseError(
            f"{path}: last label {last.name} ends at ${end:04X}, "
            f"expected the music end ${MUSIC_SECTION_END:04X}"
        )


def build_graph(
    elements: list[Element], by_addr: dict[int, Element], pointers: list[int], path: Path
) -> tuple[list[Song], list[Channel], list[Section]]:
    """Interpret songs, channels, and sections from the reconstructed elements; cross-check the graph
    against MusicPointers and the section grammar."""
    songs: list[Song] = []
    channels: list[Channel] = []
    sections: list[Section] = []

    song_elems = [e for e in elements if e.kind == "Song"]
    if len(song_elems) != MUSIC_SONG_COUNT:
        raise ParseError(f"{path}: {len(song_elems)} Song labels, expected {MUSIC_SONG_COUNT}")

    # MusicPointers <-> the 17 Song labels, in table order.
    if pointers != [e.addr for e in song_elems]:
        raise ParseError(
            f"{path}: MusicPointers {[f'${p:04X}' for p in pointers]} != "
            f"Song labels {[f'${e.addr:04X}' for e in song_elems]}"
        )

    for el in song_elems:
        if len(el.data) != SONG_LEN:
            raise ParseError(f"{path}: {el.name} is {len(el.data)} bytes, expected {SONG_LEN}")
        byte0 = el.data[0]
        length_table = el.data[1] | (el.data[2] << 8)
        if not NOTE_LENGTH_REGION_BASE <= length_table < NOTE_LENGTH_REGION_END:
            raise ParseError(
                f"{path}: {el.name} length-table pointer ${length_table:04X} outside "
                f"[${NOTE_LENGTH_REGION_BASE:04X}, ${NOTE_LENGTH_REGION_END:04X})"
            )
        chan_addrs: list[int] = []
        for slot in range(SONG_CHANNELS):
            addr = el.data[3 + 2 * slot] | (el.data[4 + 2 * slot] << 8)
            if addr != 0:
                target = by_addr.get(addr)
                if target is None or target.kind != f"Channel{slot + 1}":
                    raise ParseError(
                        f"{path}: {el.name} channel slot {slot + 1} -> ${addr:04X} is not a "
                        f"Channel{slot + 1} label"
                    )
            chan_addrs.append(addr)
        songs.append(Song(el.addr, byte0, length_table, chan_addrs))

    for el in (e for e in elements if e.kind.startswith("Channel")):
        words: list[int] = []
        for _, body in el.body_lines:
            if not body.startswith("dw "):
                raise ParseError(f"{path}: {el.name} has a non-dw line: {body!r}")
            for tok in body[3:].split(","):
                words.append(_resolve_word(tok, path, el.addr))
        terminator, repeat_target, secs = _classify_channel(words, el, by_addr, path)
        channels.append(Channel(el.addr, terminator, repeat_target, secs))

    for el in (e for e in elements if e.kind == "Section"):
        _assert_section_grammar(el, path)
        sections.append(Section(el.addr, len(el.data), hashlib.sha1(el.data).hexdigest()))

    return songs, channels, sections


def _classify_channel(
    words: list[int], el: Element, by_addr: dict[int, Element], path: Path
) -> tuple[str, int, list[int]]:
    """A channel's dw list ends in $FFFF,target (repeat), $0000 (stop), or nothing (adjacency)."""
    terminator, repeat_target = "none", 0
    sections = list(words)
    if words and words[-1] == CHANNEL_STOP:
        terminator, sections = "stop", words[:-1]
    elif len(words) >= 2 and words[-2] == CHANNEL_REPEAT:
        terminator, repeat_target, sections = "repeat", words[-1], words[:-2]
    for addr in sections:
        target = by_addr.get(addr)
        if target is None or target.kind != "Section":
            raise ParseError(
                f"{path}: {el.name} references ${addr:04X}, which is not a Section label"
            )
    return terminator, repeat_target, sections


def _assert_section_grammar(el: Element, path: Path) -> None:
    """Every byte of a section is a legal driver command: a note < $92, $01 rest, $00 end, $9D + 3
    operands, or $Ax set-length. A section may end at its extent without an explicit $00."""
    data = el.data
    i = 0
    while i < len(data):
        b = data[i]
        if b == CMD_END_SECTION or b == CMD_REST:
            i += 1
        elif b == CMD_WAVE_LOAD:
            if i + 3 >= len(data):
                raise ParseError(f"{path}: {el.name} $9D without 3 operand bytes at +{i}")
            i += 4
        elif (b & 0xF0) == 0xA0:
            i += 1
        elif b < NOTE_MAX:
            i += 1
        else:
            raise ParseError(f"{path}: {el.name} illegal byte ${b:02X} at +{i}")


# --- Emit: src/data/music.h ---------------------------------------------------------------------

def emit_header(pointers: list[int], source_commit: str) -> str:
    section_base = pointers[0]
    width = max(len(name) for name, _, _ in MUSIC_ID_ENUM)
    enum_rows = "\n".join(
        f"    {name:<{width}} = 0x{value:02X},  // {desc}"
        for name, value, desc in MUSIC_ID_ENUM
    )
    return f"""#pragma once
{common.banner("parse_music.py", source_commit)}\
// MusicId + the music-data address map. The song sequences themselves are never committed: the
// engine-hosted sound driver reads the driver's ROM image at these absolute addresses, so the port
// carries only the identifiers the game writes to wNewMusicID and the spans that locate the data.
// Section content is verified by hashing the ROM at test time (see tests/test_music.cpp).

#include <cstdint>

namespace kirpich {{

// The byte the game writes to wNewMusicID. StartMusic special-cases $00 (no-op) and $FF (stop);
// the 17 songs are $01..$11, and `value & kMusicIdIndexMask` is the 1-based MusicPointers index.
enum class MusicId : std::uint8_t {{
{enum_rows}
}};

inline constexpr std::uint16_t kMusicPointersAddr    = 0x{MUSIC_POINTERS_ADDR:04X};
inline constexpr std::uint8_t  kMusicSongCount       = {MUSIC_SONG_COUNT};
inline constexpr std::uint8_t  kMusicIdIndexMask     = 0x{MUSIC_ID_INDEX_MASK:02X};
inline constexpr std::uint16_t kMusicSectionBase     = 0x{section_base:04X};
inline constexpr std::uint16_t kMusicSectionEnd      = 0x{MUSIC_SECTION_END:04X};  // exclusive
inline constexpr std::uint16_t kStereoDataAddr       = 0x{STEREO_DATA_ADDR:04X};
inline constexpr std::uint16_t kNoteLengthRegionBase = 0x{NOTE_LENGTH_REGION_BASE:04X};
inline constexpr std::uint16_t kNoteLengthRegionEnd  = 0x{NOTE_LENGTH_REGION_END:04X};  // exclusive

}}  // namespace kirpich
"""


# --- Emit: tests/fixtures/music_expected.h ------------------------------------------------------

def _song_rows(songs: list[Song]) -> str:
    rows = []
    for i, s in enumerate(songs):
        chans = ", ".join(f"0x{a:04X}" for a in s.channel_addrs)
        rows.append(
            f"    {{ .id = 0x{i + 1:02X}, .headerAddr = 0x{s.addr:04X}, .byte0 = 0x{s.byte0:02X}, "
            f".lengthTableAddr = 0x{s.length_table_addr:04X}, .channelAddrs = {{{{ {chans} }}}} }},"
        )
    return "\n".join(rows)


def _channel_rows(channels: list[Channel]) -> tuple[str, str, int]:
    """Channel rows over a flat section-address pool; returns (rows, pool, pool_len)."""
    pool: list[int] = []
    rows = []
    term_enum = {"none": "None", "stop": "Stop", "repeat": "Repeat"}
    for c in sorted(channels, key=lambda c: c.addr):
        offset = len(pool)
        pool += c.sections
        rows.append(
            f"    {{ .addr = 0x{c.addr:04X}, .terminator = MusicChannelTerm::{term_enum[c.terminator]}, "
            f".repeatTargetAddr = 0x{c.repeat_target:04X}, "
            f".sectionCount = {len(c.sections)}, .poolOffset = {offset} }},"
        )
    pool_cells = ", ".join(f"0x{a:04X}" for a in pool)
    pool_text = "\n".join(
        "    " + ", ".join(f"0x{a:04X}" for a in pool[i:i + 8]) + ","
        for i in range(0, len(pool), 8)
    ) if pool else ""
    return "\n".join(rows), pool_text, len(pool)


def _section_rows(sections: list[Section]) -> str:
    rows = []
    for s in sorted(sections, key=lambda s: s.addr):
        rows.append(
            f'    {{ .addr = 0x{s.addr:04X}, .length = {s.length:3d}, .sha1 = "{s.sha1}" }},'
        )
    return "\n".join(rows)


def _stereo_rows(stereo: list[list[int]]) -> str:
    out = []
    for i, row in enumerate(stereo):
        cells = ", ".join(f"0x{b:02X}" for b in row)
        out.append("    {{" + cells + "}},  // song " + str(i + 1))
    return "\n".join(out)


def _note_length_rows(region: list[int]) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{b:02X}" for b in region[i:i + 12]) + ","
        for i in range(0, len(region), 12)
    )


def emit_fixture(
    songs: list[Song],
    channels: list[Channel],
    sections: list[Section],
    stereo: list[list[int]],
    region: list[int],
    source_commit: str,
) -> str:
    channel_rows, pool_text, pool_len = _channel_rows(channels)
    return f"""#pragma once
{common.banner("parse_music.py", source_commit)}\
// Independent fixture for the full-corpus music sweep. Holds the song/channel/section address graph,
// the per-section {{addr, length, SHA-1}} pins (NOT the note bytes - those are hashed from the ROM at
// test time and never committed), the StereoData rows, and the note-length region. Deliberately
// carries its own record types so a defect in src/data/music.h cannot mask the sweep.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

struct MusicSongExpected {{
    std::uint8_t                 id;               // wire MusicId ($01..$11)
    std::uint16_t                headerAddr;       // Song_ label address
    std::uint8_t                 byte0;            // header byte 0 (-> $DF80, unused)
    std::uint16_t                lengthTableAddr;  // note-length table pointer
    std::array<std::uint16_t, 4> channelAddrs;     // 0 = unused channel
    bool operator==(const MusicSongExpected&) const = default;
}};

enum class MusicChannelTerm : std::uint8_t {{ None, Stop, Repeat }};

struct MusicChannelExpected {{
    std::uint16_t    addr;
    MusicChannelTerm terminator;
    std::uint16_t    repeatTargetAddr;  // valid iff terminator == Repeat
    std::uint16_t    sectionCount;
    std::uint16_t    poolOffset;        // into kMusicChannelSectionPool
    bool operator==(const MusicChannelExpected&) const = default;
}};

struct MusicSectionExpected {{
    std::uint16_t    addr;
    std::uint16_t    length;
    std::string_view sha1;  // SHA-1 of the section's ROM bytes, lower-case hex
    bool operator==(const MusicSectionExpected&) const = default;
}};

inline constexpr std::array<MusicSongExpected, {len(songs)}> kExpectedMusicSongs{{{{
{_song_rows(songs)}
}}}};

inline constexpr std::array<std::uint16_t, {pool_len}> kMusicChannelSectionPool{{{{
{pool_text}
}}}};

inline constexpr std::array<MusicChannelExpected, {len(channels)}> kExpectedMusicChannels{{{{
{channel_rows}
}}}};

inline constexpr std::array<MusicSectionExpected, {len(sections)}> kExpectedMusicSections{{{{
{_section_rows(sections)}
}}}};

inline constexpr std::array<std::array<std::uint8_t, {STEREO_ROW_BYTES}>, {len(stereo)}>
    kExpectedStereoData{{{{
{_stereo_rows(stereo)}
}}}};

inline constexpr std::array<std::uint8_t, {len(region)}> kExpectedNoteLengthRegion{{{{
{_note_length_rows(region)}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's music data surface + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--header-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    audio_path = source_root / "audio.asm"
    music_path = source_root / "music.asm"
    for p in (audio_path, music_path):
        if not p.is_file():
            print(f"parse_music: source file not found: {p}", file=sys.stderr)
            return 2

    audio_lines = audio_path.read_bytes().decode("utf-8").splitlines()
    music_text = music_path.read_bytes().decode("utf-8")

    pointers = parse_music_pointers(audio_lines, audio_path)
    assert_index_mask(audio_lines, audio_path)
    stereo = parse_stereo_data(audio_lines, audio_path)
    region = parse_note_length_region(audio_lines, audio_path)

    elements, by_addr = parse_music_asm(music_text, music_path)
    assert_tiling(elements, music_path)
    songs, channels, sections = build_graph(elements, by_addr, pointers, music_path)

    commit = common.source_commit_of(source_root)
    outputs = {
        args.header_out: emit_header(pointers, commit),
        args.fixture_out: emit_fixture(songs, channels, sections, stereo, region, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_music: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_music: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
