#!/usr/bin/env python3
"""Unit tests for parse_music.py.

Three layers per the parser test discipline:
  1. Helper units          - the byte/word readers, the macro-to-bytes reconstructor, and the
                             channel-terminator classifier on crafted input.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a raise
                             path): SFX-table miscount, MusicPointers/Song mismatch, tiling gap,
                             bad StereoData mode, out-of-region length pointer, name/address
                             mismatch, illegal note - plus the shape of both emitted artifacts.
  3. End-to-end            - the real ../tetris/{audio,music}.asm: song/channel/section counts,
                             boundary addresses, StereoData/note-length pins, and SHA-1 stability.
                             Skips cleanly when the disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_music
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_music as pm  # noqa: E402

P = Path("audio.asm")
PM = Path("music.asm")

REAL_SONG_ADDRS = [
    0x6F3F, 0x6F4A, 0x6F55, 0x6F60, 0x6F6B, 0x6F76, 0x6F81, 0x6F8C, 0x6F97,
    0x6FA2, 0x6FAD, 0x6FB8, 0x6FC3, 0x6FCE, 0x6FD9, 0x6FE4, 0x6FEF,
]

REAL_STEREO_ROW0 = [1, 36, 0xEF, 0x56]
REAL_STEREO_ROW16 = [1, 32, 0xEF, 0xF7]


# --- Synthetic renderers ------------------------------------------------------------------------

def _sfx_tables(sizes=(8, 8, 4, 4)) -> list[str]:
    names = ["SquareSFXStartPointers", "SquareSFXContinuePointers",
             "NoiseSFXStartPointers", "NoiseSFXContinuePointers"]
    out: list[str] = []
    for name, n in zip(names, sizes):
        out.append(f"{name}::")
        out += ["    dw $0000" for _ in range(n)]
    return out


def _audio_pointers(addrs=None, sizes=(8, 8, 4, 4)) -> list[str]:
    addrs = REAL_SONG_ADDRS if addrs is None else addrs
    lines = ['SECTION "Audio", ROM0[$6480]']
    lines += _sfx_tables(sizes)
    lines.append("MusicPointers::")
    lines += [f"    dw ${a:04X}" for a in addrs]
    lines += ["DoNothing::", "    ret"]
    return lines


def _audio_pointers_text(**kw) -> str:
    return "\n".join(_audio_pointers(**kw)) + "\n"


def _stereo_text(rows=None) -> str:
    rows = [REAL_STEREO_ROW0] * 17 if rows is None else rows
    out = ["StereoData::"]
    for r in rows:
        out.append("    db " + ", ".join(str(b) for b in r))
    out += ["NextRoutine::", "    ret"]
    return "\n".join(out) + "\n"


def _start_music_text(mask="$1F", *, present=True) -> str:
    out = ["StartMusic::", "    ld hl, wNewMusicID"]
    if present:
        out.append(f"    and a, {mask}")
    out += ["    ret", "InitStereo::", "    ret"]
    return "\n".join(out) + "\n"


def _note_region_text(nbytes=70) -> str:
    return "\n".join([
        "Data_6EF9::",
        "    db " + ", ".join("1" for _ in range(nbytes)),
        'SECTION "Footer", ROM0[$7FF0]',
    ]) + "\n"


def _music_text(body: list[str]) -> str:
    return "\n".join(['SECTION "Music", ROM0[$6F3F]'] + body) + "\n"


# A minimal, fully-tiling Music section: one song, one channel, one section, ending at 0x6F50.
TINY_MUSIC_END = 0x6F50
TINY_MUSIC = [
    "Song_6F3F::",
    "    db $00",
    "    dw $6EF9",
    "    dw Channel1_6F4A",
    "    dw $0000",
    "    dw $0000",
    "    dw $0000",
    "Channel1_6F4A::",
    "    dw Section_6F4E, $0000",
    "Section_6F4E::",
    "    Rest",
    "    EndSection",
]


def _tiny_pointers() -> list[int]:
    return [0x6F3F]


class _EndPatch:
    """Context to temporarily point the tiling asserts at a synthetic end address."""

    def __init__(self, test: unittest.TestCase, end: int, songs: int = 1):
        self._orig_end = pm.MUSIC_SECTION_END
        self._orig_count = pm.MUSIC_SONG_COUNT
        pm.MUSIC_SECTION_END = end
        pm.MUSIC_SONG_COUNT = songs
        test.addCleanup(self._restore)

    def _restore(self):
        pm.MUSIC_SECTION_END = self._orig_end
        pm.MUSIC_SONG_COUNT = self._orig_count


# --- Layer 1: pure helpers ----------------------------------------------------------------------

class ParseByte(unittest.TestCase):
    def test_decimal(self):
        self.assertEqual(pm._parse_byte("36", P, 1), 36)

    def test_hex(self):
        self.assertEqual(pm._parse_byte("$EF", P, 1), 0xEF)

    def test_over_a_byte_raises(self):
        with self.assertRaises(SystemExit):
            pm._parse_byte("$100", P, 1)

    def test_garbage_raises(self):
        with self.assertRaises(SystemExit):
            pm._parse_byte("nope", P, 1)


class ResolveWord(unittest.TestCase):
    def test_hex_literal(self):
        self.assertEqual(pm._resolve_word("$7016", P, 1), 0x7016)

    def test_section_label(self):
        self.assertEqual(pm._resolve_word("Section_7016", P, 1), 0x7016)

    def test_channel_label(self):
        self.assertEqual(pm._resolve_word("Channel1_7CF9", P, 1), 0x7CF9)

    def test_song_label(self):
        self.assertEqual(pm._resolve_word("Song_6F3F", P, 1), 0x6F3F)

    def test_unresolvable_raises(self):
        with self.assertRaises(SystemExit):
            pm._resolve_word("SomethingElse", P, 1)


class LineBytes(unittest.TestCase):
    def test_db_hex(self):
        self.assertEqual(pm._line_bytes("Section", "db $9D, $74, $00, $41", P, 1),
                         [0x9D, 0x74, 0x00, 0x41])

    def test_db_set_length(self):
        self.assertEqual(pm._line_bytes("Section", "db $A2", P, 1), [0xA2])

    def test_dw_words_little_endian(self):
        self.assertEqual(pm._line_bytes("Channel1", "dw Section_7016, $FFFF", P, 1),
                         [0x16, 0x70, 0xFF, 0xFF])

    def test_notes_pitch_formula(self):
        # G_ (8) octave 7 -> 8 + (7-2)*12 = 68; D# (4) octave 8 -> 4 + 72 = 76.
        self.assertEqual(pm._line_bytes("Section", "Notes G_, 7, D#, 8", P, 1), [68, 76])

    def test_notes_low(self):
        self.assertEqual(pm._line_bytes("Section", "Notes C_, 2", P, 1), [1])

    def test_notes_odd_args_raise(self):
        with self.assertRaises(SystemExit):
            pm._line_bytes("Section", "Notes G_", P, 1)

    def test_notes_unknown_name_raises(self):
        with self.assertRaises(SystemExit):
            pm._line_bytes("Section", "Notes X_, 2", P, 1)

    def test_noise_mapping(self):
        self.assertEqual(pm._line_bytes("Section", "Noise 0, 1, 2, 3", P, 1), [1, 6, 11, 16])

    def test_noise_out_of_range_raises(self):
        with self.assertRaises(SystemExit):
            pm._line_bytes("Section", "Noise 4", P, 1)

    def test_rest_and_end(self):
        self.assertEqual(pm._line_bytes("Section", "Rest", P, 1), [0x01])
        self.assertEqual(pm._line_bytes("Section", "EndSection", P, 1), [0x00])

    def test_unknown_directive_raises(self):
        with self.assertRaises(SystemExit):
            pm._line_bytes("Section", "Vibrato 3", P, 1)


class ClassifyChannel(unittest.TestCase):
    def setUp(self):
        self.by_addr = {
            0x7016: pm.Element("Section_7016", "Section", 0x7016),
            0x7034: pm.Element("Section_7034", "Section", 0x7034),
        }
        self.el = pm.Element("Channel1_7000", "Channel1", 0x7000)

    def test_stop(self):
        term, target, secs = pm._classify_channel([0x7016, 0x0000], self.el, self.by_addr, PM)
        self.assertEqual((term, target, secs), ("stop", 0, [0x7016]))

    def test_repeat(self):
        term, target, secs = pm._classify_channel(
            [0x7016, 0x7034, 0xFFFF, 0x6FFA], self.el, self.by_addr, PM)
        self.assertEqual((term, target, secs), ("repeat", 0x6FFA, [0x7016, 0x7034]))

    def test_none_extent_by_adjacency(self):
        term, target, secs = pm._classify_channel([0x7016, 0x7034], self.el, self.by_addr, PM)
        self.assertEqual((term, target, secs), ("none", 0, [0x7016, 0x7034]))

    def test_reference_to_non_section_raises(self):
        with self.assertRaises(SystemExit):
            pm._classify_channel([0x1234], self.el, self.by_addr, PM)


# --- Layer 2: synthetic corpora that MUST raise (and the valid baselines) -----------------------

class MusicPointers(unittest.TestCase):
    def test_valid(self):
        got = pm.parse_music_pointers(_audio_pointers_text().splitlines(), P)
        self.assertEqual(got, REAL_SONG_ADDRS)

    def test_wrong_sfx_table_size_raises(self):
        with self.assertRaises(SystemExit):
            pm.parse_music_pointers(_audio_pointers_text(sizes=(7, 8, 4, 4)).splitlines(), P)

    def test_wrong_pointer_count_raises(self):
        with self.assertRaises(SystemExit):
            pm.parse_music_pointers(_audio_pointers_text(addrs=REAL_SONG_ADDRS[:16]).splitlines(), P)


class IndexMask(unittest.TestCase):
    def test_valid(self):
        pm.assert_index_mask(_start_music_text().splitlines(), P)  # no raise

    def test_wrong_mask_raises(self):
        with self.assertRaises(SystemExit):
            pm.assert_index_mask(_start_music_text(mask="$0F").splitlines(), P)

    def test_missing_mask_raises(self):
        with self.assertRaises(SystemExit):
            pm.assert_index_mask(_start_music_text(present=False).splitlines(), P)


class StereoData(unittest.TestCase):
    def test_valid(self):
        rows = pm.parse_stereo_data(_stereo_text().splitlines(), P)
        self.assertEqual(len(rows), 17)

    def test_bad_mode_raises(self):
        rows = [[2, 0, 0, 0]] + [REAL_STEREO_ROW0] * 16
        with self.assertRaises(SystemExit):
            pm.parse_stereo_data(_stereo_text(rows).splitlines(), P)

    def test_wrong_row_byte_count_raises(self):
        rows = [[1, 0, 0]] + [REAL_STEREO_ROW0] * 16
        with self.assertRaises(SystemExit):
            pm.parse_stereo_data(_stereo_text(rows).splitlines(), P)

    def test_wrong_row_count_raises(self):
        with self.assertRaises(SystemExit):
            pm.parse_stereo_data(_stereo_text([REAL_STEREO_ROW0] * 16).splitlines(), P)


class NoteLengthRegion(unittest.TestCase):
    def test_valid(self):
        got = pm.parse_note_length_region(_note_region_text().splitlines(), P)
        self.assertEqual(len(got), 70)

    def test_wrong_length_raises(self):
        with self.assertRaises(SystemExit):
            pm.parse_note_length_region(_note_region_text(nbytes=69).splitlines(), P)


class MusicGraph(unittest.TestCase):
    def test_tiny_parses_and_tiles(self):
        _EndPatch(self, TINY_MUSIC_END)
        elements, by_addr = pm.parse_music_asm(_music_text(TINY_MUSIC), PM)
        pm.assert_tiling(elements, PM)
        songs, channels, sections = pm.build_graph(elements, by_addr, _tiny_pointers(), PM)
        self.assertEqual((len(songs), len(channels), len(sections)), (1, 1, 1))
        self.assertEqual(channels[0].terminator, "stop")
        self.assertEqual(sections[0].addr, 0x6F4E)

    def test_label_position_mismatch_raises(self):
        # Rename the section's label so its name-encoded address no longer matches where adjacency
        # places it (the channel still points at the old address). Caught by the tiling assert.
        _EndPatch(self, TINY_MUSIC_END)
        body = [ln.replace("Section_6F4E::", "Section_6F4F::") for ln in TINY_MUSIC]
        elements, _ = pm.parse_music_asm(_music_text(body), PM)
        with self.assertRaises(SystemExit):
            pm.assert_tiling(elements, PM)

    def test_duplicate_address_raises(self):
        _EndPatch(self, TINY_MUSIC_END)
        body = list(TINY_MUSIC) + ["Section_6F4E::", "    EndSection"]  # second label at 0x6F4E
        with self.assertRaises(SystemExit):
            pm.parse_music_asm(_music_text(body), PM)

    def test_tiling_gap_raises(self):
        _EndPatch(self, TINY_MUSIC_END)
        # Drop the section's Rest byte: Channel now points past a shorter section -> gap.
        body = [ln for ln in TINY_MUSIC if ln.strip() != "Rest"]
        elements, _ = pm.parse_music_asm(_music_text(body), PM)
        with self.assertRaises(SystemExit):
            pm.assert_tiling(elements, PM)

    def test_pointer_song_mismatch_raises(self):
        _EndPatch(self, TINY_MUSIC_END)
        elements, by_addr = pm.parse_music_asm(_music_text(TINY_MUSIC), PM)
        with self.assertRaises(SystemExit):
            pm.build_graph(elements, by_addr, [0x6F40], PM)  # wrong pointer

    def test_length_pointer_out_of_region_raises(self):
        _EndPatch(self, TINY_MUSIC_END)
        body = [ln.replace("dw $6EF9", "dw $6000") for ln in TINY_MUSIC]  # outside note region
        elements, by_addr = pm.parse_music_asm(_music_text(body), PM)
        with self.assertRaises(SystemExit):
            pm.build_graph(elements, by_addr, _tiny_pointers(), PM)

    def test_illegal_section_byte_raises(self):
        _EndPatch(self, TINY_MUSIC_END + 1)
        # A lone $99 is neither a note (<$92), $01, $00, $9D, nor $Ax.
        body = list(TINY_MUSIC)
        body[body.index("    Rest")] = "    db $99"
        elements, by_addr = pm.parse_music_asm(_music_text(body), PM)
        songs = [e for e in elements if e.kind == "Section"]
        with self.assertRaises(SystemExit):
            pm._assert_section_grammar(songs[0], PM)


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def test_header_has_enum_and_constants(self):
        header = pm.emit_header(REAL_SONG_ADDRS, "abc1234")
        self.assertIn("enum class MusicId : std::uint8_t", header)
        self.assertIn("TOP_SCORE           = 0x01", header)
        self.assertIn("STOP                = 0xFF", header)
        self.assertIn("kMusicPointersAddr    = 0x64B0", header)
        self.assertIn("kStereoDataAddr       = 0x6ABE", header)
        self.assertIn("kMusicSectionBase     = 0x6F3F", header)
        # 19 enumerators + 7 hex constants (kMusicSongCount = 17 is decimal, no 0x).
        self.assertEqual(header.count("= 0x"), 19 + 7)
        self.assertTrue(header.isascii())

    def test_fixture_carries_records_not_note_bytes(self):
        _EndPatch(self, TINY_MUSIC_END)
        elements, by_addr = pm.parse_music_asm(_music_text(TINY_MUSIC), PM)
        songs, channels, sections = pm.build_graph(elements, by_addr, _tiny_pointers(), PM)
        fixture = pm.emit_fixture(songs, channels, sections, [REAL_STEREO_ROW0] * 17,
                                  list(range(70)), "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        self.assertIn("struct MusicSectionExpected", fixture)
        self.assertIn(".sha1 =", fixture)
        # The note/command stream never appears - only its hash.
        self.assertNotIn("Notes", fixture)
        self.assertNotIn('#include "data/music.h"', fixture)
        self.assertTrue(fixture.isascii())


# --- Layer 3: end-to-end against the real disassembly -------------------------------------------

def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "audio.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        self.audio = (self.root / "audio.asm")
        self.music = (self.root / "music.asm")
        self.audio_lines = self.audio.read_bytes().decode("utf-8").splitlines()
        self.music_text = self.music.read_bytes().decode("utf-8")

    def test_music_pointers(self):
        self.assertEqual(pm.parse_music_pointers(self.audio_lines, self.audio), REAL_SONG_ADDRS)

    def test_index_mask(self):
        pm.assert_index_mask(self.audio_lines, self.audio)  # no raise

    def test_stereo_data(self):
        rows = pm.parse_stereo_data(self.audio_lines, self.audio)
        self.assertEqual(len(rows), 17)
        self.assertEqual(rows[0], REAL_STEREO_ROW0)
        self.assertEqual(rows[16], REAL_STEREO_ROW16)
        for row in rows:
            self.assertIn(row[0], (1, 3))

    def test_note_length_region(self):
        region = pm.parse_note_length_region(self.audio_lines, self.audio)
        self.assertEqual(len(region), 70)
        self.assertEqual(region[:6], [2, 4, 8, 16, 32, 64])
        self.assertEqual(region[-4:], [0xA0, 0x1E, 0x3C, 0x78])  # 160, 30, dw $783C

    def test_graph_counts_and_tiling(self):
        elements, by_addr = pm.parse_music_asm(self.music_text, self.music)
        pm.assert_tiling(elements, self.music)
        pointers = pm.parse_music_pointers(self.audio_lines, self.audio)
        songs, channels, sections = pm.build_graph(elements, by_addr, pointers, self.music)
        self.assertEqual(len(songs), 17)
        self.assertEqual(len(channels), 57)
        self.assertEqual(len(sections), 119)

    def test_first_song(self):
        elements, by_addr = pm.parse_music_asm(self.music_text, self.music)
        pointers = pm.parse_music_pointers(self.audio_lines, self.audio)
        songs, _, _ = pm.build_graph(elements, by_addr, pointers, self.music)
        first = songs[0]
        self.assertEqual(first.addr, 0x6F3F)
        self.assertEqual(first.length_table_addr, 0x6F0E)
        self.assertEqual(first.channel_addrs, [0x7CF9, 0x7CFF, 0x7D11, 0x7D21])

    def test_section_hash_is_stable(self):
        elements, by_addr = pm.parse_music_asm(self.music_text, self.music)
        pointers = pm.parse_music_pointers(self.audio_lines, self.audio)
        _, _, sections = pm.build_graph(elements, by_addr, pointers, self.music)
        again = pm.build_graph(*pm.parse_music_asm(self.music_text, self.music), pointers, self.music)[2]
        self.assertEqual([s.sha1 for s in sections], [s.sha1 for s in again])
        first = next(s for s in sections if s.addr == 0x7016)
        self.assertEqual(first.sha1, "2b2341a6b9ea746db3ac2b0c11638f4ca2139244")


if __name__ == "__main__":
    unittest.main()
