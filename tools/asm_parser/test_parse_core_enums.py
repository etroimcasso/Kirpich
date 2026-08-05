#!/usr/bin/env python3
"""Unit tests for parse_core_enums.py.

Three layers per the parser test discipline:
  1. Helper units          - parse_equ / assert_equ_values / the scanners on crafted input.
  2. Synthetic edge cases  - malformed fragments that MUST raise (drift is a hard error, not a
                             silent pass).
  3. End-to-end            - the real ../tetris source, asserting known boundary values. Skips
                             cleanly when the disassembly checkout is not present (unit-only run).

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_core_enums
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

# Make the parser importable whether run via the package path or via `discover -s tools/asm_parser`.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_core_enums as pce  # noqa: E402


def synthetic_tetris(
    values=range(0x00, 0x36),
    *,
    include_vestigial=True,
    include_labels=True,
    serial=pce.EXPECTED_SERIAL_DISPATCH,
) -> str:
    """Build a minimal tetris.asm-shaped fragment exercising both dispatch tables."""
    lines = [".tableJump", "    ldh a, [hSerialState]", "    rst $28             ; TableJump"]
    lines += [f"    dw {entry}" for entry in serial]
    lines += ["", "MainLoop::", ".dispatch", "    ldh a, [hGameState]", "    rst $28", ""]
    lines += [f"dw GameState_{v:02X} ; comment {v:02X}" for v in values]
    if include_vestigial:
        lines.append("dw $27EA ; 0x36")
    lines.append("")
    if include_labels:
        for v in values:
            lines += [f"GameState_{v:02X}::", "    ret"]
    return "\n".join(lines)


class EquHelpers(unittest.TestCase):
    def test_parse_equ_valid(self):
        text = ("MASTER EQU $29\nSLAVE  EQU $55\n"
                "SERIAL_TRANSFER_EXTERNAL_CLOCK EQU $80\n"
                "SERIAL_TRANSFER_INTERNAL_CLOCK EQU $81\n")
        table = pce.parse_equ(text, Path("constants.asm"))
        self.assertEqual(table["MASTER"], 0x29)
        self.assertEqual(table["SLAVE"], 0x55)
        self.assertEqual(table["SERIAL_TRANSFER_INTERNAL_CLOCK"], 0x81)

    def test_parse_equ_skips_blank_and_comment(self):
        table = pce.parse_equ("\n; a comment\nMASTER EQU $29\n", Path("constants.asm"))
        self.assertEqual(table, {"MASTER": 0x29})

    def test_parse_equ_rejects_garbage(self):
        with self.assertRaises(SystemExit):
            pce.parse_equ("this is not an equ line\n", Path("constants.asm"))

    def test_assert_equ_values_ok(self):
        pce.assert_equ_values({"MASTER": 0x29, "SLAVE": 0x55},
                              pce.EXPECTED_SERIAL_ROLE, Path("constants.asm"))

    def test_assert_equ_values_wrong_value_raises(self):
        with self.assertRaises(SystemExit):
            pce.assert_equ_values({"MASTER": 0x2A, "SLAVE": 0x55},
                                  pce.EXPECTED_SERIAL_ROLE, Path("constants.asm"))

    def test_assert_equ_values_missing_symbol_raises(self):
        with self.assertRaises(SystemExit):
            pce.assert_equ_values({"SLAVE": 0x55}, pce.EXPECTED_SERIAL_ROLE, Path("constants.asm"))


class GameStateScan(unittest.TestCase):
    def test_valid_54_states(self):
        rows = pce.scan_game_states(synthetic_tetris(), Path("tetris.asm"))
        self.assertEqual(len(rows), 54)
        self.assertEqual(rows[0], (0x00, "GameState_00", "comment 00"))
        self.assertEqual(rows[-1][0], 0x35)

    def test_short_count_raises(self):
        with self.assertRaises(SystemExit):
            pce.scan_game_states(synthetic_tetris(values=range(0x00, 0x35)), Path("tetris.asm"))

    def test_gap_in_values_raises(self):
        values = [v for v in range(0x00, 0x37) if v != 0x0A]  # 54 entries but non-contiguous
        self.assertEqual(len(values), 54)
        with self.assertRaises(SystemExit):
            pce.scan_game_states(synthetic_tetris(values=values), Path("tetris.asm"))

    def test_missing_vestigial_raises(self):
        with self.assertRaises(SystemExit):
            pce.scan_game_states(synthetic_tetris(include_vestigial=False), Path("tetris.asm"))

    def test_label_dispatch_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            pce.scan_game_states(synthetic_tetris(include_labels=False), Path("tetris.asm"))


class SerialStateScan(unittest.TestCase):
    def test_valid_dispatch(self):
        rows = pce.scan_serial_states(synthetic_tetris(), Path("tetris.asm"))
        self.assertEqual(rows, [(0, "Handshake"), (1, "SerialState_01"),
                                (2, "SerialState_02"), (3, "SerialState_03")])

    def test_reordered_dispatch_raises(self):
        bad = ["Handshake", "SerialState_02", "SerialState_01",
               "SerialState_03", "LoadTilesFromHL.ret"]
        with self.assertRaises(SystemExit):
            pce.scan_serial_states(synthetic_tetris(serial=bad), Path("tetris.asm"))


def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "constants.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")

    def test_equ_symbols(self):
        table = pce.parse_equ((self.root / "constants.asm").read_text(), self.root / "constants.asm")
        pce.assert_equ_values(table, pce.EXPECTED_SERIAL_ROLE, self.root / "constants.asm")
        pce.assert_equ_values(table, pce.EXPECTED_CLOCK_MODE, self.root / "constants.asm")

    def test_game_states(self):
        rows = pce.scan_game_states((self.root / "tetris.asm").read_text(), self.root / "tetris.asm")
        self.assertEqual(len(rows), 54)
        self.assertEqual(rows[0], (0x00, "GameState_00", "Normal gameplay"))
        self.assertEqual(rows[0x35][0], 0x35)
        self.assertIn("Copyright", rows[0x35][2])

    def test_serial_states(self):
        rows = pce.scan_serial_states((self.root / "tetris.asm").read_text(), self.root / "tetris.asm")
        self.assertEqual(rows, [(0, "Handshake"), (1, "SerialState_01"),
                                (2, "SerialState_02"), (3, "SerialState_03")])


if __name__ == "__main__":
    unittest.main()
