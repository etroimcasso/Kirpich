#!/usr/bin/env python3
"""Unit tests for parse_tilemaps.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the greedy charmap encoder, the db operand tokenizer, the row-comment
                              renderer, and a full valid synthetic corpus parsing to the right grids.
  2. Synthetic edge cases   - malformed corpora that MUST raise (every structural assert and consumer
                              anchor has a raise path).
  3. End-to-end             - the real ../tetris/tetris.asm + charmap.asm, asserting the corpus totals,
                              the four constants, all 22 labels' dimensions, and the boundary bytes.
                              Skips cleanly when the disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_tilemaps
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_tilemaps as pt  # noqa: E402

P = Path("tetris.asm")


# --- charmap table (mirrors charmap.asm; the encoder takes a plain dict) -------------------------

def _table() -> dict[str, int]:
    t = {ch: i for i, ch in enumerate("0123456789")}
    for i, ch in enumerate("abcdefghijklmnopqrstuvwxyz"):
        t[ch] = 0x0A + i
    t.update({".": 0x24, "-": 0x25, "×": 0x26, "♥": 0x27, "⋯": 0x29, " ": 0x2F,
              "©": 0x33, "…": 0x60, "”": 0x9B, ",": 0x9C, ".”": 0x9D})
    return t


# --- synthetic corpus builder (valid baseline the edge-case tests perturb) -----------------------

_TOWER_BYTES = [0xC2, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA]
_CONGRATS_BYTES = [0xB3, 0xBC, 0x3D, 0xBE, 0xBB, 0xB5, 0x1D, 0xB2,
                   0xBD, 0xB5, 0x1D, 0x2E, 0xBC, 0x3D, 0x0E, 0x3E]

_C1 = ["TypeAGameplayTilemap", "TypeBGameplayTilemap", "CopyrightScreenTilemap",
       "TitleScreenTilemap", "ConfigScreenTilemap", "TypeADifficultyTilemap",
       "TypeBDifficultyTilemap", "MultiplayerDifficultyTilemap", "MultiplayerGameplayTilemap"]


def _uniform(rows: int, width: int, val: int = 0x2A) -> list[list[int]]:
    return [[val] * width for _ in range(rows)]


def _default_blocks() -> dict[str, list[list[int]]]:
    blocks: dict[str, list[list[int]]] = {}
    for name in _C1:
        blocks[name] = _uniform(18, 20)
    blocks["MultiplayerVictoryTopTilemap"] = _uniform(4, 20)
    blocks["MultiplayerVictoryBottomTilemap"] = _uniform(6, 20)
    blocks["BuranBackdropTilemap"] = _uniform(4, 20)
    blocks["ScoreboardTilemap"] = _uniform(18, 10) + [[0xFF]]
    blocks["DancersTilemap"] = _uniform(18, 10) + [[0xFF]]
    blocks["PauseMessageTilemap"] = _uniform(10, 8)
    blocks["Data_293E"] = _uniform(7, 8)
    blocks["Data_2976"] = _uniform(6, 8)
    for name in ("LeftTowerLeftSideTilemap", "LeftTowerRightSideTilemap",
                 "RightTowerLeftSideTilemap", "RightTowerRightSideTilemap"):
        blocks[name] = [list(_TOWER_BYTES)]
    return blocks


def _emit_block(label: str, rows: list[list[int]]) -> list[str]:
    out = [f"{label}::"]
    for row in rows:
        out.append("    db " + ", ".join(f"${b & 0xFF:02X}" for b in row))
    out.append("")
    return out


def _render(*, blocks_override: dict | None = None, drop_label: str | None = None,
            duplicate_label: str | None = None, window_ld_b: str = "$08", tower_ld_b: str = "7",
            top_ld_b: str = "4", bottom_ld_b: str = "6", buran_ld_b: str = "4",
            load_tilemap_b: bool = True, load_tilemap_c: bool = True,
            pf_ld_b: str = "10", pf_cp: bool = True, congrats_bytes: int = 16,
            second_bottom_call: bool = True) -> str:
    blocks = _default_blocks()
    if blocks_override:
        blocks.update(blocks_override)
    if drop_label:
        blocks.pop(drop_label, None)

    lines = ['SECTION "test", ROM0', ""]

    lines += ["LoadTilemap::"]
    if load_tilemap_b:
        lines += ["    ld b, SCRN_Y_B"]
    if load_tilemap_c:
        lines += ["    ld c, SCRN_X_B"]
    lines += ["    ret", ""]

    lines += ["LoadPlayingFieldTilemap::", f"    ld b, {pf_ld_b}"]
    if pf_cp:
        lines += ["    cp a, $FF"]
    lines += ["    ret", ""]

    lines += ["Call_1F7D::", f"    ld b, {window_ld_b}", "    ret", ""]
    lines += ["LoadTilemap9C00Row::", "    ret", ""]

    # Call sites: banners (columnLoop) and towers (LoadTilemap9C00Row).
    lines += ["Callers::"]
    lines += ["    ld de, MultiplayerVictoryTopTilemap", f"    ld b, {top_ld_b}",
              "    call LoadTilemap.columnLoop", "    ld hl, $9980", f"    ld b, {bottom_ld_b}"]
    if second_bottom_call:
        lines += ["    call LoadTilemap.columnLoop"]
    lines += ["    ld hl, $9DC0", "    ld de, BuranBackdropTilemap", f"    ld b, {buran_ld_b}",
              "    call LoadTilemap.columnLoop"]
    for name in ("LeftTowerLeftSideTilemap", "LeftTowerRightSideTilemap",
                 "RightTowerLeftSideTilemap", "RightTowerRightSideTilemap"):
        lines += [f"    ld de, {name}", f"    ld b, {tower_ld_b}", "    call LoadTilemap9C00Row"]
    lines += ["    ret", ""]

    # Congratulations: local label inside GameState_2C.
    lines += ["GameState_2C::", "    ld a, 1", "    .data_12F5",
              "    db " + ", ".join(f"${b & 0xFF:02X}" for b in _CONGRATS_BYTES[:congrats_bytes]),
              "GameState_2D::", "    ret", ""]

    for name, rows in blocks.items():
        lines += _emit_block(name, rows)
    if duplicate_label and duplicate_label in blocks:
        lines += _emit_block(duplicate_label, blocks[duplicate_label])

    return "\n".join(lines) + "\n"


def _by_name(result: dict, name: str) -> dict:
    return next(info for info in result["labels"] if info["name"] == name)


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class Encoder(unittest.TestCase):
    def setUp(self):
        self.t = _table()

    def test_plain_text(self):
        self.assertEqual(pt.encode_text("single    ", self.t, P, 1),
                         [0x1C, 0x12, 0x17, 0x10, 0x15, 0x0E, 0x2F, 0x2F, 0x2F, 0x2F])

    def test_greedy_ligature_wins_over_period(self):
        # ".<U+201D>" is one tile ($9D), not "." ($24) then "<U+201D>" ($9B).
        self.assertEqual(pt.encode_text("a.”b", self.t, P, 1), [0x0A, 0x9D, 0x0B])

    def test_bare_period_still_maps(self):
        self.assertEqual(pt.encode_text("nintendo.", self.t, P, 1),
                         [0x17, 0x12, 0x17, 0x1D, 0x0E, 0x17, 0x0D, 0x18, 0x24])

    def test_heart_and_multiply(self):
        self.assertEqual(pt.encode_text("again♥", self.t, P, 1),
                         [0x0A, 0x10, 0x0A, 0x12, 0x17, 0x27])
        self.assertEqual(pt.encode_text("×", self.t, P, 1), [0x26])

    def test_unmapped_character_raises(self):
        with self.assertRaises(SystemExit):
            pt.encode_text("hi!", self.t, P, 1)


class Tokenizer(unittest.TestCase):
    def test_pure_bytes(self):
        segs = pt._tokenize_db("$C2, $CA, $CA", P, 1)
        self.assertEqual(segs, [("byte", 0xC2), ("byte", 0xCA), ("byte", 0xCA)])

    def test_pure_string(self):
        self.assertEqual(pt._tokenize_db('"single    "', P, 1), [("str", "single    ")])

    def test_mixed_string_and_bytes(self):
        segs = pt._tokenize_db('$64, " game ", $65', P, 1)
        self.assertEqual(segs, [("byte", 0x64), ("str", " game "), ("byte", 0x65)])

    def test_string_containing_comma(self):
        # The comma is a mapped glyph inside the string, not a segment separator.
        self.assertEqual(pt._tokenize_db('"elorg,"', P, 1), [("str", "elorg,")])

    def test_unterminated_string_raises(self):
        with self.assertRaises(SystemExit):
            pt._tokenize_db('"oops', P, 1)

    def test_dollar_without_hex_raises(self):
        with self.assertRaises(SystemExit):
            pt._tokenize_db("$, $CA", P, 1)


class DecodeAndComment(unittest.TestCase):
    def setUp(self):
        self.t = _table()

    def test_decode_mixed_line(self):
        # Copyright row: `db "   ©", $30, $31, $32, $31, " ", $34..$39, "     "` -> 20 bytes.
        operand = '"   ©", $30, $31, $32, $31, " ", $34, $35, $36, $37, $38, $39, "     "'
        row, _segs = pt._decode_db_line(operand, self.t, P, 1)
        self.assertEqual(len(row), 20)
        self.assertEqual(row[:5], [0x2F, 0x2F, 0x2F, 0x33, 0x30])
        self.assertEqual(row[-1], 0x2F)

    def test_decode_gameover_row(self):
        row, _segs = pt._decode_db_line('$64, " game ", $65', self.t, P, 1)
        self.assertEqual(row, [0x64, 0x2F, 0x10, 0x0A, 0x16, 0x0E, 0x2F, 0x65])

    def test_row_comment_pure_string(self):
        _row, segs = pt._decode_db_line('"single    "', self.t, P, 1)
        self.assertEqual(pt._row_comment(segs), '"single    "')

    def test_row_comment_mixed_is_ascii_safe(self):
        _row, segs = pt._decode_db_line('$64, " ", $AD, $65', self.t, P, 1)
        self.assertEqual(pt._row_comment(segs), '$64 " " $AD $65')

    def test_row_comment_non_ascii_rendered(self):
        _row, segs = pt._decode_db_line('"again♥"', self.t, P, 1)
        comment = pt._row_comment(segs)
        self.assertTrue(comment.isascii())
        self.assertIn("<U+2665>", comment)

    def test_row_comment_pure_bytes_is_none(self):
        _row, segs = pt._decode_db_line("$C2, $CA", self.t, P, 1)
        self.assertIsNone(pt._row_comment(segs))


class ParseValid(unittest.TestCase):
    def setUp(self):
        self.result = pt.parse_tilemaps(_render(), _table(), P)

    def test_all_22_labels(self):
        self.assertEqual(len(self.result["labels"]), 22)

    def test_derived_constants(self):
        self.assertEqual(self.result["window_cols"], 8)
        self.assertEqual(self.result["tower_rows"], 7)

    def test_corpus_total(self):
        self.assertEqual(sum(len(i["flat"]) for i in self.result["labels"]), 4110)

    def test_c1_shape(self):
        info = _by_name(self.result, "kTypeAGameplayTilemap")
        self.assertEqual(len(info["grid_rows"]), 18)
        self.assertTrue(all(len(r) == 20 for r in info["grid_rows"]))
        self.assertEqual(len(info["flat"]), 360)

    def test_c2_bottom_is_six_rows(self):
        info = _by_name(self.result, "kMultiplayerVictoryBottomTilemap")
        self.assertEqual(len(info["grid_rows"]), 6)
        self.assertEqual(len(info["flat"]), 120)

    def test_c3_drops_sentinel_from_grid_keeps_in_flat(self):
        info = _by_name(self.result, "kScoreboardTilemap")
        self.assertEqual(len(info["grid_rows"]), 18)
        self.assertTrue(all(len(r) == 10 for r in info["grid_rows"]))
        self.assertEqual(len(info["flat"]), 181)
        self.assertEqual(info["flat"][-1], 0xFF)

    def test_c4_row_counts(self):
        self.assertEqual(len(_by_name(self.result, "kPauseMessageTilemap")["grid_rows"]), 10)
        self.assertEqual(len(_by_name(self.result, "kGameOverTilemap")["grid_rows"]), 7)
        self.assertEqual(len(_by_name(self.result, "kTryAgainTilemap")["grid_rows"]), 6)

    def test_c5_tower_is_seven(self):
        info = _by_name(self.result, "kLeftTowerLeftSideTilemap")
        self.assertEqual(info["flat_1d"], _TOWER_BYTES)

    def test_c6_congrats_is_sixteen(self):
        info = _by_name(self.result, "kCongratulationsTilemap")
        self.assertEqual(info["flat_1d"], _CONGRATS_BYTES)


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class EdgeCasesMustRaise(unittest.TestCase):
    def _parse(self, **kw):
        return pt.parse_tilemaps(_render(**kw), _table(), P)

    def test_c1_extra_row_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"TypeAGameplayTilemap": _uniform(19, 20)})

    def test_c1_short_row_raises(self):
        rows = _uniform(18, 20)
        rows[5] = [0x2A] * 19
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"TypeAGameplayTilemap": rows})

    def test_c3_missing_sentinel_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"ScoreboardTilemap": _uniform(18, 10)})

    def test_c3_sentinel_midstream_raises(self):
        rows = _uniform(18, 10) + [[0xFF]]
        rows[9] = [0xFF]
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"ScoreboardTilemap": rows})

    def test_c4_wrong_width_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"PauseMessageTilemap": _uniform(10, 7)})

    def test_missing_label_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(drop_label="TitleScreenTilemap")

    def test_duplicate_label_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(duplicate_label="TitleScreenTilemap")

    def test_window_width_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(window_ld_b="$09")

    def test_tower_height_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(tower_ld_b="6")

    def test_tower_call_disagreement_raises(self):
        # Break one tower call site's data width so it disagrees with `ld b, 7`.
        with self.assertRaises(SystemExit):
            self._parse(blocks_override={"LeftTowerLeftSideTilemap": [[0xC2] * 6]})

    def test_top_banner_count_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(top_ld_b="5")

    def test_bottom_banner_count_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(bottom_ld_b="7")

    def test_buran_count_mismatch_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(buran_ld_b="3")

    def test_bottom_banner_missing_second_call_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(second_bottom_call=False)

    def test_load_tilemap_missing_b_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(load_tilemap_b=False)

    def test_load_tilemap_missing_c_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(load_tilemap_c=False)

    def test_playing_field_wrong_width_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(pf_ld_b="9")

    def test_playing_field_missing_sentinel_test_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(pf_cp=False)

    def test_congrats_wrong_length_breaks_corpus_total(self):
        with self.assertRaises(SystemExit):
            self._parse(congrats_bytes=15)

    def test_missing_congrats_local_label_raises(self):
        text = _render().replace("    .data_12F5\n", "")
        with self.assertRaises(SystemExit):
            pt.parse_tilemaps(text, _table(), P)


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.result = pt.parse_tilemaps(_render(), _table(), P)

    def test_inc_has_constants_and_grids(self):
        inc = pt.emit_inc(self.result, "abc1234")
        self.assertIn("inline constexpr std::uint8_t kTilemapScreenCols = 20;", inc)
        self.assertIn("inline constexpr std::uint8_t kTilemapScreenRows = 18;", inc)
        self.assertIn("inline constexpr std::uint8_t kTilemapWindowCols = 8;", inc)
        self.assertIn("inline constexpr std::uint8_t kTowerTilemapRows = 7;", inc)
        self.assertIn("kTypeAGameplayTilemap = {{", inc)
        self.assertIn("std::array<std::array<std::uint8_t, kPlayingFieldCols>, kPlayingFieldRows>", inc)
        self.assertIn("std::array<std::uint8_t, kTowerTilemapRows> kLeftTowerLeftSideTilemap", inc)
        # 1-D arrays open with `{{` and must close with `}};` (not a collapsed `};`).
        self.assertIn("0xCA,  // vertical column, top to bottom\n}};", inc)
        self.assertEqual(inc.count("{{"), inc.count("}}"))
        self.assertNotIn("namespace kirpich {", inc)  # spliced mid-namespace; opens nothing
        self.assertTrue(inc.isascii())

    def test_fixture_shape(self):
        fixture = pt.emit_fixture(self.result, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        self.assertIn("std::array<std::uint8_t, 360> kExpectedTypeAGameplayTilemapBytes", fixture)
        self.assertIn("std::array<std::uint8_t, 181> kExpectedScoreboardTilemapBytes", fixture)
        self.assertIn("std::array<std::uint8_t, 16> kExpectedCongratulationsTilemapBytes", fixture)
        self.assertNotIn('#include "data/tilemaps.h"', fixture)
        self.assertTrue(fixture.isascii())

    def test_fixture_is_ascii_even_with_text_rows(self):
        # A block with a heart glyph must still emit ASCII (comment rendered as <U+XXXX>).
        result = pt.parse_tilemaps(
            _render(blocks_override={"Data_2976": [
                [0x2F, 0x2F, 0x0A, 0x10, 0x0A, 0x12, 0x17, 0x27]] * 6}),
            _table(), P)
        self.assertTrue(pt.emit_fixture(result, "abc1234").isascii())


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
        table = pt.build_charmap_table(self.root)
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        self.result = pt.parse_tilemaps(text, table, self.root / "tetris.asm")

    def _grid(self, name):
        return _by_name(self.result, name)["grid_rows"]

    def _flat(self, name):
        return _by_name(self.result, name)["flat"]

    def test_constants_and_total(self):
        self.assertEqual(self.result["window_cols"], 8)
        self.assertEqual(self.result["tower_rows"], 7)
        self.assertEqual(sum(len(i["flat"]) for i in self.result["labels"]), 4110)

    def test_all_dimensions(self):
        expected = {
            "kTypeAGameplayTilemap": 360, "kTypeBGameplayTilemap": 360,
            "kCopyrightScreenTilemap": 360, "kTitleScreenTilemap": 360,
            "kConfigScreenTilemap": 360, "kTypeADifficultyTilemap": 360,
            "kTypeBDifficultyTilemap": 360, "kMultiplayerDifficultyTilemap": 360,
            "kMultiplayerGameplayTilemap": 360,
            "kMultiplayerVictoryTopTilemap": 80, "kMultiplayerVictoryBottomTilemap": 120,
            "kBuranBackdropTilemap": 80,
            "kScoreboardTilemap": 181, "kDancersTilemap": 181,
            "kPauseMessageTilemap": 80, "kGameOverTilemap": 56, "kTryAgainTilemap": 48,
            "kLeftTowerLeftSideTilemap": 7, "kLeftTowerRightSideTilemap": 7,
            "kRightTowerLeftSideTilemap": 7, "kRightTowerRightSideTilemap": 7,
            "kCongratulationsTilemap": 16,
        }
        actual = {i["name"]: len(i["flat"]) for i in self.result["labels"]}
        self.assertEqual(actual, expected)

    def test_boundary_bytes(self):
        self.assertEqual(self._grid("kTypeAGameplayTilemap")[0][0], 0x2A)
        self.assertEqual(self._grid("kTitleScreenTilemap")[0][0], 0x8E)
        self.assertEqual(self._grid("kGameOverTilemap")[0][0], 0x61)
        self.assertEqual(self._grid("kDancersTilemap")[17][9], 0x7D)
        self.assertEqual(_by_name(self.result, "kCongratulationsTilemap")["flat_1d"][0], 0xB3)
        self.assertEqual(_by_name(self.result, "kCongratulationsTilemap")["flat_1d"][15], 0x3E)
        self.assertEqual(_by_name(self.result, "kLeftTowerLeftSideTilemap")["flat_1d"],
                         [0xC2, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA, 0xCA])

    def test_charmap_text_rows(self):
        # "score" run in the Type-A gameplay map.
        self.assertEqual(self._grid("kTypeAGameplayTilemap")[1][14:19], [0x1C, 0x0C, 0x18, 0x1B, 0x0E])
        # The ".<U+201D>" ligature is one tile ($9D) at the end of the credits line.
        self.assertEqual(self._grid("kCopyrightScreenTilemap")[16][19], 0x9D)
        # The scoreboard's " 0 x 40   " row decodes verbatim through the charmap.
        self.assertEqual(self._grid("kScoreboardTilemap")[1],
                         [0x2F, 0x00, 0x2F, 0x26, 0x2F, 0x04, 0x00, 0x2F, 0x2F, 0x2F])
        # The try-again heart.
        self.assertEqual(self._grid("kTryAgainTilemap")[4][7], 0x27)

    def test_buran_solid_row(self):
        self.assertEqual(self._grid("kBuranBackdropTilemap")[3], [0x07] * 20)

    def test_scoreboard_sentinel_only_in_flat(self):
        flat = self._flat("kScoreboardTilemap")
        self.assertEqual(len(flat), 181)
        self.assertEqual(flat[180], 0xFF)
        # The composed grid carries no sentinel.
        for row in self._grid("kScoreboardTilemap"):
            self.assertNotIn(0xFF, row)


if __name__ == "__main__":
    unittest.main()
