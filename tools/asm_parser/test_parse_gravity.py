#!/usr/bin/env python3
"""Unit tests for parse_gravity.py.

Three layers per the parser test discipline:
  1. Helper units          - the comment splitter and the decimal-byte reader on crafted input.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a
                             raise path), plus the shape of both emitted artifacts.
  3. End-to-end            - the real ../tetris/tetris.asm, asserting the row count and the known
                             boundary values. Skips cleanly when the disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_gravity
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_gravity as pg  # noqa: E402


# --- Valid synthetic corpus (the baseline the edge-case tests perturb) --------------------------

REAL_VALUES = [52, 48, 44, 40, 36, 32, 27, 21, 16, 10, 9, 8, 7, 6, 5, 5, 4, 4, 3, 3, 2]


def _render(values: list[int], *, label: bool = True, terminal: bool = True,
            anchors: dict[int, str] | None = None) -> str:
    """Render a synthetic tetris.asm fragment holding the table."""
    if anchors is None:
        anchors = pg.COMMENT_ANCHORS
    lines = ["SomeEarlierRoutine::", "    ret", ""]
    if label:
        lines.append(f"{pg.TABLE_LABEL}::")
    for i, value in enumerate(values):
        comment = anchors.get(i)
        lines.append(f"    db {value}" + (f"               ; {comment}" if comment else ""))
    if terminal:
        lines += ["", "; a trailing comment", "InitDemoGarbage::", "    ret"]
    return "\n".join(lines) + "\n"


P = Path("tetris.asm")


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class SplitComment(unittest.TestCase):
    def test_splits_directive_and_comment(self):
        self.assertEqual(pg._split_comment("db 52    ; Level 0"), ("db 52", "Level 0"))

    def test_no_comment_yields_empty_text(self):
        self.assertEqual(pg._split_comment("db 48"), ("db 48", ""))


class ParseDecimalByte(unittest.TestCase):
    def test_reads_decimal(self):
        self.assertEqual(pg._parse_decimal_byte("52", P, 1), 52)

    def test_hex_literal_raises(self):
        with self.assertRaises(SystemExit):
            pg._parse_decimal_byte("$34", P, 1)

    def test_over_a_byte_raises(self):
        with self.assertRaises(SystemExit):
            pg._parse_decimal_byte("256", P, 1)


class ParseValid(unittest.TestCase):
    def test_full_corpus_parses(self):
        values = pg.parse_gravity(_render(REAL_VALUES), P)
        self.assertEqual(values, REAL_VALUES)

    def test_blank_and_comment_lines_inside_the_table_ignored(self):
        text = _render(REAL_VALUES).replace("    db 44", "\n; an interjection\n    db 44", 1)
        self.assertEqual(pg.parse_gravity(text, P), REAL_VALUES)


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class EdgeCasesMustRaise(unittest.TestCase):
    def test_missing_label_raises(self):
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES, label=False), P)

    def test_duplicate_label_raises(self):
        text = _render(REAL_VALUES) + f"\n{pg.TABLE_LABEL}::\n    db 52               ; Level 0\n"
        with self.assertRaises(SystemExit):
            pg.parse_gravity(text, P)

    def test_too_few_rows_raises(self):
        anchors = {0: "Level 0", 10: "Level 10"}  # row 20 no longer exists
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES[:20], anchors=anchors), P)

    def test_too_many_rows_raises(self):
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES + [2]), P)

    def test_non_db_line_inside_the_table_raises(self):
        text = _render(REAL_VALUES).replace("    db 44", "    ds 1\n    db 44", 1)
        with self.assertRaises(SystemExit):
            pg.parse_gravity(text, P)

    def test_multi_value_db_raises(self):
        text = _render(REAL_VALUES).replace("    db 44", "    db 44, 40", 1)
        with self.assertRaises(SystemExit):
            pg.parse_gravity(text, P)

    def test_hex_value_raises(self):
        text = _render(REAL_VALUES).replace("    db 44", "    db $2C", 1)
        with self.assertRaises(SystemExit):
            pg.parse_gravity(text, P)

    def test_value_over_a_byte_raises(self):
        text = _render(REAL_VALUES).replace("    db 44", "    db 300", 1)
        with self.assertRaises(SystemExit):
            pg.parse_gravity(text, P)

    def test_missing_first_anchor_raises(self):
        anchors = {10: "Level 10", 20: "Level 20"}
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES, anchors=anchors), P)

    def test_wrong_middle_anchor_raises(self):
        anchors = {0: "Level 0", 10: "Level 9", 20: "Level 20"}
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES, anchors=anchors), P)

    def test_missing_last_anchor_raises(self):
        anchors = {0: "Level 0", 10: "Level 10"}
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES, anchors=anchors), P)

    def test_shifted_rows_break_an_anchor(self):
        """A row dropped ahead of an anchor slides every later row - the anchors catch it."""
        values = REAL_VALUES[:5] + REAL_VALUES[6:]  # 20 rows; level 10's value now sits at row 9
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(values), P)

    def test_missing_terminal_label_raises(self):
        with self.assertRaises(SystemExit):
            pg.parse_gravity(_render(REAL_VALUES, terminal=False), P)


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.values = pg.parse_gravity(_render(REAL_VALUES), P)

    def test_inc_has_designated_rows_for_every_level(self):
        inc = pg.emit_inc(self.values, "abc1234")
        self.assertEqual(inc.count(".level ="), pg.ROW_COUNT)
        self.assertIn("{ .level =  0, .frames = 52 },", inc)
        self.assertIn("{ .level = 20, .frames =  2 },", inc)
        self.assertNotIn("namespace kirpich {", inc)  # included mid-initializer; opens nothing
        self.assertTrue(inc.isascii())

    def test_fixture_is_raw_bytes_with_no_port_type(self):
        fixture = pg.emit_fixture(self.values, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn(f"std::array<std::uint8_t, {pg.ROW_COUNT}> {pg.CPP_FIXTURE}", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        # Independent of the surface it guards: no include of it, no use of its type.
        self.assertNotIn('#include "data/gravity.h"', fixture)
        self.assertNotIn("FramesPerDropEntry", fixture)
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
        if candidate and (candidate / "tetris.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")

    def test_real_asm(self):
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        values = pg.parse_gravity(text, self.root / "tetris.asm")

        self.assertEqual(len(values), pg.ROW_COUNT)

        # Boundary values, hand-traced against tetris.asm:4262-4283.
        self.assertEqual(values[0], 52)    # ; Level 0
        self.assertEqual(values[5], 32)
        self.assertEqual(values[9], 10)    # the level 9 -> 10 cliff, above ...
        self.assertEqual(values[10], 9)    # ... and below - ; Level 10
        self.assertEqual(values[15], 5)
        self.assertEqual(values[20], 2)    # ; Level 20

    def test_real_table_never_increases(self):
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        values = pg.parse_gravity(text, self.root / "tetris.asm")
        for level in range(1, len(values)):
            self.assertLessEqual(values[level], values[level - 1])


if __name__ == "__main__":
    unittest.main()
