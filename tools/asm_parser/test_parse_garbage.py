#!/usr/bin/env python3
"""Unit tests for parse_garbage.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the expression evaluator and charmap-space lookup on crafted input,
                              plus a full valid synthetic corpus parsing to the right grid + constants.
  2. Synthetic edge cases   - malformed corpora that MUST raise (every structural assert has a raise
                              path), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris/tetris.asm, asserting the 40 table bytes, the six
                              constants, and the multiplayer sites. Skips cleanly when the disassembly
                              is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_garbage
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_garbage as g  # noqa: E402


P = Path("tetris.asm")
EMPTY = 0x2F


# --- Synthetic corpus builder (the baseline the edge-case tests perturb) ------------------------

_VALID_TABLE = [
    [0x85, 0x2F, 0x82, 0x86, 0x83, 0x2F, 0x2F, 0x80, 0x82, 0x85],
    [0x2F, 0x82, 0x84, 0x82, 0x83, 0x2F, 0x83, 0x2F, 0x87, 0x2F],
    [0x2F, 0x85, 0x2F, 0x83, 0x2F, 0x86, 0x82, 0x80, 0x81, 0x2F],
    [0x83, 0x2F, 0x86, 0x83, 0x2F, 0x85, 0x2F, 0x85, 0x2F, 0x2F],
]


def _table(rows=None) -> list[str]:
    rows = _VALID_TABLE if rows is None else rows
    out = ["TypeBDemoGarbage::"]
    out += ["    db " + ", ".join(f"${b:02X}" for b in row) for row in rows]
    return out


def _demo_stamp(*, ld_c=4, ld_b=10, hl=0x99C2, de_label="TypeBDemoGarbage", add=0x30,
                row_stride=0x0020) -> list[str]:
    lines = ["InitDemoGarbage::"]
    if hl is not None:
        lines.append(f"    ld hl, ${hl:04X}")
    if de_label is not None:
        lines.append(f"    ld de, {de_label}")
    if ld_c is not None:
        lines.append(f"    ld c, {ld_c}")
    lines.append(".nextRow")
    if ld_b is not None:
        lines.append(f"    ld b, {ld_b}")
    lines += ["    ld a, [de]", "    ld [hl], a", "    ld a, h"]
    if add is not None:
        lines.append(f"    add a, ${add:02X}")
    lines += ["    ld h, a", "    ld a, [de]", "    ld [hl], a", "    inc l", "    inc de",
              "    dec b", "    jr nz, .nextRow"]
    if row_stride is not None:
        lines.append(f"    ld de, ${row_stride:04X}")
    lines += ["    add hl, de", "    dec c", "    jr nz, .nextRow", "    ret"]
    return lines


def _typeb_site(*, hl=0x9A02, de="-2 * $20", height="ld a, b", demo=True) -> list[str]:
    lines = ["TypeBStart::", "    cp a, $77", "    jr nz, .done"]
    if demo:
        lines += ["    ldh a, [hDemoNumber]", "    and a", "    jr z, .fill",
                  "    call InitDemoGarbage", "    jr .done"]
    lines.append(".fill")
    if height is not None:
        lines.append(f"    {height}")
    if de is not None:
        lines.append(f"    ld de, {de}")
    if hl is not None:
        lines.append(f"    ld hl, ${hl:04X}")
    lines += ["    call InitGarbage", ".done", "    ret"]
    return lines


def _mp_site(nn: int, *, rows=6, hl=0xC9A2, de="-$20") -> list[str]:
    lines = [f"MpRound{nn}::"]
    if rows is not None:
        lines.append(f"    ld a, {rows}")
    if de is not None:
        lines.append(f"    ld de, {de}")
    if hl is not None:
        lines.append(f"    ld hl, ${hl:04X}")
    lines += ["    call InitGarbage", "    ret"]
    return lines


_FILL_BODY = """InitGarbage::
    ld b, a
.rowLoop
    dec b
    jr z, .fillLoop
    add hl, de
    jr .rowLoop
.fillLoop
    ldh a, [rDIV]
    ld b, a
.chooseBlock
    ld a, $80
.loop
    dec b
    jr z, .writeTile
    cp a, $80
    jr nz, .chooseBlock
    ld a, " "
    jr .loop
.writeTile
    cp a, " "
    jr z, .writeHole
    ldh a, [rDIV]
    and a, $07
    or a, $80
    jr .ensureAtLeastOneHole
.writeHole
    ldh [$A0], a
.ensureAtLeastOneHole
    push af
    ld a, l
    and a, $0F
    cp a, $0B
    jr nz, .popAndWrite
    ldh a, [$A0]
    cp a, " "
    jr z, .popAndWrite
    pop af
    ld a, " "
    jr .write
.popAndWrite
    pop af
.write
    ld [hl], a
    push hl
    push af
    ldh a, [hIsMultiplayer]
    and a
    jr nz, .skip
    ld de, $3000
    add hl, de
.skip
    pop af
    ld [hl], a
    pop hl
    inc hl
    ld a, l
    and a, $0F
    cp a, $0C
    jr nz, .fillLoop
    xor a
    ldh [$A0], a
    ld a, h
    and a, $0F
    cp a, $0A
    jr z, .secondOrFirstRow
.nextRow
    ld de, $20 - 10
    add hl, de
    jr .fillLoop
.secondOrFirstRow
    ld a, l
    cp a, $2C
    jr nz, .nextRow
    ret"""


def _render(*, table=None, demo_stamp=None, sites=None, fill=None) -> str:
    table = _table() if table is None else table
    demo_stamp = _demo_stamp() if demo_stamp is None else demo_stamp
    fill = _FILL_BODY.splitlines() if fill is None else fill
    if sites is None:
        sites = [_mp_site(1), _mp_site(2), _typeb_site()]

    lines = ['SECTION "rom", ROM0', ""]
    for site in sites:
        lines += site + [""]
    lines += demo_stamp + [""]
    lines += table + [""]
    lines += fill + [""]
    lines += ["AfterAll::", "    ret", ""]
    return "\n".join(lines) + "\n"


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class ExprEval(unittest.TestCase):
    def test_type_b_stride(self):
        self.assertEqual(g.eval_expr("-2 * $20", P, 1), -64)

    def test_multiplayer_stride(self):
        self.assertEqual(g.eval_expr("-$20", P, 1), -32)

    def test_row_wrap(self):
        self.assertEqual(g.eval_expr("$20 - 10", P, 1), 22)

    def test_hex_and_decimal_and_parens(self):
        self.assertEqual(g.eval_expr("$0020", P, 1), 32)
        self.assertEqual(g.eval_expr("6", P, 1), 6)
        self.assertEqual(g.eval_expr("(2 + 3) * $02", P, 1), 10)

    def test_bad_token_raises(self):
        with self.assertRaises(SystemExit):
            g.eval_expr("$20 & 7", P, 1)

    def test_empty_raises(self):
        with self.assertRaises(SystemExit):
            g.eval_expr("   ", P, 1)


class ParseValid(unittest.TestCase):
    def setUp(self):
        self.result = g.parse_garbage(_render(), EMPTY, P)

    def test_grid_matches(self):
        self.assertEqual(self.result["grid"], _VALID_TABLE)

    def test_constants(self):
        self.assertEqual(self.result["rows_per_height"], 2)
        self.assertEqual(self.result["multiplayer_rows"], 6)
        self.assertEqual(self.result["block_base"], 0x80)
        self.assertEqual(self.result["block_count"], 8)
        self.assertEqual(self.result["empty_tile"], 0x2F)


# --- Layer 2: table edge cases that MUST raise --------------------------------------------------

class TableEdgeCases(unittest.TestCase):
    def test_missing_table_label_raises(self):
        text = _render().replace("TypeBDemoGarbage::", "SomethingElse::")
        with self.assertRaises(SystemExit):
            g.parse_garbage(text, EMPTY, P)

    def test_too_few_rows_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(_VALID_TABLE[:3])), EMPTY, P)

    def test_too_many_rows_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(_VALID_TABLE + [_VALID_TABLE[0]])), EMPTY, P)

    def test_wrong_row_width_raises(self):
        bad = [row[:] for row in _VALID_TABLE]
        bad[1] = bad[1][:9]                              # 9-wide row
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(bad)), EMPTY, P)

    def test_out_of_domain_cell_raises(self):
        bad = [row[:] for row in _VALID_TABLE]
        bad[2][5] = 0x79                                 # below the block range and not the empty tile
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(bad)), EMPTY, P)

    def test_block_tile_over_range_raises(self):
        bad = [row[:] for row in _VALID_TABLE]
        bad[0][0] = 0x88                                 # one past $87
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(bad)), EMPTY, P)

    def test_row_with_no_hole_raises(self):
        bad = [row[:] for row in _VALID_TABLE]
        bad[3] = [0x80] * 10                             # every cell a block, no empty
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(table=_table(bad)), EMPTY, P)


# --- Layer 2: demo-stamp edge cases -------------------------------------------------------------

class DemoStampEdgeCases(unittest.TestCase):
    def test_missing_destination_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(hl=None)), EMPTY, P)

    def test_missing_table_source_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(de_label=None)), EMPTY, P)

    def test_missing_buffer_switch_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(add=None)), EMPTY, P)

    def test_missing_row_stride_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(row_stride=None)), EMPTY, P)

    def test_stamp_row_count_disagrees_with_table_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(ld_c=5)), EMPTY, P)

    def test_stamp_col_count_wrong_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(demo_stamp=_demo_stamp(ld_b=8)), EMPTY, P)


# --- Layer 2: call-site edge cases --------------------------------------------------------------

class CallSiteEdgeCases(unittest.TestCase):
    def test_wrong_total_call_count_raises(self):
        # Drop the Type B site entirely -> only 2 `call InitGarbage`.
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=[_mp_site(1), _mp_site(2)]), EMPTY, P)

    def test_no_type_b_site_raises(self):
        # Three calls, but all multiplayer-shaped -> zero Type B sites.
        sites = [_mp_site(1), _mp_site(2), _mp_site(3)]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_one_multiplayer_site_raises(self):
        # Two Type B-shaped sites + one multiplayer -> only one multiplayer.
        sites = [_typeb_site(demo=True), _typeb_site(demo=False), _mp_site(1)]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_unequal_multiplayer_rows_raises(self):
        sites = [_mp_site(1, rows=6), _mp_site(2, rows=5), _typeb_site()]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_type_b_wrong_offset_raises(self):
        sites = [_mp_site(1), _mp_site(2), _typeb_site(de="-3 * $20")]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_type_b_missing_height_load_raises(self):
        sites = [_mp_site(1), _mp_site(2), _typeb_site(height="ld a, 4")]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_multiplayer_wrong_offset_raises(self):
        sites = [_mp_site(1, de="-2 * $20"), _mp_site(2), _typeb_site()]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_unknown_destination_raises(self):
        sites = [_mp_site(1), _mp_site(2), _typeb_site(hl=0x9999)]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)

    def test_missing_demo_branch_raises(self):
        sites = [_mp_site(1), _mp_site(2), _typeb_site(demo=False)]
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(sites=sites), EMPTY, P)


# --- Layer 2: fill-mechanism edge cases ---------------------------------------------------------

class FillMechanismEdgeCases(unittest.TestCase):
    def _fill_without(self, needle: str) -> list[str]:
        return [line for line in _FILL_BODY.splitlines() if needle not in line]

    def test_one_rdiv_read_raises(self):
        # Remove the second rDIV read (the tile pick).
        fill = _FILL_BODY.replace("    ldh a, [rDIV]\n    and a, $07",
                                  "    ld a, b\n    and a, $07").splitlines()
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=fill), EMPTY, P)

    def test_missing_block_arm_raises(self):
        fill = _FILL_BODY.replace("    ld a, $80", "    ld a, $70").splitlines()
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=fill), EMPTY, P)

    def test_missing_tile_mask_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("and a, $07")), EMPTY, P)

    def test_missing_block_base_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("or a, $80")), EMPTY, P)

    def test_missing_rightmost_check_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("cp a, $0B")), EMPTY, P)

    def test_missing_multiplayer_skip_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("hIsMultiplayer")), EMPTY, P)

    def test_missing_buffer_offset_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("ld de, $3000")), EMPTY, P)

    def test_missing_row_wrap_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("ld de, $20 - 10")), EMPTY, P)

    def test_missing_termination_low_raises(self):
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(fill=self._fill_without("cp a, $2C")), EMPTY, P)

    def test_empty_arm_wrong_charmap_value_raises(self):
        # If the charmap mapped " " to something other than $2F, the fill/table would disagree.
        with self.assertRaises(SystemExit):
            g.parse_garbage(_render(), 0x40, P)


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.result = g.parse_garbage(_render(), EMPTY, P)

    def test_inc_has_the_six_constants_and_grid(self):
        inc = g.emit_inc(self.result, "abc1234")
        self.assertIn("inline constexpr std::uint8_t kTypeBDemoGarbageRows = 4;", inc)
        self.assertIn("inline constexpr std::uint8_t kTypeBGarbageRowsPerHeight = 2;", inc)
        self.assertIn("inline constexpr std::uint8_t kMultiplayerRoundStartGarbageRows = 6;", inc)
        self.assertIn("inline constexpr std::uint8_t kGarbageBlockTileBase = 0x80;", inc)
        self.assertIn("inline constexpr std::uint8_t kGarbageBlockTileCount = 8;", inc)
        self.assertIn("inline constexpr std::uint8_t kGarbageEmptyTile = 0x2F;", inc)
        self.assertIn("kTypeBDemoGarbage = {{", inc)
        self.assertIn("{{ 0x85, 0x2F, 0x82, 0x86, 0x83, 0x2F, 0x2F, 0x80, 0x82, 0x85 }},", inc)
        self.assertNotIn("namespace kirpich {", inc)  # included mid-namespace; opens nothing
        self.assertTrue(inc.isascii())

    def test_fixture_is_flat_bytes_independent_of_surface(self):
        fixture = g.emit_fixture(self.result, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("std::array<std::uint8_t, 40> kExpectedTypeBDemoGarbageBytes", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        self.assertIn("0x85, 0x2F, 0x82, 0x86, 0x83, 0x2F, 0x2F, 0x80, 0x82, 0x85,", fixture)
        # Independent of the port surface: it does not include or name the header it guards.
        self.assertNotIn('#include "data/garbage.h"', fixture)
        self.assertNotIn("kTypeBDemoGarbage ", fixture)
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
        empty = g.charmap_space(self.root)
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        self.result = g.parse_garbage(text, empty, self.root / "tetris.asm")

    def test_charmap_space_is_2f(self):
        self.assertEqual(g.charmap_space(self.root), 0x2F)

    def test_grid_and_constants(self):
        self.assertEqual(self.result["grid"], _VALID_TABLE)
        self.assertEqual(self.result["rows_per_height"], 2)
        self.assertEqual(self.result["multiplayer_rows"], 6)
        self.assertEqual(self.result["block_base"], 0x80)
        self.assertEqual(self.result["block_count"], 8)
        self.assertEqual(self.result["empty_tile"], 0x2F)

    def test_flat_fixture_is_forty_bytes(self):
        flat = [b for row in self.result["grid"] for b in row]
        self.assertEqual(len(flat), 40)
        # Boundary corners, hand-traced against tetris.asm:4317-4320.
        self.assertEqual(flat[0], 0x85)
        self.assertEqual(flat[9], 0x85)
        self.assertEqual(flat[30], 0x83)
        self.assertEqual(flat[39], 0x2F)


if __name__ == "__main__":
    unittest.main()
