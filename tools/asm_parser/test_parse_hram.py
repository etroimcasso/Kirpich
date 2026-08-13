#!/usr/bin/env python3
"""Unit tests for parse_hram.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the expression evaluator, the ds self-check, the directive classifier,
                              the ldh-operand and pointer-operand resolvers, a full walk of a crafted
                              HRAM section (fields, alias grouping, a commented-out slot that becomes
                              a gap, a same-line directive, a single-colon label), and a census scan
                              of crafted tetris-style code.
  2. Synthetic edge cases   - malformed input that MUST raise: gap-arithmetic mismatch, unknown
                              directive, content before a section, a dangling label at EOF, a walk
                              that does not end at $FFFE, an overrun past $FFFE, and an unrecognized
                              ldh operand form (never a silent skip) - plus the emitted fixture shape.
  3. End-to-end             - the real ../tetris/hram.asm + tetris.asm: 60 fields / 1 alias / 19 gaps,
                              the section span, the layout boundary + alias pins, and the census
                              pinned census counts ($FF98 = 14, $FFA0 = 22, $FF8C = 2, ...). Skips
                              when the disassembly is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_hram
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_hram as hr  # noqa: E402

P = Path("hram.asm")
T = Path("tetris.asm")


def _text(*rows: str) -> str:
    return "\n".join(rows) + "\n"


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class EvalExpr(unittest.TestCase):
    def test_hex_and_decimal(self):
        self.assertEqual(hr._eval_expr("$A0", P, 1), 160)
        self.assertEqual(hr._eval_expr("256", P, 1), 256)

    def test_addition_offset(self):
        self.assertEqual(hr._eval_expr("$86 + 6", P, 1), 0x8C)   # the ldh expression form
        self.assertEqual(hr._eval_expr("$86 + 1", P, 1), 0x87)

    def test_subtraction(self):
        self.assertEqual(hr._eval_expr("$FF9E - $FF9B", P, 1), 3)

    def test_garbage_raises(self):
        with self.assertRaises(hr.ParseError):
            hr._eval_expr("$A0 +", P, 1)
        with self.assertRaises(hr.ParseError):
            hr._eval_expr("nonsense", P, 1)


class DsSize(unittest.TestCase):
    def test_absolute_gap_self_check_passes(self):
        self.assertEqual(hr._ds_size("$FF9E - $FF9B", 0xFF9B, P, 1), 3)

    def test_absolute_gap_self_check_fails(self):
        with self.assertRaises(hr.ParseError):
            hr._ds_size("$FF9E - $FF9B", 0xFF9C, P, 1)   # $FF9B != running $FF9C

    def test_plain_count(self):
        self.assertEqual(hr._ds_size("$A", 0xFFB6, P, 1), 10)    # hDMARoutine

    def test_zero_raises(self):
        with self.assertRaises(hr.ParseError):
            hr._ds_size("$FF80 - $FF80", 0xFF80, P, 1)


class DirectiveBytes(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual(hr._directive_bytes("db", 0xFF80, P, 1), 1)
        self.assertEqual(hr._directive_bytes("dw", 0xFF80, P, 1), 2)
        self.assertEqual(hr._directive_bytes("ds $A", 0xFF80, P, 1), 10)

    def test_unknown_raises(self):
        with self.assertRaises(hr.ParseError):
            hr._directive_bytes("nop", 0xFF80, P, 1)


class LdhOperand(unittest.TestCase):
    def test_short_numeric_is_ff00_plus_n8(self):
        self.assertEqual(hr._resolve_ldh_operand("$98", T, 1), 0xFF98)
        self.assertEqual(hr._resolve_ldh_operand("$A0", T, 1), 0xFFA0)

    def test_long_numeric_used_directly(self):
        self.assertEqual(hr._resolve_ldh_operand("$FFD1", T, 1), 0xFFD1)

    def test_expression_form(self):
        self.assertEqual(hr._resolve_ldh_operand("$86 + 6", T, 1), 0xFF8C)

    def test_hardware_register_short_form_resolves_but_is_low(self):
        # $40 -> $FF40 (rLCDC): the resolver returns it; the census record() filters it out of scope.
        self.assertEqual(hr._resolve_ldh_operand("$40", T, 1), 0xFF40)

    def test_symbols_skip(self):
        for sym in ("c", "rLCDC", "hGameState", "hLines + 1"):
            self.assertIsNone(hr._resolve_ldh_operand(sym, T, 1), sym)

    def test_unrecognized_form_hard_errors(self):
        with self.assertRaises(hr.ParseError):
            hr._resolve_ldh_operand("$GG", T, 1)
        with self.assertRaises(hr.ParseError):
            hr._resolve_ldh_operand("$FF00 + c", T, 1)


class PtrOperand(unittest.TestCase):
    SYMS = {"hLines": 0xFF9E, "hFrameCounter": 0xFFE2, "hTempPreviewPiece": 0xFFFC}

    def test_numeric_hram_literal(self):
        self.assertEqual(hr._resolve_ptr_operand("$FFC6", self.SYMS), 0xFFC6)
        self.assertEqual(hr._resolve_ptr_operand("$FFFE", self.SYMS), 0xFFFE)

    def test_label_and_label_offset(self):
        self.assertEqual(hr._resolve_ptr_operand("hLines", self.SYMS), 0xFF9E)
        self.assertEqual(hr._resolve_ptr_operand("hLines + 1", self.SYMS), 0xFF9F)
        self.assertEqual(hr._resolve_ptr_operand("hFrameCounter", self.SYMS), 0xFFE2)

    def test_non_hram_operands_skip(self):
        self.assertIsNone(hr._resolve_ptr_operand("$FEFF", self.SYMS))   # OAM, below HRAM
        self.assertIsNone(hr._resolve_ptr_operand("wOAMBuffer", self.SYMS))  # not an HRAM label
        self.assertIsNone(hr._resolve_ptr_operand("DMARoutine", self.SYMS))


class WalkValid(unittest.TestCase):
    def test_field_alias_gap_slot_and_singlecolon(self):
        text = _text(
            'SECTION "HRAM", HRAM',
            "hA:: db",              # $FF80 field, same-line directive
            "hB::",                 # $FF81 field
            "    db",
            ";hC::",                # commented-out slot ...
            "    db",               # ... the bare db is an anonymous gap at $FF82
            "hVLatch:",             # single-colon export label at $FF83
            "    db",
            "hD:: dw",              # $FF84 field, 2 bytes -> ends $FF86
            "hAlias1::",            # two labels share $FF86
            "hAlias2::",
            "    db",
            "ds $FFFE - $FF87",     # big gap to the required end
        )
        regions, sections = hr.parse_layout(text, P)
        by_name = {r.name: r for r in regions if r.name}

        self.assertEqual((by_name["hA"].address, by_name["hA"].size), (0xFF80, 1))
        self.assertEqual((by_name["hB"].address, by_name["hB"].size), (0xFF81, 1))
        self.assertEqual(by_name["hVLatch"].address, 0xFF83)
        self.assertEqual((by_name["hD"].address, by_name["hD"].size), (0xFF84, 2))
        self.assertEqual(by_name["hAlias1"].kind, hr.Kind.FIELD)
        self.assertEqual(by_name["hAlias2"].kind, hr.Kind.ALIAS)
        self.assertEqual(by_name["hAlias1"].address, by_name["hAlias2"].address)
        self.assertEqual(by_name["hAlias2"].address, 0xFF86)

        gaps = [r for r in regions if r.kind is hr.Kind.GAP]
        self.assertEqual(gaps[0].address, 0xFF82)          # the commented-out slot
        self.assertEqual(gaps[0].size, 1)
        self.assertEqual(sections[0].origin, 0xFF80)
        self.assertEqual(sections[0].end, 0xFFFE)          # the walk ends exactly at $FFFE

    def test_symbol_table_includes_alias(self):
        text = _text(
            'SECTION "HRAM", HRAM',
            "hLo::",
            "hAka::",
            "    db",
            "ds $FFFE - $FF81",
        )
        regions, _ = hr.parse_layout(text, P)
        syms = hr.symbol_table(regions)
        self.assertEqual(syms["hLo"], 0xFF80)
        self.assertEqual(syms["hAka"], 0xFF80)             # the alias resolves too


class CensusValid(unittest.TestCase):
    SYMS = {"hLines": 0xFF9E, "hGameState": 0xFFE1}

    def test_counts_by_address(self):
        asm = _text(
            "    ldh [$98], a",              # $FF98
            "    ldh a, [$98]",              # $FF98 again -> 2
            "    ldh [$40], a",              # $FF40 hardware -> skipped
            "    ldh a, [$FFD1]",            # $FFD1
            "    ldh a, [$86 + 6]",          # $FF8C
            "    ldh [hGameState], a",       # symbolic -> skipped
            "    ldh [c], a",                # dynamic -> skipped
            "    ld hl, $FFC6",              # $FFC6
            "    ld de, hLines + 1",         # $FF9F
            "    ld de, wOAMBuffer",         # non-HRAM -> skipped
            "    ld hl, $FEFF",              # below HRAM -> skipped
        )
        rows = hr.census(asm, self.SYMS, T)
        got = {c.address: c.ref_count for c in rows}
        self.assertEqual(got, {0xFF98: 2, 0xFFD1: 1, 0xFF8C: 1, 0xFFC6: 1, 0xFF9F: 1})

    def test_fffe_in_scope(self):
        rows = hr.census(_text("    ld hl, $FFFE"), self.SYMS, T)
        self.assertEqual([(c.address, c.ref_count) for c in rows], [(0xFFFE, 1)])

    def test_comment_only_operand_ignored(self):
        # A comment that looks like an access must not be counted.
        rows = hr.census(_text("    ; ldh [$98], a is only a comment"), self.SYMS, T)
        self.assertEqual(rows, [])


# --- Layer 2: synthetic edge cases (must raise) + emit shape ------------------------------------

class WalkRaises(unittest.TestCase):
    def test_gap_arithmetic_mismatch(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text('SECTION "HRAM", HRAM', "hA:: db", "ds $FFFE - $FF05"), P)

    def test_unknown_directive(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text('SECTION "HRAM", HRAM', "hA:: nop"), P)

    def test_content_before_section(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text("hA:: db"), P)

    def test_dangling_label_at_eof(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text('SECTION "HRAM", HRAM', "hA:: db",
                                  "ds $FFFE - $FF81", "hDangling::"), P)

    def test_walk_must_end_at_fffe(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text('SECTION "HRAM", HRAM', "hA:: db", "ds $FFF0 - $FF81"), P)

    def test_overrun_past_fffe(self):
        with self.assertRaises(hr.ParseError):
            hr.parse_layout(_text('SECTION "HRAM", HRAM', "ds $FFFF - $FF80"), P)  # ends $FFFF

    def test_census_unrecognized_ldh_form_raises(self):
        with self.assertRaises(hr.ParseError):
            hr.census(_text("    ldh a, [$FF00 + c]"), {}, T)


class EmitShape(unittest.TestCase):
    def test_fixture_shape(self):
        regions, sections = hr.parse_layout(
            _text('SECTION "HRAM", HRAM', "hA:: ds $A", "ds $FFFE - $FF8A"), P)
        census_rows = [hr.CensusEntry(0xFF98, 14), hr.CensusEntry(0xFFA0, 22)]
        fx = hr.emit_fixture(regions, sections, census_rows, "deadbee")
        self.assertIn("namespace kirpich::fixtures", fx)
        self.assertIn("enum class HramKind", fx)
        self.assertIn("struct HramCensus", fx)
        self.assertIn("std::array<HramCensus, 2> kHramCensus", fx)
        self.assertIn('.name = "hA", .address = 0xFF80, .size = 10', fx)
        self.assertIn(".address = 0xFF98, .refCount = 14", fx)
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
        if candidate and (candidate / "hram.asm").is_file() and (candidate / "tetris.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        hram = self.root / "hram.asm"
        self.regions, self.sections = hr.parse_layout(hram.read_bytes().decode("utf-8"), hram)
        self.by_name = {r.name: r for r in self.regions if r.name and r.kind is not hr.Kind.ALIAS}
        self.symbols = hr.symbol_table(self.regions)
        tetris = self.root / "tetris.asm"
        self.census = hr.census(tetris.read_bytes().decode("utf-8"), self.symbols, tetris)
        self.refs = {c.address: c.ref_count for c in self.census}

    def test_region_totals(self):
        fields = sum(1 for r in self.regions if r.kind is hr.Kind.FIELD)
        aliases = sum(1 for r in self.regions if r.kind is hr.Kind.ALIAS)
        gaps = sum(1 for r in self.regions if r.kind is hr.Kind.GAP)
        self.assertEqual((fields, aliases, gaps), (60, 1, 19))
        self.assertEqual(len(self.regions), 80)

    def test_section_span(self):
        self.assertEqual([(s.name, s.origin, s.end) for s in self.sections],
                         [("HRAM", 0xFF80, 0xFFFE)])

    def test_layout_pins(self):
        pins = {
            "hJoyHeld": (0xFF80, 1),        # first byte
            "hLines": (0xFF9E, 2),          # the dw
            "hDMARoutine": (0xFFB6, 10),    # the ds $A
            "hGameState": (0xFFE1, 1),      # same-line `hGameState:: db`
            "hTopScorePointerHi": (0xFFFB, 1),
            "hTopScorePointerLo": (0xFFFC, 1),
        }
        for name, (addr, size) in pins.items():
            self.assertEqual((self.by_name[name].address, self.by_name[name].size), (addr, size), name)

    def test_ffc_alias_pair(self):
        alias = [r for r in self.regions if r.kind is hr.Kind.ALIAS]
        self.assertEqual(len(alias), 1)
        self.assertEqual(alias[0].name, "hTempPreviewPiece")
        self.assertEqual(alias[0].address, 0xFFFC)
        self.assertEqual(self.by_name["hTopScorePointerLo"].address, 0xFFFC)  # shares the byte

    def test_commented_out_slots_are_gaps(self):
        gap_addrs = {r.address for r in self.regions if r.kind is hr.Kind.GAP}
        for addr in (0xFF94, 0xFF98, 0xFFA0, 0xFFA8, 0xFFAF, 0xFFC6, 0xFFCE):
            self.assertIn(addr, gap_addrs, hex(addr))

    def test_census_totals(self):
        self.assertEqual(len(self.census), 57)
        self.assertEqual(sum(c.ref_count for c in self.census), 244)

    def test_census_plan_pins(self):
        # The pinned census counts.
        self.assertEqual(self.refs[0xFF98], 14)   # pieceLockStage (spot-check)
        self.assertEqual(self.refs[0xFFA0], 22)   # completedRowCount (D5)
        self.assertEqual(self.refs[0xFF9C], 12)   # blinkCounter (D5)
        self.assertEqual(self.refs[0xFFC6], 17)   # coarseCountdown (14 ldh + 3 pointer)
        self.assertEqual(self.refs[0xFF8C], 2)    # the [$86 + 6] expression form (spot-check)
        self.assertEqual(self.refs[0xFFFB], 1)    # the topout-counter raw access
        self.assertEqual(self.refs[0xFFFE], 1)    # the boot HRAM-clear loop pointer

    def test_census_scope_is_hram_state(self):
        self.assertGreaterEqual(min(self.refs), 0xFF80)
        self.assertLessEqual(max(self.refs), 0xFFFE)
        self.assertNotIn(0xFFFF, self.refs)       # rIE is out of scope
        for addr in self.refs:
            self.assertFalse(0xFF00 <= addr <= 0xFF7F, hex(addr))  # no hardware registers


if __name__ == "__main__":
    unittest.main()
