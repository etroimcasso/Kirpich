#!/usr/bin/env python3
"""Unit tests for parse_wram.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the expression evaluator, the ds self-check, the directive classifier,
                              and a full walk of a crafted section (fields, alias grouping, the
                              data-slot field extension, and a gap that must not fold into the field
                              before it).
  2. Synthetic edge cases   - malformed input that MUST raise (every structural assert has a raise
                              path): gap-arithmetic mismatch, section-origin mismatch, unknown
                              directive, content before a section, a dangling label at EOF - plus the
                              shape of the emitted fixture.
  3. End-to-end             - the real ../tetris/wram.asm, asserting 36 fields / 1 alias / 14 gaps,
                              the two section spans, and the boundary + stride pins. Skips when absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_wram
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_wram as wr  # noqa: E402

P = Path("wram.asm")


def _text(*rows: str) -> str:
    return "\n".join(rows) + "\n"


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class EvalExpr(unittest.TestCase):
    def test_hex_and_decimal(self):
        self.assertEqual(wr._eval_expr("$A0", P, 1), 160)
        self.assertEqual(wr._eval_expr("256", P, 1), 256)

    def test_product_with_parens(self):
        self.assertEqual(wr._eval_expr("10*6*3*(6+3)", P, 1), 1620)   # wTypeBTopScores
        self.assertEqual(wr._eval_expr("10*3*(6+3)", P, 1), 270)      # wTypeATopScores

    def test_subtraction(self):
        self.assertEqual(wr._eval_expr("$DFE0 - $DF80", P, 1), 96)
        self.assertEqual(wr._eval_expr("$E - $B", P, 1), 3)

    def test_garbage_raises(self):
        with self.assertRaises(wr.ParseError):
            wr._eval_expr("$A0 +", P, 1)
        with self.assertRaises(wr.ParseError):
            wr._eval_expr("nonsense", P, 1)


class DsSize(unittest.TestCase):
    def test_absolute_gap_self_check_passes(self):
        # `ds $D000 - $C001` starting exactly at $C001 reserves $FFF bytes.
        self.assertEqual(wr._ds_size("$D000 - $C001", 0xC001, P, 1), 0xFFF)

    def test_absolute_gap_self_check_fails(self):
        with self.assertRaises(wr.ParseError):
            wr._ds_size("$D000 - $C005", 0xC001, P, 1)   # $C005 != running $C001

    def test_relative_subtraction_skips_address_check(self):
        # $E - $B are low-byte offsets (< $C000): value only, no address assertion.
        self.assertEqual(wr._ds_size("$E - $B", 0xDF7B, P, 1), 3)

    def test_plain_count(self):
        self.assertEqual(wr._ds_size("4", 0xC000, P, 1), 4)

    def test_zero_raises(self):
        with self.assertRaises(wr.ParseError):
            wr._ds_size("$C000 - $C000", 0xC000, P, 1)


class DirectiveBytes(unittest.TestCase):
    def test_slots_and_reservation(self):
        self.assertEqual(wr._directive_bytes("db", 0xC000, P, 1), (1, False))
        self.assertEqual(wr._directive_bytes("dw", 0xC000, P, 1), (2, False))
        self.assertEqual(wr._directive_bytes("ds 4", 0xC000, P, 1), (4, True))

    def test_unknown_raises(self):
        with self.assertRaises(wr.ParseError):
            wr._directive_bytes("nop", 0xC000, P, 1)


class WalkValid(unittest.TestCase):
    def test_field_alias_gap_and_extension(self):
        text = _text(
            'SECTION "T", WRAM0',
            "wBuf:: ds $A0",       # labelled ds field, 160 bytes
            "wList::",             # a field built from several bare slots
            "    dw",
            "    dw",
            "    db",              # -> 5 bytes total
            "wStats::",            # two labels share one address (alias)
            "wSingles::",
            "    db",
            "ds 4",                # anonymous pad - must NOT fold into wSingles
            "wDoubles::",
            "    db",
        )
        regions, sections = wr.parse_wram(text, P)
        by_name = {r.name: r for r in regions if r.name}

        self.assertEqual((by_name["wBuf"].address, by_name["wBuf"].size), (0xC000, 160))
        self.assertEqual((by_name["wList"].address, by_name["wList"].size), (0xC0A0, 5))
        self.assertEqual(by_name["wStats"].kind, wr.Kind.FIELD)
        self.assertEqual(by_name["wSingles"].kind, wr.Kind.ALIAS)
        self.assertEqual(by_name["wStats"].address, by_name["wSingles"].address)
        self.assertEqual(by_name["wSingles"].size, 1)                     # not folded with the ds 4
        self.assertEqual(by_name["wDoubles"].address - by_name["wSingles"].address, 5)  # stride 5

        gaps = [r for r in regions if r.kind is wr.Kind.GAP]
        self.assertEqual(len(gaps), 1)
        self.assertEqual((gaps[0].address, gaps[0].size), (0xC0A6, 4))
        self.assertEqual(len(sections), 1)
        self.assertEqual((sections[0].origin, sections[0].end), (0xC000, 0xC0AB))

    def test_section_origin_self_check_passes(self):
        text = _text(
            'SECTION "A", WRAM0',
            "wA:: ds $10",
            'SECTION "B", WRAM0[$C010]',
            "wB:: db",
        )
        _regions, sections = wr.parse_wram(text, P)
        self.assertEqual([s.name for s in sections], ["A", "B"])
        self.assertEqual(sections[1].origin, 0xC010)


# --- Layer 2: synthetic edge cases (must raise) + emit shape ------------------------------------

class WalkRaises(unittest.TestCase):
    def test_gap_arithmetic_mismatch(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text('SECTION "T", WRAM0', "wA:: db", "ds $D000 - $C005"), P)

    def test_section_origin_mismatch(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text('SECTION "A", WRAM0', "wA:: ds $10",
                                'SECTION "B", WRAM0[$C020]', "wB:: db"), P)

    def test_unknown_directive(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text('SECTION "T", WRAM0', "wA:: nop"), P)

    def test_content_before_section(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text("wA:: db"), P)

    def test_dangling_label_at_eof(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text('SECTION "T", WRAM0', "wA:: db", "wDangling::"), P)

    def test_overrun_past_wram(self):
        with self.assertRaises(wr.ParseError):
            wr.parse_wram(_text('SECTION "T", WRAM0[$DFFF]', "wA:: dw"), P)  # 2 bytes past $DFFF


class EmitShape(unittest.TestCase):
    def test_fixture_shape(self):
        regions, sections = wr.parse_wram(
            _text('SECTION "T", WRAM0', "wA:: ds $A0", "wB:: db"), P)
        fx = wr.emit_fixture(regions, sections, "deadbee")
        self.assertIn("namespace kirpich::fixtures", fx)
        self.assertIn("enum class WramKind", fx)
        self.assertIn("std::array<WramLabel, 2> kWramLabels", fx)
        self.assertIn("std::array<WramSection, 1> kWramSections", fx)
        self.assertIn('.name = "wA", .address = 0xC000, .size = 160', fx)
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
        if candidate and (candidate / "wram.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        wram = self.root / "wram.asm"
        self.regions, self.sections = wr.parse_wram(wram.read_bytes().decode("utf-8"), wram)
        self.by_name = {r.name: r for r in self.regions if r.name and r.kind is not wr.Kind.ALIAS}

    def test_region_totals(self):
        fields = sum(1 for r in self.regions if r.kind is wr.Kind.FIELD)
        aliases = sum(1 for r in self.regions if r.kind is wr.Kind.ALIAS)
        gaps = sum(1 for r in self.regions if r.kind is wr.Kind.GAP)
        self.assertEqual((fields, aliases, gaps), (36, 1, 14))
        self.assertEqual(len(self.regions), 51)

    def test_section_spans(self):
        self.assertEqual([(s.name, s.origin, s.end) for s in self.sections],
                         [("WRAM", 0xC000, 0xDF70), ("Audio RAM", 0xDF70, 0xDFFA)])

    def test_c000_block_pins(self):
        pins = {
            "wOAMBuffer": (0xC000, 160),
            "wScore": (0xC0A0, 3),
            "wLineClearsList": (0xC0A3, 9),        # dw x4 + db terminator
            "wSoftDropPoints": (0xC0C0, 2),
            "wSoftDropPointsBCD": (0xC0C2, 3),
            "wScoreboardState": (0xC0C5, 1),
            "wHidePreviewPiece": (0xC0DE, 1),
            "wPieceList": (0xC300, 256),           # not the trailing 3 KB gap
        }
        for name, (addr, size) in pins.items():
            self.assertEqual((self.by_name[name].address, self.by_name[name].size), (addr, size), name)

    def test_stats_stride_five(self):
        counts = ["wSinglesCount", "wDoublesCount", "wTriplesCount", "wTetrisCount"]
        # wSinglesCount is the alias of wLineClearStats; look it up including aliases.
        by_all = {r.name: r for r in self.regions if r.name}
        addrs = [by_all[c].address for c in counts]
        self.assertEqual(addrs, [0xC0AC, 0xC0B1, 0xC0B6, 0xC0BB])
        self.assertTrue(all(by_all[c].size == 1 for c in counts))
        self.assertEqual(by_all["wLineClearStats"].address, 0xC0AC)     # alias head

    def test_audio_ram_pins(self):
        self.assertEqual(self.by_name["wMusicCurrentChannel"].address, 0xDF70)
        self.assertEqual((self.by_name["wCurrentNoiseSFXID"].address,
                          self.by_name["wCurrentNoiseSFXID"].size), (0xDFF9, 1))

    def test_top_scores_pins(self):
        self.assertEqual((self.by_name["wTypeBTopScores"].address,
                          self.by_name["wTypeBTopScores"].size), (0xD000, 1620))
        self.assertEqual((self.by_name["wTypeATopScores"].address,
                          self.by_name["wTypeATopScores"].size), (0xD654, 270))


if __name__ == "__main__":
    unittest.main()
