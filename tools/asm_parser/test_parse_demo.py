#!/usr/bin/env python3
"""Unit tests for parse_demo.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - blob reading, record pairing, and the piece-domain check on crafted
                              input, plus a full valid synthetic corpus parsing to the right counts.
  2. Synthetic edge cases   - malformed sources that MUST raise (every structural assert has a raise
                              path: missing/reordered/truncated INCBIN anchors, wrong blob sizes,
                              out-of-domain piece bytes), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris blobs, asserting the record counts, the first and last
                              records of each stream, the piece list, and the button corpus invariant.
                              Skips cleanly when the disassembly is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_demo
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_demo as d  # noqa: E402


# --- Synthetic source builder (the baseline the edge-case tests perturb) ------------------------

# A valid Type A blob: 128 records. held cycles through the four buttons the real demo uses; frames is
# an arbitrary per-record value. Record 0 is a deliberate {0x00, 0x2A} so spot assertions can anchor.
def _valid_type_a() -> bytes:
    allowed = [0x00, 0x01, 0x10, 0x20, 0x80]
    out = bytearray()
    for i in range(d.TYPE_A_SIZE // d.RECORD_SIZE):
        held = 0x00 if i == 0 else allowed[i % len(allowed)]
        frames = 0x2A if i == 0 else (i * 3) & 0xFF
        out += bytes((held, frames))
    return bytes(out)


def _valid_type_b() -> bytes:
    allowed = [0x00, 0x01, 0x10, 0x20, 0x80]
    out = bytearray()
    for i in range(d.TYPE_B_SIZE // d.RECORD_SIZE):
        held = 0x00 if i == 0 else allowed[i % len(allowed)]
        frames = 0x4D if i == 0 else (i * 5) & 0xFF
        out += bytes((held, frames))
    return bytes(out)


def _valid_pieces() -> bytes:
    # 48 spawn-orientation piece specs: multiples of four below 28, cycling through the seven kinds.
    return bytes(((i % d.PIECE_KIND_COUNT) * d.PIECE_ROTATION_STRIDE) for i in range(d.PIECE_SIZE))


def _tetris_asm(*, labels=None, filenames=None, truncate=False, duplicate_first=False) -> str:
    """A minimal tetris.asm carrying the three demo INCBIN pairs, with knobs to perturb them."""
    labels = ["TypeADemoData", "TypeBDemoData", "DemoPieceList"] if labels is None else labels
    filenames = [d.TYPE_A_BIN, d.TYPE_B_BIN, d.PIECE_BIN] if filenames is None else filenames
    lines = ['SECTION "rom", ROM0', "    ret", ""]
    if duplicate_first:
        lines += [f"{labels[0]}::", "    ret", ""]
    pairs = list(zip(labels, filenames))
    if truncate:
        lines += [f"{labels[0]}::"]        # label with no INCBIN following
    else:
        for label, filename in pairs:
            lines += [f"{label}::", f'INCBIN "{filename}"']
    lines += ["", "AfterAll::", "    ret", ""]
    return "\n".join(lines) + "\n"


def _write_source(root: Path, *, type_a=None, type_b=None, pieces=None, asm=None,
                  omit_piece_file=False) -> Path:
    """Materialize a synthetic disassembly root and return it."""
    (root / (d.TYPE_A_BIN)).write_bytes(_valid_type_a() if type_a is None else type_a)
    (root / (d.TYPE_B_BIN)).write_bytes(_valid_type_b() if type_b is None else type_b)
    if not omit_piece_file:
        (root / (d.PIECE_BIN)).write_bytes(_valid_pieces() if pieces is None else pieces)
    (root / "tetris.asm").write_text(_tetris_asm() if asm is None else asm, encoding="utf-8")
    return root


class _SourceCase(unittest.TestCase):
    """Base class giving each test its own temp disassembly root."""

    def _root(self, **kw) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        return _write_source(Path(tmp.name), **kw)


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class BlobHelpers(unittest.TestCase):
    def test_to_records_pairs(self):
        recs = d.to_records([0x00, 0x2A, 0x20, 0x01], "x.bin", Path("x.bin"))
        self.assertEqual(recs, [(0x00, 0x2A), (0x20, 0x01)])

    def test_to_records_odd_raises(self):
        with self.assertRaises(SystemExit):
            d.to_records([0x00, 0x2A, 0x20], "x.bin", Path("x.bin"))

    def test_piece_domain_accepts_valid(self):
        d.assert_piece_domain([0x00, 0x04, 0x18, 0x0C], Path("p.bin"))  # 0, 4, 24, 12

    def test_piece_domain_rejects_non_multiple(self):
        with self.assertRaises(SystemExit):
            d.assert_piece_domain([0x02], Path("p.bin"))

    def test_piece_domain_rejects_over_range(self):
        with self.assertRaises(SystemExit):
            d.assert_piece_domain([0x1C], Path("p.bin"))  # 28 = kind 7, out of range


class ParseValid(_SourceCase):
    def setUp(self):
        self.result = d.parse_demo(self._root())

    def test_counts(self):
        self.assertEqual(len(self.result["type_a"]), 128)
        self.assertEqual(len(self.result["type_b"]), 80)
        self.assertEqual(len(self.result["pieces"]), 48)

    def test_first_record(self):
        self.assertEqual(self.result["type_a"][0], (0x00, 0x2A))
        self.assertEqual(self.result["type_b"][0], (0x00, 0x4D))


# --- Layer 2: INCBIN-anchor edge cases ----------------------------------------------------------

class IncbinAnchorEdgeCases(_SourceCase):
    def test_missing_first_label_raises(self):
        asm = _tetris_asm(labels=["SomethingElse", "TypeBDemoData", "DemoPieceList"])
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(asm=asm))

    def test_wrong_filename_raises(self):
        asm = _tetris_asm(filenames=[d.TYPE_A_BIN, "wrong.bin", d.PIECE_BIN])
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(asm=asm))

    def test_reordered_labels_raises(self):
        asm = _tetris_asm(labels=["TypeADemoData", "DemoPieceList", "TypeBDemoData"])
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(asm=asm))

    def test_truncated_block_raises(self):
        asm = _tetris_asm(truncate=True)
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(asm=asm))

    def test_duplicate_first_label_raises(self):
        asm = _tetris_asm(duplicate_first=True)
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(asm=asm))


# --- Layer 2: blob edge cases -------------------------------------------------------------------

class BlobEdgeCases(_SourceCase):
    def test_wrong_type_a_size_raises(self):
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(type_a=_valid_type_a()[:-2]))

    def test_wrong_type_b_size_raises(self):
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(type_b=_valid_type_b() + b"\x00\x00"))

    def test_wrong_piece_size_raises(self):
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(pieces=_valid_pieces()[:-1]))

    def test_out_of_domain_piece_raises(self):
        bad = bytearray(_valid_pieces())
        bad[5] = 0x1C                                   # 28, past the last valid kind
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(pieces=bytes(bad)))

    def test_non_multiple_piece_raises(self):
        bad = bytearray(_valid_pieces())
        bad[0] = 0x02                                   # rotation != 0
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(pieces=bytes(bad)))

    def test_missing_piece_file_raises(self):
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(omit_piece_file=True))

    def test_unmapped_button_bit_raises(self):
        # A held byte that presses START ($08) - a button with no piece-control action.
        bad = bytearray(_valid_type_a())
        bad[2] = 0x08
        with self.assertRaises(SystemExit):
            d.parse_demo(self._root(type_a=bytes(bad)))


# --- Layer 1b: button -> action mapping ---------------------------------------------------------

class HeldActions(unittest.TestCase):
    P = Path("x.bin")

    def test_empty(self):
        self.assertEqual(d.held_actions(0x00, self.P), [])

    def test_single(self):
        self.assertEqual(d.held_actions(0x20, self.P), ["MoveLeft"])
        self.assertEqual(d.held_actions(0x80, self.P), ["SoftDrop"])
        self.assertEqual(d.held_actions(0x01, self.P), ["RotateClockwise"])

    def test_combination_is_enum_ordered(self):
        # A | RIGHT -> RotateClockwise + MoveRight, emitted in Action enum order (MoveRight first).
        self.assertEqual(d.held_actions(0x11, self.P), ["MoveRight", "RotateClockwise"])

    def test_unmapped_bit_raises(self):
        with self.assertRaises(SystemExit):
            d.held_actions(0x08, self.P)     # START


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(_SourceCase):
    def setUp(self):
        self.result = d.parse_demo(self._root())

    def test_demo_header(self):
        header = d.emit_demo(self.result, "abc1234")
        self.assertIn("struct DemoInputRecord {", header)
        self.assertIn("retropp::ActionSet held;", header)
        self.assertIn("constexpr retropp::ActionSet heldActions(std::initializer_list<Action>", header)
        self.assertIn("inline constexpr std::uint16_t kTypeADemoInputCount = 128;", header)
        self.assertIn("inline constexpr std::uint16_t kTypeBDemoInputCount = 80;", header)
        self.assertIn("inline constexpr std::uint16_t kDemoPieceCount      = 48;", header)
        self.assertIn("kTypeADemoInputs = {{", header)
        # held resolves to named engine actions, not a magic byte. Synthetic record 0 = no input,
        # record 1 = A ($01) -> RotateClockwise.
        self.assertIn("{ .held = heldActions({}), .frames = 0x2A },", header)
        self.assertIn("{ .held = heldActions({Action::RotateClockwise}), .frames = 0x03 },", header)
        self.assertIn("Piece{0x00},", header)
        self.assertIn("#include <retropp/input.h>", header)
        self.assertIn("#include <kirpich/action.h>", header)
        self.assertIn("#include <kirpich/piece.h>", header)
        # No magic held byte, and the engine input system is NOT reinvented port-side.
        self.assertNotIn(".held = 0x", header)
        self.assertNotIn("PadFlag", header)
        self.assertNotIn("JoypadButtons", header)
        self.assertTrue(header.isascii())

    def test_fixture(self):
        fixture = d.emit_fixture(self.result, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("std::array<std::uint8_t, 256> kExpectedTypeADemoBytes", fixture)
        self.assertIn("std::array<std::uint8_t, 160> kExpectedTypeBDemoBytes", fixture)
        self.assertIn("std::array<std::uint8_t, 48> kExpectedDemoPieceListBytes", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        # Independent of the port surface: it does not include or name the header it guards.
        self.assertNotIn('#include "data/demo.h"', fixture)
        self.assertNotIn("DemoInputRecord", fixture)
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


# The bits the demo ever presses: A | RIGHT | LEFT | DOWN. B/SELECT/START/UP never appear.
_ALLOWED_HELD = 0x01 | 0x10 | 0x20 | 0x80


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        self.result = d.parse_demo(self.root)

    def test_type_a_records(self):
        recs = self.result["type_a"]
        self.assertEqual(len(recs), 128)
        self.assertEqual(recs[0], (0x00, 0x2A))
        self.assertEqual(recs[1], (0x20, 0x01))
        self.assertEqual(recs[127], (0x00, 0x00))       # trailing, never reached

    def test_type_b_records(self):
        recs = self.result["type_b"]
        self.assertEqual(len(recs), 80)
        self.assertEqual(recs[0], (0x00, 0x4D))

    def test_piece_list(self):
        pieces = self.result["pieces"]
        self.assertEqual(len(pieces), 48)
        self.assertEqual(pieces[0], 0x10)
        self.assertEqual(pieces[47], 0x08)
        for byte in pieces:
            self.assertEqual(byte % 4, 0)
            self.assertLess(byte, 28)

    def test_button_corpus_invariant(self):
        for stream in ("type_a", "type_b"):
            for held, _frames in self.result[stream]:
                self.assertEqual(held & ~_ALLOWED_HELD, 0,
                                 f"{stream} held byte 0x{held:02X} presses a bit outside A/RIGHT/LEFT/DOWN")

    def test_incbin_anchors(self):
        # Does not raise on the real tetris.asm.
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        d.assert_incbin_anchors(text, self.root / "tetris.asm")


if __name__ == "__main__":
    unittest.main()
