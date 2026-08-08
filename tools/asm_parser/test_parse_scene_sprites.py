#!/usr/bin/env python3
"""Unit tests for parse_scene_sprites.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the object decoder and block collector on crafted input, plus a full
                              valid synthetic corpus parsing to the right tables/objects.
  2. Synthetic edge cases   - malformed corpora that MUST raise (every structural assert has a raise
                              path), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris/tetris.asm, asserting the 13 tables, the 35 objects,
                              and boundary pins. Skips cleanly when the disassembly is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_scene_sprites
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_scene_sprites as ss  # noqa: E402

P = Path("tetris.asm")


# --- Synthetic corpus builder (the baseline the edge-case tests perturb) ------------------------

def _record(b0: int, b1: int, b2: int, b3: int, b4: int, b5: int) -> str:
    return "    db " + ", ".join(f"${b:02X}" for b in (b0, b1, b2, b3, b4, b5))


def _shape_a_block(label: str, count: int) -> list[str]:
    """`count` plain visible records with distinct coords and byte-3 = 0x1C."""
    out = [f"{label}::"]
    for i in range(count):
        out.append(_record(0x00, 0x60 + i, 0x40 + i, 0x1C, 0x00, 0x00))
    return out


def _shape_b_block(label: str) -> list[str]:
    return [f"{label}::",
            "    db $00, $18, $3F, $00, $80, $00, $00, $FF"]


def _corpus(overrides: dict[str, list[str]] | None = None) -> str:
    """A full valid synthetic source covering all 13 tables. `overrides` replaces a table's lines."""
    overrides = overrides or {}
    lines: list[str] = ["SECTION \"synthetic\", ROM0[$2600]", ""]
    for label, _accessor, count in ss.SHAPE_A_TABLES:
        lines += overrides.get(label, _shape_a_block(label, count))
        lines.append("")
    for label, _accessor in ss.SHAPE_B_TABLES:
        lines += overrides.get(label, _shape_b_block(label))
        lines.append("")
    lines += ["EndMarker::", "    ret"]
    return "\n".join(lines)


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class Decode(unittest.TestCase):
    def test_plain_visible_object(self):
        obj = ss._decode_object([0x00, 0x60, 0x72, 0x2A, 0x80, 0x20], "ctx", P)
        self.assertFalse(obj.hidden)
        self.assertEqual((obj.y, obj.x), (0x60, 0x72))
        self.assertEqual(obj.sprite_index, 0x2A)
        self.assertTrue(obj.behind_bg)
        self.assertTrue(obj.xflip)

    def test_hidden_and_clear_attributes(self):
        obj = ss._decode_object([0x80, 0x3F, 0x40, 0x44, 0x00, 0x00], "ctx", P)
        self.assertTrue(obj.hidden)
        self.assertFalse(obj.behind_bg)
        self.assertFalse(obj.xflip)

    def test_sprite_name_via_import(self):
        # Byte 3 resolves through the imported SpriteId name table.
        self.assertEqual(ss._decode_object([0, 0, 0, 0x00, 0, 0], "c", P).sprite_name, "L_0")
        self.assertEqual(ss._decode_object([0, 0, 0, 0x58, 0, 0], "c", P).sprite_name, "ROCKET_L")

    def test_enable_byte_must_be_draw_or_hidden(self):
        with self.assertRaises(ss.ParseError):
            ss._decode_object([0x01, 0, 0, 0x00, 0, 0], "c", P)

    def test_sprite_index_out_of_range(self):
        with self.assertRaises(ss.ParseError):
            ss._decode_object([0, 0, 0, 0x5E, 0, 0], "c", P)  # past 0x5D

    def test_stray_priority_bit(self):
        with self.assertRaises(ss.ParseError):
            ss._decode_object([0, 0, 0, 0x00, 0x40, 0], "c", P)  # bit outside $80

    def test_stray_flip_bit(self):
        with self.assertRaises(ss.ParseError):
            ss._decode_object([0, 0, 0, 0x00, 0, 0x40], "c", P)  # bit outside $20


class ValidParse(unittest.TestCase):
    def setUp(self):
        self.tables = ss.parse_scene_sprites(_corpus(), P)

    def test_table_and_object_counts(self):
        self.assertEqual(len(self.tables), ss.TABLE_COUNT)
        self.assertEqual(sum(len(t.objects) for t in self.tables), ss.RECORD_COUNT)

    def test_per_table_counts_and_shapes(self):
        by_label = {t.label: t for t in self.tables}
        for label, _accessor, count in ss.SHAPE_A_TABLES:
            self.assertEqual(len(by_label[label].objects), count, label)
            self.assertEqual(by_label[label].shape, "A", label)
        for label, _accessor in ss.SHAPE_B_TABLES:
            self.assertEqual(len(by_label[label].objects), 1, label)
            self.assertEqual(by_label[label].shape, "B", label)

    def test_shape_b_placeholder_and_terminator(self):
        active = next(t for t in self.tables if t.label == "ActivePieceSprite")
        self.assertEqual(active.raw_bytes[-1], ss.TERMINATOR)
        self.assertEqual(active.objects[0].sprite_index, ss.PLACEHOLDER_SPRITE)


# --- Layer 2: synthetic edge cases + emit shape -------------------------------------------------

class EdgeCases(unittest.TestCase):
    def _expect_raise(self, overrides: dict[str, list[str]]):
        with self.assertRaises(ss.ParseError):
            ss.parse_scene_sprites(_corpus(overrides), P)

    def test_wrong_record_count(self):
        self._expect_raise({"MarioVictorySprites": _shape_a_block("MarioVictorySprites", 2)})

    def test_missing_label(self):
        text = _corpus().replace("DancerSprites::", "DancerSpritesTypo::")
        with self.assertRaises(ss.ParseError):
            ss.parse_scene_sprites(text, P)

    def test_bad_sprite_index_in_block(self):
        self._expect_raise({"Data_26DB": ["Data_26DB::", _record(0, 0x40, 0x34, 0x5E, 0, 0)]})

    def test_stray_attribute_bit_in_block(self):
        self._expect_raise({"Data_26DB": ["Data_26DB::", _record(0, 0x40, 0x34, 0x1C, 0x40, 0)]})

    def test_shape_b_missing_terminator(self):
        self._expect_raise(
            {"ActivePieceSprite": ["ActivePieceSprite::", "    db $00, $18, $3F, $00, $80, $00, $00, $00"]})

    def test_shape_b_nonzero_byte6(self):
        self._expect_raise(
            {"PreviewPieceSprite": ["PreviewPieceSprite::",
                                    "    db $00, $80, $8F, $00, $80, $00, $10, $FF"]})

    def test_shape_b_nonplaceholder_sprite(self):
        self._expect_raise(
            {"ActivePieceSprite": ["ActivePieceSprite::", "    db $00, $18, $3F, $05, $80, $00, $00, $FF"]})

    def test_emit_inc_shape(self):
        tables = ss.parse_scene_sprites(_corpus(), P)
        inc = ss.emit_inc(tables, "deadbee")
        self.assertIn("std::array<SceneSprite, 10> kDancerSprites{{", inc)
        # Single-object rows must open their brace-init (regression guard: the emitter once dropped
        # the `{`, producing `kActivePieceSprite .hidden` that would not compile).
        self.assertIn("inline constexpr SceneSprite kActivePieceSprite{ .hidden", inc)
        self.assertIn("inline constexpr SceneSprite kPreviewPieceSprite{ .hidden", inc)
        self.assertIn(".sprite = SpriteId::", inc)
        self.assertTrue(inc.isascii())

    def test_emit_fixture_shape(self):
        tables = ss.parse_scene_sprites(_corpus(), P)
        fx = ss.emit_fixture(tables, "deadbee")
        self.assertIn(f"std::array<SceneTableRow, {ss.TABLE_COUNT}>", fx)
        self.assertIn("kExpectedSceneSpriteBytes", fx)
        self.assertIn(".bytes_per_record = 8", fx)   # shape B rows
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
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        self.tables = ss.parse_scene_sprites(text, self.root / "tetris.asm")
        self.by_label = {t.label: t for t in self.tables}

    def test_corpus_totals(self):
        self.assertEqual(len(self.tables), 13)
        self.assertEqual(sum(len(t.objects) for t in self.tables), 35)

    def test_dancer_count_and_hidden(self):
        dancer = self.by_label["DancerSprites"]
        self.assertEqual(len(dancer.objects), 10)
        self.assertTrue(all(o.hidden for o in dancer.objects))

    def test_mario_victory_pins(self):
        recs = self.by_label["MarioVictorySprites"].objects
        self.assertEqual(recs[0].sprite_name, "JUMPING_LARGE_MARIO_1")
        self.assertFalse(recs[0].hidden)
        self.assertTrue(recs[0].behind_bg)
        self.assertFalse(recs[0].xflip)
        self.assertTrue(recs[1].xflip)           # mirrored second of the pair
        self.assertEqual(recs[2].sprite_name, "CRYING_SMALL_LUIGI_1")

    def test_launch_pins(self):
        buran = self.by_label["BuranLaunchSprites"].objects
        self.assertEqual(buran[0].sprite_name, "BURAN")
        self.assertEqual([o.sprite_name for o in buran[1:]], ["ROCKET_SMOKE_1", "ROCKET_SMOKE_1"])
        rocket = self.by_label["RocketLaunchSprites"].objects
        self.assertEqual(rocket[0].sprite_name, "ROCKET_L")

    def test_piece_templates(self):
        active = self.by_label["ActivePieceSprite"].objects[0]
        preview = self.by_label["PreviewPieceSprite"].objects[0]
        for o in (active, preview):
            self.assertEqual(o.sprite_index, 0x00)
            self.assertTrue(o.behind_bg)
            self.assertFalse(o.hidden)
        self.assertEqual((active.y, active.x), (0x18, 0x3F))
        self.assertEqual((preview.y, preview.x), (0x80, 0x8F))


if __name__ == "__main__":
    unittest.main()
