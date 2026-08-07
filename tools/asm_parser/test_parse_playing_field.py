#!/usr/bin/env python3
"""Unit tests for parse_playing_field.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the closed-form address math and the comment splitter on crafted
                              input, plus a full valid synthetic corpus parsing to the right rows.
  2. Synthetic edge cases   - malformed corpora that MUST raise (every structural assert has a raise
                              path), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris/tetris.asm, asserting the 18 triples, the geometry,
                              and the boundary rows. Skips cleanly when the disassembly is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_playing_field
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_playing_field as pf  # noqa: E402


P = Path("tetris.asm")


# --- Synthetic corpus builder (the baseline the edge-case tests perturb) ------------------------

def _gate(gate_nn: int) -> list[str]:
    return [f"    ldh a, [{pf.COUNTER_VAR}]", f"    cp a, {gate_nn}", "    ret nz"]


def _wipe(nn: int, *, gate: int | None = None, vram: int | None = None, wram: int | None = None,
          hl: bool = True, de: bool = True, call: bool = True, de_first: bool = False,
          reset: bool = False) -> list[str]:
    """One PlayingFieldWipeNN routine. Defaults produce the exact source shape; each keyword lets a
    test break one part of it."""
    gate = nn if gate is None else gate
    vram = pf.expected_vram(nn) if vram is None else vram
    wram = pf.expected_wram(nn) if wram is None else wram
    out = [f"PlayingFieldWipe{nn:02d}::", *_gate(gate)]
    hl_line = f"    ld hl, ${vram:04X}"
    de_line = f"    ld de, ${wram:04X}"
    loads = []
    if de_first:
        if de:
            loads.append(de_line)
        if hl:
            loads.append(hl_line)
    else:
        if hl:
            loads.append(hl_line)
        if de:
            loads.append(de_line)
    out += loads
    if call:
        out.append(f"    call {pf.WIPE_ROW_LABEL}")
    if reset:
        out += ["    xor a", f"    ldh [{pf.COUNTER_VAR}], a"]
    out.append("    ret")
    return out


def _wipe_row(*, ld_b: int | None = 10, increment: bool = True) -> list[str]:
    out = [f"{pf.WIPE_ROW_LABEL}::"]
    if ld_b is not None:
        out.append(f"    ld b, {ld_b}")
    out += [".loop", "    ld a, [de]", "    ld [hl], a", "    inc l", "    inc e",
            "    dec b", "    jr nz, .loop"]
    if increment:
        out += [f"    ldh a, [{pf.COUNTER_VAR}]", "    inc a", f"    ldh [{pf.COUNTER_VAR}], a"]
    out.append("    ret")
    return out


def _dispatch(order: list[int]) -> list[str]:
    out = ["VBlank::", "    call AnimateLineClear   ; unrelated"]
    out += [f"    call PlayingFieldWipe{nn:02d}" for nn in order]
    out += ["    call UpdateScoreboard", "    ret"]
    return out


def _render(*, labels: list[int] | None = None, dispatch: list[int] | None = None,
            ld_b: int | None = 10, increment: bool = True, wipe19_reset: bool = True,
            include_wipe_row: bool = True, overrides: dict | None = None) -> str:
    labels = list(range(2, 20)) if labels is None else list(labels)
    dispatch = list(range(19, 1, -1)) if dispatch is None else list(dispatch)
    overrides = overrides or {}

    lines = ['SECTION "rom", ROM0', ""]
    lines += _dispatch(dispatch)
    lines += ["", "SomeRoutine::", "    ret", "", "; the wipe routines follow"]
    for nn in labels:
        kw = dict(overrides.get(nn, {}))
        if nn == 19 and "reset" not in kw:
            kw["reset"] = wipe19_reset
        lines += _wipe(nn, **kw)
        lines.append("")
    if include_wipe_row:
        lines += _wipe_row(ld_b=ld_b, increment=increment)
        lines.append("")
    lines += ["AfterAll::", "    ret", ""]
    return "\n".join(lines) + "\n"


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class ClosedForm(unittest.TestCase):
    def test_vram_endpoints(self):
        self.assertEqual(pf.expected_vram(2), 0x9A22)   # bottom row
        self.assertEqual(pf.expected_vram(19), 0x9802)  # top row

    def test_wram_endpoints(self):
        self.assertEqual(pf.expected_wram(2), 0xCA22)
        self.assertEqual(pf.expected_wram(19), 0xC802)

    def test_interior_values(self):
        self.assertEqual(pf.expected_vram(8), 0x9962)
        self.assertEqual(pf.expected_wram(8), 0xC962)
        self.assertEqual(pf.expected_vram(13), 0x98C2)
        self.assertEqual(pf.expected_wram(13), 0xC8C2)


class SplitComment(unittest.TestCase):
    def test_strips_trailing_comment(self):
        self.assertEqual(pf._split_comment("ld hl, $9802   ; the top row"), "ld hl, $9802")

    def test_no_comment_returns_body(self):
        self.assertEqual(pf._split_comment("    ret nz"), "ret nz")


class ParseValid(unittest.TestCase):
    def setUp(self):
        self.result = pf.parse_playing_field(_render(), P)

    def test_eighteen_rows_in_counter_order(self):
        rows = self.result["rows"]
        self.assertEqual(len(rows), 18)
        self.assertEqual([counter for counter, _v, _w in rows], list(range(2, 20)))

    def test_rows_hold_closed_form_addresses(self):
        for counter, vram, wram in self.result["rows"]:
            self.assertEqual(vram, pf.expected_vram(counter))
            self.assertEqual(wram, pf.expected_wram(counter))

    def test_cols_from_wipe_row(self):
        self.assertEqual(self.result["cols"], 10)

    def test_wipe19_intervening_and_extra_tails_tolerated(self):
        """The real Wipe19 has `ld [$C0C7], a` between gate and load; other wipes have SFX/print
        tails. Opaque code around the mandatory skeleton must not break the parse."""
        overrides = {
            19: {"reset": True},
            8: {},   # placeholder; tails added below by raw injection
        }
        text = _render(overrides=overrides)
        # Inject an intervening store in Wipe19 (between `ret nz` and `ld hl`) and a print tail.
        text = text.replace("PlayingFieldWipe19::\n    ldh a, [hWipeCounter]\n    cp a, 19\n"
                            "    ret nz\n    ld hl, $9802",
                            "PlayingFieldWipe19::\n    ldh a, [hWipeCounter]\n    cp a, 19\n"
                            "    ret nz\n    ld [$C0C7], a\n    ld hl, $9802")
        result = pf.parse_playing_field(text, P)
        self.assertEqual(len(result["rows"]), 18)


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class EdgeCasesMustRaise(unittest.TestCase):
    def test_missing_a_label_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(labels=list(range(2, 19))), P)  # no Wipe19

    def test_noncontiguous_labels_raise(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(labels=list(range(2, 19)) + [20]), P)

    def test_out_of_order_labels_raise(self):
        order = list(range(2, 18)) + [19, 18]
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(labels=order), P)

    def test_gate_operand_not_matching_suffix_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"gate": 6}}), P)

    def test_missing_ld_hl_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"hl": False}}), P)

    def test_missing_ld_de_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"de": False}}), P)

    def test_missing_call_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"call": False}}), P)

    def test_hl_after_de_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"de_first": True}}), P)

    def test_non_closed_form_vram_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"vram": 0x9999}}), P)

    def test_non_closed_form_wram_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(overrides={5: {"wram": 0xC9E2}}), P)

    def test_missing_ld_b_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(ld_b=None), P)

    def test_missing_counter_increment_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(increment=False), P)

    def test_missing_wipe19_reset_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(wipe19_reset=False), P)

    def test_missing_wipe_row_routine_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(include_wipe_row=False), P)

    def test_ascending_dispatch_order_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(dispatch=list(range(2, 20))), P)

    def test_dispatch_missing_a_call_raises(self):
        with self.assertRaises(SystemExit):
            pf.parse_playing_field(_render(dispatch=list(range(19, 2, -1))), P)  # 19..3, no 02


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.result = pf.parse_playing_field(_render(), P)

    def test_inc_has_the_four_constants(self):
        inc = pf.emit_inc(self.result, "abc1234")
        self.assertIn("inline constexpr std::uint8_t kPlayingFieldRows = 18;", inc)
        self.assertIn("inline constexpr std::uint8_t kPlayingFieldCols = 10;", inc)
        self.assertIn("inline constexpr std::uint8_t kPlayingFieldWipeCounterFirst = 2;", inc)
        self.assertIn("inline constexpr std::uint8_t kPlayingFieldWipeCounterLast = 19;", inc)
        self.assertNotIn("namespace kirpich {", inc)  # included mid-namespace; opens nothing
        self.assertTrue(inc.isascii())

    def test_fixture_is_raw_triples_independent_of_the_surface(self):
        fixture = pf.emit_fixture(self.result, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn(f"std::array<{pf.CPP_FIXTURE_ROW}, 18> {pf.CPP_FIXTURE}", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        self.assertIn("{ .counter =  2, .vram = 0x9A22, .wram = 0xCA22 },", fixture)
        self.assertIn("{ .counter = 19, .vram = 0x9802, .wram = 0xC802 },", fixture)
        self.assertEqual(fixture.count(".counter ="), 18)
        # Independent of the port surface: it does not include or name the header it guards.
        self.assertNotIn('#include "data/playing_field.h"', fixture)
        self.assertNotIn("playingFieldRowForWipeCounter", fixture)
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
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        self.result = pf.parse_playing_field(text, self.root / "tetris.asm")

    def test_geometry_and_domain(self):
        rows = self.result["rows"]
        self.assertEqual(len(rows), 18)
        self.assertEqual(self.result["cols"], 10)
        self.assertEqual([counter for counter, _v, _w in rows], list(range(2, 20)))

    def test_boundary_and_interior_triples(self):
        by_counter = {counter: (vram, wram) for counter, vram, wram in self.result["rows"]}
        # Hand-traced against tetris.asm:5563-5758.
        self.assertEqual(by_counter[2], (0x9A22, 0xCA22))   # bottom row
        self.assertEqual(by_counter[8], (0x9962, 0xC962))
        self.assertEqual(by_counter[13], (0x98C2, 0xC8C2))
        self.assertEqual(by_counter[19], (0x9802, 0xC802))  # top row


if __name__ == "__main__":
    unittest.main()
