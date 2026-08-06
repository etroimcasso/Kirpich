#!/usr/bin/env python3
"""Unit tests for parse_sprite_grids.py.

Three layers per the parser test discipline:
  1. Helper units          - the db-byte splitter and the parse+assert pass on crafted input.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a
                             raise path).
  3. End-to-end            - the real ../tetris/tetris.asm, asserting the five counts and known
                             boundary pairs. Skips cleanly when the disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_sprite_grids
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_sprite_grids as ps  # noqa: E402


# --- Valid synthetic corpus (the baseline the edge-case tests perturb) ---------------------------

def _flat(pairs: list[tuple[int, int]]) -> list[int]:
    return [b for pair in pairs for b in pair]


def _notched_pairs() -> list[tuple[int, int]]:
    pairs = [(8 * r, 8 * (c + 1)) for r in range(2) for c in range(2)]      # rows 0-1: 2 pairs
    pairs += [(8 * r, 8 * c) for r in range(2, 8) for c in range(4)]        # rows 2-7: 4 pairs
    return pairs


def _valid_tables() -> list[tuple[str, list[int]]]:
    return [
        ("Matrix_31A9", _flat([(8 * (i // 4), 8 * (i % 4)) for i in range(16)])),
        ("Matrix_31C9", _flat([(0, 8 * i) for i in range(8)])),
        ("Matrix_31D9", _flat([(8 * (i // 2), 8 * (i % 2)) for i in range(14)])),
        ("Matrix_31F5", _flat(_notched_pairs())),
        ("Matrix_322D", _flat([(8 * (i // 3), 8 * (i % 3)) for i in range(9)])),
    ]


def _render(tables: list[tuple[str, list[int]]], *, section: bool = True,
            terminal: bool = True) -> str:
    lines: list[str] = []
    if section:
        lines += [ps.SECTION_TEXT, ""]
    for label, flat in tables:
        lines.append(f"{label}::")
        lines.append("    db " + ", ".join(f"${b:02X}" for b in flat))
    if terminal:
        lines += ["GameplayTiles::", 'INCBIN "gfx/configandgameplay.2bpp"']
    return "\n".join(lines) + "\n"


P = Path("tetris.asm")


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class ParseDbBytes(unittest.TestCase):
    def test_splits_hex_bytes(self):
        self.assertEqual(ps._parse_db_bytes("$00, $08, $10", P, 1), [0x00, 0x08, 0x10])

    def test_inline_comment_dropped(self):
        self.assertEqual(ps._parse_db_bytes("$00, $38 ; a comment", P, 1), [0x00, 0x38])

    def test_bad_token_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_db_bytes("$00, nope", P, 1)


class ParseValid(unittest.TestCase):
    def test_full_corpus_parses(self):
        tables = ps.parse_sprite_grids(_render(_valid_tables()), P)
        self.assertEqual([len(t) for t in tables], [16, 8, 14, 28, 9])
        self.assertEqual(tables[0][5], (0x08, 0x08))      # 4x4[5]
        self.assertEqual(tables[4][4], (0x08, 0x08))      # 3x3[4]

    def test_trailing_comment_and_blank_lines_ignored(self):
        text = _render(_valid_tables()).replace(
            "Matrix_31C9::", "; a stray comment\n\nMatrix_31C9::", 1
        )
        tables = ps.parse_sprite_grids(text, P)
        self.assertEqual([len(t) for t in tables], [16, 8, 14, 28, 9])


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class EdgeCasesMustRaise(unittest.TestCase):
    def test_missing_section_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(_valid_tables(), section=False), P)

    def test_missing_label_raises(self):
        tables = [t for t in _valid_tables() if t[0] != "Matrix_31C9"]
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_wrong_label_order_raises(self):
        tables = _valid_tables()
        tables[1], tables[2] = tables[2], tables[1]  # swap Matrix_31C9 / Matrix_31D9
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_short_table_raises(self):
        tables = _valid_tables()
        tables[0] = ("Matrix_31A9", tables[0][1][:-2])  # 30 bytes, expected 32
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_odd_count_table_raises(self):
        tables = _valid_tables()
        tables[0] = ("Matrix_31A9", tables[0][1][:-1])  # 31 bytes - odd, and != 32
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_non_multiple_of_eight_byte_raises(self):
        tables = _valid_tables()
        bytes_ = list(tables[4][1])
        bytes_[0] = 0x04  # correct length, but not a multiple of 8
        tables[4] = ("Matrix_322D", bytes_)
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_byte_over_max_raises(self):
        tables = _valid_tables()
        bytes_ = list(tables[4][1])
        bytes_[0] = 0x40  # multiple of 8 but exceeds the $38 grid maximum
        tables[4] = ("Matrix_322D", bytes_)
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(tables), P)

    def test_stray_directive_between_tables_raises(self):
        text = _render(_valid_tables()).replace("Matrix_31C9::", "ds 4\nMatrix_31C9::", 1)
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(text, P)

    def test_db_before_first_label_raises(self):
        text = _render(_valid_tables()).replace("Matrix_31A9::", "    db $00, $00\nMatrix_31A9::", 1)
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(text, P)

    def test_terminal_too_early_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(_valid_tables()[:3]), P)

    def test_missing_terminal_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(_render(_valid_tables(), terminal=False), P)

    def test_extra_table_raises(self):
        text = _render(_valid_tables()).replace(
            "GameplayTiles::", "Matrix_9999::\n    db $00, $00\nGameplayTiles::", 1
        )
        with self.assertRaises(SystemExit):
            ps.parse_sprite_grids(text, P)


# --- Layer 2b: emit shape ------------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.tables = ps.parse_sprite_grids(_render(_valid_tables()), P)

    def test_inc_has_five_arrays_and_designated_rows(self):
        inc = ps.emit_inc(self.tables, "abc1234")
        for name in ("kSpriteGrid4x4", "kSpriteGrid1x8", "kSpriteGrid7x2",
                     "kSpriteGrid8x4Notched", "kSpriteGrid3x3"):
            self.assertIn(f"std::array<SpriteGridOffset, ", inc)
            self.assertIn(name, inc)
        self.assertIn("{ .y = 0x00, .x = 0x08 },", inc)
        self.assertNotIn("namespace kirpich {", inc)  # included at namespace scope; opens none
        self.assertTrue(inc.isascii())

    def test_fixture_has_expected_arrays_and_scaffolding(self):
        fixture = ps.emit_fixture(self.tables, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn('#include "data/sprite_grids.h"', fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        for name in ("kExpectedSpriteGrid4x4", "kExpectedSpriteGrid1x8", "kExpectedSpriteGrid7x2",
                     "kExpectedSpriteGrid8x4Notched", "kExpectedSpriteGrid3x3"):
            self.assertIn(name, fixture)
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
        tables = ps.parse_sprite_grids(text, self.root / "tetris.asm")
        grid4x4, grid1x8, grid7x2, notched, grid3x3 = tables

        self.assertEqual([len(t) for t in tables], [16, 8, 14, 28, 9])

        # Spot boundary pairs (hand-traced against tetris.asm:6902-6934).
        self.assertEqual(grid4x4[5], (0x08, 0x08))
        self.assertEqual(grid4x4[15], (0x18, 0x18))
        self.assertEqual(grid1x8[7], (0x00, 0x38))
        self.assertEqual(grid7x2[13], (0x30, 0x08))
        self.assertEqual(notched[0], (0x00, 0x08))   # the notch: row 0 starts at x=$08
        self.assertEqual(notched[3], (0x08, 0x10))
        self.assertEqual(notched[27], (0x38, 0x18))
        self.assertEqual(grid3x3[4], (0x08, 0x08))


if __name__ == "__main__":
    unittest.main()
