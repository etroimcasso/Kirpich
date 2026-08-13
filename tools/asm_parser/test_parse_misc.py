#!/usr/bin/env python3
"""Unit tests for parse_misc.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the block collector (incl. Data_1741's trailing-padding absorption),
                              the OAM decoder, the coordinate/raw-string/routine helpers, and the
                              sentinel/quirk derivations on crafted input.
  2. Synthetic edge cases   - malformed input that MUST raise (every structural assert has a raise
                              path), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris/tetris.asm + charmap.asm, asserting the 25 objects,
                              42 pairs, 5 strings, 3 scalars, and boundary pins. Skips when absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_misc
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_charmap  # noqa: E402
import parse_misc as mc  # noqa: E402

P = Path("tetris.asm")

# A charmap subset sufficient to encode "pause" (greedy segmenter tests don't need the full table).
PAUSE_CHARMAP = {"p": 0x19, "a": 0x0A, "u": 0x1E, "s": 0x1C, "e": 0x0E}


def _lines(*rows: str) -> list[str]:
    return list(rows)


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class CollectHexBlock(unittest.TestCase):
    def test_basic_run(self):
        raw = mc._collect_hex_block(
            _lines("Foo::", "    db $01, $02", "    db $03", "Bar::", "    ret"), "Foo", P)
        self.assertEqual(raw, [0x01, 0x02, 0x03])

    def test_trailing_padding_absorbed_across_blank(self):
        # Data_1741's shape: pairs, a blank line, then a stray `db $00`, then the next routine.
        raw = mc._collect_hex_block(
            _lines("Data_1741::", "    db $40, $70", "", "    db $00  ; XXX", "Next::"), "Data_1741", P)
        self.assertEqual(raw, [0x40, 0x70, 0x00])

    def test_missing_label_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._collect_hex_block(_lines("Other::", "    db $01"), "Foo", P)

    def test_no_db_bytes_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._collect_hex_block(_lines("Foo::", "Bar::"), "Foo", P)


class DecodeOam(unittest.TestCase):
    def test_no_flip(self):
        obj = mc._decode_oam_object([0x40, 0x28, 0xAE, 0x00], "ctx", P)
        self.assertEqual((obj.y, obj.x, obj.tile, obj.xflip), (0x40, 0x28, 0xAE, False))

    def test_xflip(self):
        obj = mc._decode_oam_object([0x40, 0x30, 0xAE, 0x20], "ctx", P)
        self.assertTrue(obj.xflip)

    def test_stray_attr_bit_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._decode_oam_object([0, 0, 0, 0x40], "ctx", P)  # bit outside $20


class ParseOamTable(unittest.TestCase):
    def test_valid_with_letters(self):
        lines = _lines("PushStartObjects::",
                       "    db $42, $30, $0D, $00",
                       "    db $42, $38, $B2, $00",
                       "End::")
        table = mc._parse_oam_table(lines, "PushStartObjects", "pushStartObjects", 2, "PU", {}, P)
        self.assertEqual(len(table.objects), 2)
        self.assertEqual(table.objects[0].tile, 0x0D)

    def test_wrong_count_raises(self):
        lines = _lines("T::", "    db $40, $28, $AE, $00", "End::")
        with self.assertRaises(mc.ParseError):
            mc._parse_oam_table(lines, "T", "t", 2, None, {}, P)

    def test_letter_length_mismatch_raises(self):
        lines = _lines("T::", "    db $42, $30, $0D, $00", "End::")
        with self.assertRaises(mc.ParseError):
            mc._parse_oam_table(lines, "T", "t", 1, "PU", {}, P)


class ParseCoordTable(unittest.TestCase):
    def test_valid(self):
        lines = _lines("T::", "    db $40, $60, $40, $70", "End::")
        table = mc._parse_coord_table(lines, "T", "t", 2, False, "x 0-1", P)
        self.assertEqual([(c.y, c.x) for c in table.pairs], [(0x40, 0x60), (0x40, 0x70)])

    def test_trailing_zero_stripped(self):
        lines = _lines("Data_1741::", "    db $40, $70, $40, $80, $40, $90", "    db $00", "End::")
        table = mc._parse_coord_table(lines, "Data_1741", "t", 3, True, "start 0-2", P)
        self.assertEqual(len(table.pairs), 3)
        self.assertEqual(len(table.raw_bytes), 6)  # padding byte excluded from the surface

    def test_wrong_count_raises(self):
        lines = _lines("T::", "    db $40, $60", "End::")
        with self.assertRaises(mc.ParseError):
            mc._parse_coord_table(lines, "T", "t", 2, False, "n", P)

    def test_bad_padding_byte_raises(self):
        lines = _lines("T::", "    db $40, $70", "    db $99", "End::")
        with self.assertRaises(mc.ParseError):
            mc._parse_coord_table(lines, "T", "t", 1, True, "n", P)


class RecordLetter(unittest.TestCase):
    def test_consistent(self):
        table: dict[int, str] = {}
        mc._record_letter(table, 0xB5, "A", "c1", P)
        mc._record_letter(table, 0xB5, "A", "c2", P)  # same tile, same letter - OK
        self.assertEqual(table[0xB5], "A")

    def test_conflict_raises(self):
        table = {0xB5: "A"}
        with self.assertRaises(mc.ParseError):
            mc._record_letter(table, 0xB5, "Z", "c", P)


class GreedySegments(unittest.TestCase):
    def test_pause(self):
        segs = mc._greedy_segments("pause", PAUSE_CHARMAP, P)
        self.assertEqual([tile for _s, tile in segs], [0x19, 0x0A, 0x1E, 0x1C, 0x0E])

    def test_unmapped_char_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._greedy_segments("pz", PAUSE_CHARMAP, P)  # 'z' absent


class RoutineBody(unittest.TestCase):
    def test_extracts_until_next_label(self):
        body = mc._routine_body(
            _lines("Foo::", "    ld a, $01  ; c", ".local", "    ret", "Bar::", "    nop"), "Foo", P)
        self.assertEqual(body, ["ld a, $01", ".local", "ret"])

    def test_missing_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._routine_body(_lines("Bar::", "    ret"), "Foo", P)


def _sentinel_lines(set_val: str = "$FF", cmp1: str = "$FF", cmp2: str = "$FF",
                    store: str = "ldh [hDemoRecording], a") -> list[str]:
    return _lines(
        "StartRecordingDemo::", f"    ld a, {set_val}", f"    {store}", "    ret",
        "DemoSimulateJoypad::", "    ldh a, [hDemoRecording]", f"    cp a, {cmp1}  ; TODO", "    ret z",
        "RecordDemo::", "    ldh a, [hDemoRecording]", f"    cp a, {cmp2}", "    ret nz",
        "End::", "    ret")


class ParseSentinel(unittest.TestCase):
    def test_three_site_agreement(self):
        self.assertEqual(mc._parse_sentinel(_sentinel_lines(), P), 0xFF)

    def test_disagreement_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._parse_sentinel(_sentinel_lines(cmp2="$FE"), P)

    def test_missing_store_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._parse_sentinel(_sentinel_lines(store="ldh [hSomethingElse], a"), P)


def _quirk_lines(addr: str = "$C842", count: str = "16", stride: str = "$0020") -> list[str]:
    return _lines(
        "CheckForCompletedRows::",
        "    ld de, wLineClearsList",
        f"    ld hl, {addr}  ; Start at the third row",
        f"    ld b, {count}",
        ".rowLoop",
        "    ld c, 10",
        f"    ld de, {stride}",
        "    add hl, de",
        "    ret",
        "End::", "    ret")


class ParseQuirk(unittest.TestCase):
    def test_derives_first_row_and_count(self):
        first, count = mc._parse_quirk(_quirk_lines(), P)
        self.assertEqual((first, count), (2, 16))  # ($C842 - $C802) / $20 == 2

    def test_stride_mismatch_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._parse_quirk(_quirk_lines(stride="$0010"), P)

    def test_invariant_violation_raises(self):
        # first_row 2 + count 15 != 18 field rows.
        with self.assertRaises(mc.ParseError):
            mc._parse_quirk(_quirk_lines(count="15"), P)

    def test_misaligned_address_raises(self):
        with self.assertRaises(mc.ParseError):
            mc._parse_quirk(_quirk_lines(addr="$C843"), P)


class ParseRawString(unittest.TestCase):
    def test_valid(self):
        lines = _lines("DeuceText::", "    db $B0, $B1, $B2, $B3, $B1, $3E", "End::")
        s = mc._parse_raw_string(lines, "DeuceText", "deuceText", "DEUCE!", {}, P)
        self.assertEqual(s.raw_bytes[0], 0xB0)
        self.assertFalse(s.typed)

    def test_length_mismatch_raises(self):
        lines = _lines("T::", "    db $B0, $B1", "End::")
        with self.assertRaises(mc.ParseError):
            mc._parse_raw_string(lines, "T", "t", "DEUCE!", {}, P)


# --- Layer 2: emit shape ------------------------------------------------------------------------

def _tiny_data() -> mc.MiscData:
    return mc.MiscData(
        oam_tables=[mc.OamTable("MarioLuigiFaceObjects", "marioLuigiFaceObjects",
                                [mc.OamObject(0x40, 0x28, 0xAE, False)], [0x40, 0x28, 0xAE, 0x00])],
        coord_tables=[mc.CoordTable("Data_1615", "typeALevelCursorCoordinates", "level 0-0",
                                    [mc.Coordinate(0x40, 0x30)], [0x40, 0x30])],
        strings=[mc.TextString("DeuceText", "deuceText", "DEUCE!", [0xB0], False),
                 mc.TextString("PauseText", "pauseText", "pause", [0x19, 0x0A, 0x1E, 0x1C, 0x0E], True)],
        pause_names=["LETTER_P", "LETTER_A", "LETTER_U", "LETTER_S", "LETTER_E"],
        scalars=mc.Scalars(0xFF, 2, 16))


class Emit(unittest.TestCase):
    def test_inc_shape(self):
        inc = mc.emit_inc(_tiny_data(), "deadbee")
        self.assertIn("std::array<OamObject, 1> kMarioLuigiFaceObjects{{", inc)
        self.assertIn("std::array<SpriteCoordinate, 1> kTypeALevelCursorCoordinates{{", inc)
        self.assertIn("std::array<CharTile, 5> kPauseText", inc)
        self.assertIn("CharTile::LETTER_P", inc)
        self.assertIn("kDemoRecordingEnabledMagic = 0xFF", inc)
        self.assertIn("kCompletedRowCheckFirstRow = 2", inc)
        self.assertIn("kCompletedRowCheckRowCount = 16", inc)
        self.assertTrue(inc.isascii())

    def test_fixture_shape(self):
        fx = mc.emit_fixture(_tiny_data(), "deadbee")
        self.assertIn("kExpectedMiscOamBytes", fx)
        self.assertIn("kExpectedMiscCoordBytes", fx)
        self.assertIn("kExpectedPauseTextBytes", fx)
        self.assertIn("kExpectedDemoRecordingMagic = 0xFF", fx)
        self.assertTrue(fx.isascii())


# --- Layer 3: end-to-end against the real disassembly -------------------------------------------

def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "tetris.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        tetris = self.root / "tetris.asm"
        charmap_path = self.root / "charmap.asm"
        text = tetris.read_bytes().decode("utf-8")
        charmap = dict(parse_charmap.parse_charmap(charmap_path.read_bytes().decode("utf-8"), charmap_path))
        self.data = mc.parse_misc(text, tetris, charmap, charmap_path)
        self.oam = {t.label: t for t in self.data.oam_tables}
        self.coord = {t.label: t for t in self.data.coord_tables}
        self.strings = {s.label: s for s in self.data.strings}

    def test_corpus_totals(self):
        self.assertEqual(sum(len(t.objects) for t in self.data.oam_tables), 25)
        self.assertEqual(sum(len(t.pairs) for t in self.data.coord_tables), 42)
        self.assertEqual(len(self.data.strings), 5)
        self.assertEqual(len(self.data.coord_tables), 6)

    def test_oam_pins(self):
        ml = self.oam["MarioLuigiFaceObjects"].objects
        self.assertEqual((ml[1].y, ml[1].x, ml[1].tile, ml[1].xflip), (0x40, 0x30, 0xAE, True))
        push = self.oam["PushStartObjects"].objects
        self.assertEqual((push[0].y, push[0].x, push[0].tile), (0x42, 0x30, 0x0D))
        self.assertTrue(all(o.y == 0x42 and not o.xflip for o in push))

    def test_coord_pins(self):
        lvl = self.coord["Data_1615"].pairs
        self.assertEqual((lvl[0].y, lvl[0].x), (0x40, 0x30))
        self.assertEqual(len(lvl), 10)
        music = self.coord["MusicTypeSpriteCoordinates"].pairs
        self.assertEqual((music[3].y, music[3].x), (0x80, 0x77))
        height = self.coord["Data_1741"].pairs
        self.assertEqual(len(height), 6)
        self.assertEqual((height[-1].y, height[-1].x), (0x50, 0x90))

    def test_text_and_pause(self):
        self.assertEqual(self.strings["DeuceText"].raw_bytes[0], 0xB0)
        self.assertEqual(self.strings["LuigiWinsText"].raw_bytes[0], 0xBD)
        pause = self.strings["PauseText"]
        self.assertTrue(pause.typed)
        self.assertEqual(pause.raw_bytes, [0x19, 0x0A, 0x1E, 0x1C, 0x0E])

    def test_scalars(self):
        self.assertEqual(self.data.scalars.demo_recording_magic, 0xFF)
        self.assertEqual(self.data.scalars.completed_row_first, 2)
        self.assertEqual(self.data.scalars.completed_row_count, 16)


if __name__ == "__main__":
    unittest.main()
