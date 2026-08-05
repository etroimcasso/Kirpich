#!/usr/bin/env python3
"""Unit tests for parse_charmap.py.

Three layers per the parser test discipline:
  1. Helper units          - the ASCII byte-string emitter, the readable-comment renderer, and the
                             parse+assert pass on crafted input.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a
                             raise path).
  3. End-to-end            - the real ../tetris/charmap.asm, asserting known boundary values. Skips
                             cleanly when the disassembly checkout is not present.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_charmap
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_charmap as pc  # noqa: E402


# A valid 47-entry corpus matching the real charmap.asm structure (used as the baseline that the
# edge-case tests perturb).
def _valid_corpus() -> list[tuple[str, int]]:
    rows = [(ch, 0x00 + i) for i, ch in enumerate("0123456789")]
    rows += [(ch, 0x0A + i) for i, ch in enumerate("abcdefghijklmnopqrstuvwxyz")]
    rows += [
        (".", 0x24), ("-", 0x25), ("×", 0x26), ("♥", 0x27), ("⋯", 0x29),
        (" ", 0x2F), ("©", 0x33), ("…", 0x60), ("”", 0x9B), (",", 0x9C), (".”", 0x9D),
    ]
    return rows


def _render(rows: list[tuple[str, int]]) -> str:
    return "".join(f'charmap "{seq}", ${tile:02X}\n' for seq, tile in rows)


class CppByteString(unittest.TestCase):
    def test_ascii_verbatim(self):
        self.assertEqual(pc.cpp_byte_string("0"), '"0"')
        self.assertEqual(pc.cpp_byte_string("a"), '"a"')
        self.assertEqual(pc.cpp_byte_string("-"), '"-"')
        self.assertEqual(pc.cpp_byte_string(" "), '" "')

    def test_single_multibyte(self):
        # U+00D7 MULTIPLICATION SIGN -> C3 97
        self.assertEqual(pc.cpp_byte_string("×"), '"\\xC3\\x97"')
        # U+2665 BLACK HEART -> E2 99 A5
        self.assertEqual(pc.cpp_byte_string("♥"), '"\\xE2\\x99\\xA5"')

    def test_ligature(self):
        # "." (verbatim) then U+201D -> E2 80 9D
        self.assertEqual(pc.cpp_byte_string(".”"), '".\\xE2\\x80\\x9D"')

    def test_quote_and_backslash_escaped(self):
        self.assertEqual(pc.cpp_byte_string('"'), '"\\""')
        self.assertEqual(pc.cpp_byte_string("\\"), '"\\\\"')

    def test_hex_digit_after_escape_splits_literal(self):
        # "©a": © -> C2 A9, then ASCII 'a' (a hex digit). C++ would fold \xA9a into one escape;
        # the emitter must split the literal so \xA9 stays two digits.
        self.assertEqual(pc.cpp_byte_string("©a"), '"\\xC2\\xA9" "a"')

    def test_output_is_ascii(self):
        for seq, _ in _valid_corpus():
            self.assertTrue(pc.cpp_byte_string(seq).isascii())


class ReadableComment(unittest.TestCase):
    def test_ascii_verbatim(self):
        self.assertEqual(pc.readable_comment("0"), "0")
        self.assertEqual(pc.readable_comment(" "), " ")

    def test_codepoint_notation(self):
        self.assertEqual(pc.readable_comment("×"), "<U+00D7>")
        self.assertEqual(pc.readable_comment(".”"), ".<U+201D>")


class ParseValid(unittest.TestCase):
    def test_full_corpus_parses(self):
        rows = pc.parse_charmap(_render(_valid_corpus()), Path("charmap.asm"))
        self.assertEqual(len(rows), 47)
        self.assertEqual(rows[0], ("0", 0x00))
        self.assertEqual(rows[-1], (".”", 0x9D))

    def test_trailing_comment_ignored(self):
        text = _render(_valid_corpus()).replace(
            'charmap "×", $26\n', 'charmap "×", $26 ; This is a multiplication sign\n'
        )
        rows = pc.parse_charmap(text, Path("charmap.asm"))
        self.assertEqual(dict(rows)["×"], 0x26)

    def test_blank_lines_skipped(self):
        rows = pc.parse_charmap(_render(_valid_corpus()) + "\n\n", Path("charmap.asm"))
        self.assertEqual(len(rows), 47)


class EdgeCasesMustRaise(unittest.TestCase):
    def test_wrong_count_raises(self):
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(_valid_corpus()[:-1]), Path("charmap.asm"))

    def test_non_matching_line_raises(self):
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(_valid_corpus()) + "this is not a charmap line\n",
                             Path("charmap.asm"))

    def test_duplicate_sequence_raises(self):
        rows = _valid_corpus()
        rows[11] = ("0", 0x24)  # "0" already at index 0 (tile 0x24 keeps tiles unique)
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))

    def test_duplicate_tile_raises(self):
        rows = _valid_corpus()
        rows[11] = ("!", 0x00)  # tile 0x00 already used by "0"
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))

    def test_digit_identity_violation_raises(self):
        rows = _valid_corpus()
        rows[5] = ("5", 0x77)  # "5" should map to 0x05
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))

    def test_letter_anchor_violation_raises(self):
        rows = _valid_corpus()
        rows[10] = ("a", 0x77)  # "a" should map to 0x0A
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))

    def test_extra_ligature_raises(self):
        rows = _valid_corpus()
        rows[36] = (".-", 0x24)  # a second two-code-point sequence breaks the ligature anchor
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))

    def test_ligature_wrong_tile_raises(self):
        rows = _valid_corpus()
        rows[-1] = (".”", 0x77)  # ligature must map to 0x9D
        with self.assertRaises(SystemExit):
            pc.parse_charmap(_render(rows), Path("charmap.asm"))


class AsciiPurity(unittest.TestCase):
    def test_emitted_inc_is_ascii(self):
        rows = pc.parse_charmap(_render(_valid_corpus()), Path("charmap.asm"))
        self.assertTrue(pc.emit_inc(rows, "abc1234").isascii())

    def test_emitted_fixture_is_ascii(self):
        rows = pc.parse_charmap(_render(_valid_corpus()), Path("charmap.asm"))
        self.assertTrue(pc.emit_fixture(rows, "abc1234").isascii())

    def test_assert_ascii_raises_on_nonascii(self):
        with self.assertRaises(SystemExit):
            pc._assert_ascii("café\n", "x.h")


def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "charmap.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")

    def test_real_charmap(self):
        text = (self.root / "charmap.asm").read_bytes().decode("utf-8")
        rows = pc.parse_charmap(text, self.root / "charmap.asm")
        table = dict(rows)
        self.assertEqual(len(rows), 47)
        # Spot boundary values across the table.
        self.assertEqual(table["0"], 0x00)
        self.assertEqual(table["t"], 0x1D)
        self.assertEqual(table["⋯"], 0x29)
        self.assertEqual(table[" "], 0x2F)
        self.assertEqual(table[".”"], 0x9D)
        # $28 is a tile-sheet glyph the charmap never names.
        self.assertNotIn(0x28, set(table.values()))


if __name__ == "__main__":
    unittest.main()
