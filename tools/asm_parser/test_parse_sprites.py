#!/usr/bin/env python3
"""Unit tests for parse_sprites.py.

Three layers per the parser test discipline:
  1. Helper units          - signed-offset decode, the tile-stream escape state machine (every
                             $FD/$FE/$FF path and its error paths), the grid grammar, and the
                             SpriteList reader, on crafted input.
  2. Synthetic edge cases  - malformed sprite-section corpora that MUST raise (address-suffix
                             mismatch, a stream with no $FF, a $FF mid-stream, a $FD with no real
                             tile, a bad record shape, an out-of-range offset), plus name-table
                             integrity and the shape of the three emitted artifacts.
  3. End-to-end            - the real ../tetris/, asserting the corpus totals, the 1:1 topology, the
                             boundary targets, the by-value duplicate identities, and hand-traced
                             composed sprites. Skips cleanly when the disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_sprites
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_sprites as ps  # noqa: E402

P = Path("tetris.asm")
SP = Path("sprites.asm")


# --- Synthetic builders --------------------------------------------------------------------------

def _off(v: int) -> str:
    return "$00" if v == 0 else (f"-${-v:02X}" if v < 0 else f"${v:02X}")


def _build_section(records: list[tuple[int, int, int]],
                   tilelists: list[tuple[str, list[int]]]) -> str:
    """An address-consistent sprite-data section: records (4 bytes each) laid out first, then tile
    lists (2 + stream). `records[i]` = (tilelist_index, y_offset, x_offset); `tilelists[i]` =
    (matrix_label, stream_including_terminator)."""
    addr = ps.SPRITE_SECTION_ADDR
    rec_addrs = []
    for _ in records:
        rec_addrs.append(addr)
        addr += 4
    tl_addrs = []
    for _m, stream in tilelists:
        tl_addrs.append(addr)
        addr += 2 + len(stream)

    lines = ['SECTION "Sprite data", ROM0[$2C20]', ""]
    for (ti, oy, ox), ra in zip(records, rec_addrs):
        lines += [f"Sprite_{ra:04X}::", f"    dw SpriteTiles_{tl_addrs[ti]:04X}",
                  f"    db {_off(oy)}, {_off(ox)}", ""]
    for (m, stream), ta in zip(tilelists, tl_addrs):
        lines += [f"SpriteTiles_{ta:04X}::", f"    dw {m}",
                  "    db " + ", ".join(f"${b:02X}" for b in stream), ""]
    return "\n".join(lines) + "\n"


def _grid_bytes(pairs: list[tuple[int, int]]) -> list[int]:
    return [b for pair in pairs for b in pair]


def _notched_pairs() -> list[tuple[int, int]]:
    pairs = [(8 * r, 8 * (c + 1)) for r in range(2) for c in range(2)]
    pairs += [(8 * r, 8 * c) for r in range(2, 8) for c in range(4)]
    return pairs


def _valid_grid_tables() -> list[tuple[str, list[int]]]:
    return [
        ("Matrix_31A9", _grid_bytes([(8 * (i // 4), 8 * (i % 4)) for i in range(16)])),
        ("Matrix_31C9", _grid_bytes([(0, 8 * i) for i in range(8)])),
        ("Matrix_31D9", _grid_bytes([(8 * (i // 2), 8 * (i % 2)) for i in range(14)])),
        ("Matrix_31F5", _grid_bytes(_notched_pairs())),
        ("Matrix_322D", _grid_bytes([(8 * (i // 3), 8 * (i % 3)) for i in range(9)])),
    ]


def _render_grids(tables: list[tuple[str, list[int]]], *, section: bool = True,
                  terminal: bool = True) -> str:
    lines: list[str] = []
    if section:
        lines += [ps.GRID_SECTION_TEXT, ""]
    for label, flat in tables:
        lines += [f"{label}::", "    db " + ", ".join(f"${b:02X}" for b in flat)]
    if terminal:
        lines += ["GameplayTiles::", 'INCBIN "gfx/configandgameplay.2bpp"']
    return "\n".join(lines) + "\n"


def _render_spritelist(rows: list[str]) -> str:
    return "\n".join(["SpriteList::", *rows, "", 'SECTION "next", ROM0[$4000]']) + "\n"


# --- Layer 1: byte + operand helpers ------------------------------------------------------------

class SignedDecode(unittest.TestCase):
    def test_signed_of_byte(self):
        self.assertEqual(ps._signed(0x00), 0)
        self.assertEqual(ps._signed(0xEF), -0x11)
        self.assertEqual(ps._signed(0xF0), -0x10)
        self.assertEqual(ps._signed(0x7F), 127)
        self.assertEqual(ps._signed(0x80), -128)

    def test_parse_signed_byte(self):
        self.assertEqual(ps._parse_signed_byte("-$11", P, 1), 0xEF)
        self.assertEqual(ps._parse_signed_byte("$00", P, 1), 0x00)
        self.assertEqual(ps._parse_signed_byte("-$28", P, 1), 0xD8)

    def test_parse_signed_byte_rejects_junk(self):
        with self.assertRaises(SystemExit):
            ps._parse_signed_byte("nope", P, 1)

    def test_parse_stream_byte(self):
        self.assertEqual(ps._parse_stream_byte("$FE", P, 1), 0xFE)
        with self.assertRaises(SystemExit):
            ps._parse_stream_byte("$FFF", P, 1)

    def test_split_operands_drops_trailing_comma_and_comment(self):
        self.assertEqual(ps._split_operands("$FE, $FE, $84, "), ["$FE", "$FE", "$84"])
        self.assertEqual(ps._split_operands("$84, $FF ; comment"), ["$84", "$FF"])


# --- Layer 1: the escape state machine ----------------------------------------------------------

GRID4 = [(0x00, 0x00), (0x00, 0x08), (0x08, 0x00), (0x08, 0x08)]


class Compose(unittest.TestCase):
    def test_plain_tile_consumes_one_pair(self):
        parts, real, fd, fe = ps._compose([0x10, 0xFF], GRID4, "T", P)
        self.assertEqual(parts, [ps.Part(0x00, 0x00, False, 0x10)])
        self.assertEqual((real, fd, fe), (1, 0, 0))

    def test_fe_skips_a_pair_and_emits_nothing(self):
        parts, real, fd, fe = ps._compose([0xFE, 0x10, 0xFF], GRID4, "T", P)
        self.assertEqual(parts, [ps.Part(0x00, 0x08, False, 0x10)])
        self.assertEqual((real, fd, fe), (1, 0, 1))

    def test_fd_flips_next_tile_and_consumes_one_pair(self):
        parts, real, fd, fe = ps._compose([0xFD, 0x10, 0xFF], GRID4, "T", P)
        self.assertEqual(parts, [ps.Part(0x00, 0x00, True, 0x10)])
        self.assertEqual((real, fd, fe), (1, 1, 0))

    def test_full_grid_walk(self):
        parts, real, _fd, _fe = ps._compose([0x10, 0x11, 0x12, 0x13, 0xFF], GRID4, "T", P)
        self.assertEqual([p.tile for p in parts], [0x10, 0x11, 0x12, 0x13])
        self.assertEqual([(p.y, p.x) for p in parts], GRID4)
        self.assertEqual(real, 4)

    def test_over_consumption_raises(self):
        with self.assertRaises(SystemExit):
            ps._compose([0x10, 0x11, 0x12, 0x13, 0x14, 0xFF], GRID4, "T", P)

    def test_fe_over_consumption_raises(self):
        with self.assertRaises(SystemExit):
            ps._compose([0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFF], GRID4, "T", P)

    def test_fd_followed_by_escape_raises(self):
        with self.assertRaises(SystemExit):
            ps._compose([0x10, 0xFD, 0xFF], GRID4, "T", P)

    def test_fd_at_end_raises(self):
        with self.assertRaises(SystemExit):
            ps._compose([0x10, 0xFD], GRID4, "T", P)


# --- Layer 1: grid grammar (carried from the retired sprite-grids parser) -----------------------

class Grids(unittest.TestCase):
    def test_valid_grids_parse(self):
        grids = ps._parse_grids(_render_grids(_valid_grid_tables()), P)
        self.assertEqual([len(pairs) for _l, _a, pairs in grids], [16, 8, 14, 28, 9])
        self.assertEqual(grids[3][2][0], (0x00, 0x08))  # the notch: 31F5 row 0 starts at x=$08

    def test_missing_section_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(_valid_grid_tables(), section=False), P)

    def test_wrong_order_raises(self):
        tables = _valid_grid_tables()
        tables[1], tables[2] = tables[2], tables[1]
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(tables), P)

    def test_short_table_raises(self):
        tables = _valid_grid_tables()
        tables[0] = ("Matrix_31A9", tables[0][1][:-2])
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(tables), P)

    def test_non_8_aligned_raises(self):
        tables = _valid_grid_tables()
        b = list(tables[4][1])
        b[0] = 0x04
        tables[4] = ("Matrix_322D", b)
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(tables), P)

    def test_over_max_raises(self):
        tables = _valid_grid_tables()
        b = list(tables[4][1])
        b[0] = 0x40
        tables[4] = ("Matrix_322D", b)
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(tables), P)

    def test_missing_terminal_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_grids(_render_grids(_valid_grid_tables(), terminal=False), P)


# --- Layer 1: SpriteList reader -----------------------------------------------------------------

class SpriteListReader(unittest.TestCase):
    def test_reads_words_across_comment_lines(self):
        text = _render_spritelist([
            "    dw $2C20, $2C24, $2C28, $2C2C ; 00 L",
            "    dw $30C7 ; 2C Buran",
            "    dw $2CCC ; 2D More jumping Mario",
        ])
        self.assertEqual(ps._parse_spritelist(text, P), [0x2C20, 0x2C24, 0x2C28, 0x2C2C, 0x30C7, 0x2CCC])

    def test_missing_label_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_spritelist("NotHere::\n    dw $2C20\n", P)

    def test_duplicate_label_raises(self):
        text = "SpriteList::\n    dw $2C20\nSpriteList::\n    dw $2C24\n"
        with self.assertRaises(SystemExit):
            ps._parse_spritelist(text, P)

    def test_non_hex_word_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_spritelist(_render_spritelist(["    dw Label"]), P)


# --- Layer 2: sprite-section grammar ------------------------------------------------------------

VALID_RECORDS = [(0, -0x11, -0x10), (1, 0x00, 0x00)]
VALID_TILELISTS = [
    ("Matrix_31A9", [0xFE, 0x84, 0xFD, 0x84, 0xFF]),
    ("Matrix_31C9", [0x00, 0xFF]),
]


class SectionGrammar(unittest.TestCase):
    def test_valid_section_parses(self):
        records, tile_lists = ps._parse_sprite_section(
            _build_section(VALID_RECORDS, VALID_TILELISTS), SP)
        self.assertEqual(len(records), 2)
        self.assertEqual(len(tile_lists), 2)
        self.assertEqual(records[0].address, 0x2C20)
        self.assertEqual(records[0].tiles_label, "SpriteTiles_2C28")  # after 2 records (8 bytes)
        self.assertEqual((records[0].offset_y_byte, records[0].offset_x_byte), (0xEF, 0xF0))
        self.assertEqual(tile_lists[0].grid_label, "Matrix_31A9")
        self.assertEqual(tile_lists[0].stream, [0xFE, 0x84, 0xFD, 0x84, 0xFF])

    def test_missing_section_raises(self):
        text = _build_section(VALID_RECORDS, VALID_TILELISTS)
        text = text.replace('SECTION "Sprite data", ROM0[$2C20]', "")
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_address_suffix_mismatch_raises(self):
        text = _build_section(VALID_RECORDS, VALID_TILELISTS).replace("Sprite_2C20::", "Sprite_2C99::")
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_stream_without_terminator_raises(self):
        text = _build_section(VALID_RECORDS, [("Matrix_31A9", [0x84]), ("Matrix_31C9", [0x00, 0xFF])])
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_terminator_before_end_raises(self):
        text = _build_section(VALID_RECORDS,
                              [("Matrix_31A9", [0x84, 0xFF, 0x84, 0xFF]), ("Matrix_31C9", [0x00, 0xFF])])
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_fd_without_real_tile_raises(self):
        text = _build_section(VALID_RECORDS,
                              [("Matrix_31A9", [0x84, 0xFD, 0xFF]), ("Matrix_31C9", [0x00, 0xFF])])
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_offset_out_of_range_raises(self):
        text = _build_section([(0, -0x32, 0x00), (1, 0, 0)], VALID_TILELISTS)  # -50 < -40
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_record_dw_not_spritetiles_raises(self):
        text = ('SECTION "Sprite data", ROM0[$2C20]\n\n'
                "Sprite_2C20::\n    dw Matrix_31A9\n    db -$11, -$10\n")
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_tiles_dw_not_matrix_raises(self):
        text = ('SECTION "Sprite data", ROM0[$2C20]\n\n'
                "SpriteTiles_2C20::\n    dw SpriteTiles_9999\n    db $00, $FF\n")
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_wrong_offset_count_raises(self):
        text = ('SECTION "Sprite data", ROM0[$2C20]\n\n'
                "Sprite_2C20::\n    dw SpriteTiles_2C24\n    db -$11\n"
                "SpriteTiles_2C24::\n    dw Matrix_31A9\n    db $00, $FF\n")
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)

    def test_unknown_label_raises(self):
        text = ('SECTION "Sprite data", ROM0[$2C20]\n\nWhat_2C20::\n    dw SpriteTiles_2C24\n')
        with self.assertRaises(SystemExit):
            ps._parse_sprite_section(text, SP)


# --- Layer 2b: name-table integrity -------------------------------------------------------------

class NameTable(unittest.TestCase):
    def test_ninety_four_unique_names(self):
        self.assertEqual(len(ps.SPRITE_NAMES), ps.SPRITE_COUNT)
        self.assertEqual(len(set(ps.SPRITE_NAMES)), ps.SPRITE_COUNT)

    def test_groups_flatten_to_names(self):
        flat = [n for _c, names in ps.SPRITE_GROUPS for n in names]
        self.assertEqual(flat, ps.SPRITE_NAMES)

    def test_rocket_names_are_the_scoring_contract(self):
        self.assertEqual(ps.SPRITE_NAMES[0x58], "ROCKET_L")
        self.assertEqual(ps.SPRITE_NAMES[0x59], "ROCKET_M")
        self.assertEqual(ps.SPRITE_NAMES[0x5A], "ROCKET_S")

    def test_alias_indices_are_alt_names_in_range(self):
        for idx in ps.EXPECTED_DUP_INDICES:
            self.assertLess(idx, ps.SPRITE_COUNT)
            self.assertTrue(ps.SPRITE_NAMES[idx].endswith("_ALT"), ps.SPRITE_NAMES[idx])


# --- Layer 2c: enum emit shape (no parsed data needed) ------------------------------------------

class EnumEmit(unittest.TestCase):
    def test_enum_shape(self):
        enum = ps.emit_enum("abc1234")
        self.assertIn("enum class SpriteId : std::uint8_t", enum)
        self.assertIn("L_0 = 0x00,", enum)
        self.assertIn("ROCKET_EXHAUST_2 = 0x5D,", enum)
        self.assertIn("VIOLINIST_1_ALT = 0x43,", enum)
        self.assertEqual(sum(1 for line in enum.splitlines() if " = 0x" in line), ps.SPRITE_COUNT)
        self.assertTrue(enum.isascii())


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
        self.tetris = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        self.sprites = (self.root / "sprites.asm").read_bytes().decode("utf-8")
        self.data = ps.parse_sprites(self.tetris, self.sprites,
                                     self.root / "tetris.asm", self.root / "sprites.asm")

    def test_corpus_scale(self):
        self.assertEqual(len(self.data.sprites), 94)
        self.assertEqual(len(self.data.records), 90)
        self.assertEqual(len(self.data.tile_lists), 90)
        self.assertEqual([len(pairs) for _l, _a, pairs in self.data.grids], [16, 8, 14, 28, 9])

    def test_boundary_targets(self):
        self.assertEqual(self.data.sprites[0x00].target_address, 0x2C20)
        self.assertEqual(self.data.sprites[0x5D].target_address, 0x3106)
        self.assertEqual(self.data.sprites[0x1C].target_address, 0x2C90)
        self.assertEqual(self.data.sprites[0x1F].target_address, 0x2C9C)

    def test_composed_l_0(self):
        s = self.data.sprites[0x00]
        self.assertEqual(s.name, "L_0")
        self.assertEqual((s.offset_y, s.offset_x), (-0x11, -0x10))
        self.assertEqual(s.parts, [
            ps.Part(0x10, 0x00, False, 0x84), ps.Part(0x10, 0x08, False, 0x84),
            ps.Part(0x10, 0x10, False, 0x84), ps.Part(0x18, 0x00, False, 0x84)])

    def test_composed_digit_0(self):
        s = self.data.sprites[0x20]
        self.assertEqual(s.name, "DIGIT_0")
        self.assertEqual((s.offset_y, s.offset_x), (0, 0))
        self.assertEqual(s.parts, [ps.Part(0x00, 0x00, False, 0x00)])

    def test_composed_buran_notched_head(self):
        s = self.data.sprites[0x2C]
        self.assertEqual(s.name, "BURAN")
        self.assertEqual((s.offset_y, s.offset_x), (-0x20, -0x10))
        self.assertEqual(len(s.parts), 28)
        self.assertEqual(s.parts[0], ps.Part(0x00, 0x08, False, 0xC0))

    def test_composed_crying_large_mario_xflip(self):
        s = self.data.sprites[0x2E]
        self.assertEqual(s.name, "CRYING_LARGE_MARIO_1")
        self.assertEqual(s.parts[2], ps.Part(0x00, 0x10, True, 0x05))

    def test_composed_rocket_s_alternating_flips(self):
        s = self.data.sprites[0x5A]
        self.assertEqual(s.name, "ROCKET_S")
        self.assertEqual(s.parts[0:2], [ps.Part(0x00, 0x00, False, 0xA8), ps.Part(0x00, 0x08, True, 0xA8)])

    def test_aliases_equal_by_value(self):
        d = self.data.sprites
        for alias, canon in [(0x2D, 0x2B), (0x42, 0x3F), (0x43, 0x44), (0x5B, 0x5A)]:
            self.assertNotEqual(d[alias].name, d[canon].name)
            self.assertEqual(d[alias].parts, d[canon].parts)
            self.assertEqual((d[alias].offset_y, d[alias].offset_x),
                             (d[canon].offset_y, d[canon].offset_x))

    def test_emit_inc_shape(self):
        inc = ps.emit_inc(self.data, "abc1234")
        self.assertIn("inline constexpr std::array<Sprite, 94> kSprites", inc)
        self.assertIn("{ .id = SpriteId::L_0, .offset_y = -17, .offset_x = -16, .parts = {", inc)
        self.assertIn("{ .y = 16, .x = 0, .xflip = false, .tile = 0x84 },", inc)  # coords decimal, tile hex
        self.assertNotIn("namespace kirpich {", inc)  # included at namespace scope
        self.assertTrue(inc.isascii())

    def test_emit_fixture_is_independent_raw(self):
        fixture = ps.emit_fixture(self.data, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("kExpectedSpriteListTargets", fixture)
        self.assertIn("kExpectedSpriteRecordRows", fixture)
        self.assertIn("kExpectedSpriteStreamBytes", fixture)
        self.assertIn("kExpectedGridPairBytes", fixture)
        self.assertIn("0x2C20, 0x2C24", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        # Independent of the surface it guards: it neither includes the port header nor uses its
        # types (the composed Sprite/SpritePart or the BoundedVec container).
        self.assertNotIn('#include "data/sprites.h"', fixture)
        self.assertNotIn("BoundedVec", fixture)
        self.assertNotIn("SpritePart", fixture)
        self.assertNotIn("std::array<Sprite,", fixture)
        self.assertTrue(fixture.isascii())


if __name__ == "__main__":
    unittest.main()
