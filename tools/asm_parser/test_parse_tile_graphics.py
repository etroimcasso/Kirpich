#!/usr/bin/env python3
"""Unit tests for parse_tile_graphics.py.

Three layers per the parser test discipline:
  1. Helpers + valid parse  - the offset evaluator, the verbatim tile decode, the minimal PNG reader
                              (round-tripped against a synthetic encoder), FNV-1a-64 against a known
                              vector, dump-tuple extraction, and a full synthetic three-way parse.
  2. Synthetic edge cases   - malformed inputs that MUST raise (every structural assert has a raise
                              path), plus the shape of both emitted artifacts.
  3. End-to-end             - the real ../tetris/ dump_gfx.py + committed PNGs + the dev ROM,
                              asserting the four verified offsets, counts, dimensions, and that the
                              corrected copyrightandtitlescreen offset is 0x4297. Skips cleanly when
                              the disassembly or ROM is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_tile_graphics
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import hashlib
import os
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_tile_graphics as tg  # noqa: E402


DUMP = Path("dump_gfx.py")


# --- Synthetic builders -------------------------------------------------------------------------

# Four blocks matching the pinned (name, 1bpp) shape, at small counts that fit an all-zero ROM.
SYNTHETIC_BLOCKS = [
    ("font", 0x100, 4, True),
    ("copyrightandtitlescreen", 0x200, 4, False),
    ("configandgameplay", 0x400, 4, False),
    ("multiplayerandburan", 0x600, 4, False),
]


def _dump_text(blocks=SYNTHETIC_BLOCKS) -> str:
    lines = ["import png", "", "def dump_tiles(name, address, count, oneBPP=False):", "    pass",
             "", 'if __name__ == "__main__":']
    for name, off, count, one in blocks:
        lines.append(f'    dump_tiles("{name}", 0x{off:X}, {count}, oneBPP={one})')
    return "\n".join(lines) + "\n"


def _rom(fill: int = 0, size: int = tg.ROM_SIZE) -> bytes:
    return bytes([fill]) * size


def _make_png(width: int, height: int, bitdepth: int, values: list[int]) -> bytes:
    """Encode a non-interlaced greyscale PNG (real deflate, filter None) - the round-trip partner
    for parse_png, and stand-in for upstream's committed PNGs in the synthetic parse."""
    stride = (width * bitdepth + 7) // 8
    maxval = (1 << bitdepth) - 1
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type None
        row = bytearray(stride)
        bitpos = 0
        for x in range(width):
            v = values[y * width + x] & maxval
            shift = 8 - bitdepth - (bitpos & 7)
            row[bitpos >> 3] |= v << shift
            bitpos += bitdepth
        raw += row

    def chunk(ctype: bytes, body: bytes) -> bytes:
        head = len(body).to_bytes(4, "big") + ctype + body
        return head + (zlib.crc32(ctype + body) & 0xFFFFFFFF).to_bytes(4, "big")

    ihdr = (width.to_bytes(4, "big") + height.to_bytes(4, "big")
            + bytes([bitdepth, 0, 0, 0, 0]))  # bitdepth, greyscale, deflate, filter 0, no interlace
    return (tg.PNG_SIGNATURE + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def _png_reader_for(rom: bytes, blocks=SYNTHETIC_BLOCKS):
    """A png_reader that returns each block's own ROM decode, PNG-encoded and read back - so the
    synthetic three-way agreement holds by construction and every step is exercised."""
    by_name = {name: (off, count, one) for name, off, count, one in blocks}

    def reader(name: str) -> dict:
        off, count, one = by_name[name]
        grid = tg.decode_rom_graphic(rom, off, count, one)
        png = _make_png(grid["width"], grid["height"], 1 if one else 2, grid["values"])
        return tg.parse_png(png, Path(f"{name}.png"))

    return reader


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class OffsetEval(unittest.TestCase):
    def _eval(self, text: str) -> int:
        import ast
        node = ast.parse(text, mode="eval").body
        return tg._eval_int_expr(node, DUMP)

    def test_plain_literal(self):
        self.assertEqual(self._eval("0x415F"), 0x415F)

    def test_addition_and_multiplication(self):
        # The copyrightandtitlescreen offset, exactly as upstream writes it.
        self.assertEqual(self._eval("0x415F + 39 * 8"), 0x4297)

    def test_subtraction(self):
        self.assertEqual(self._eval("0x300 - 0x10"), 0x2F0)

    def test_non_literal_raises(self):
        with self.assertRaises(SystemExit):
            self._eval("width")


class TileDecode(unittest.TestCase):
    def test_2bpp_all_zero_is_all_three(self):
        self.assertEqual(tg.decode_2bpp_tile(bytes(16)), [3] * 64)

    def test_1bpp_all_zero_is_all_one(self):
        self.assertEqual(tg.decode_1bpp_tile(bytes(8)), [1] * 64)

    def test_2bpp_all_ff_is_all_zero(self):
        # both planes all ones -> hiBit*2 + loBit == 3 -> 3 - 3 == 0
        self.assertEqual(tg.decode_2bpp_tile(bytes([0xFF] * 16)), [0] * 64)

    def test_1bpp_all_ff_is_all_zero(self):
        self.assertEqual(tg.decode_1bpp_tile(bytes([0xFF] * 8)), [0] * 64)

    def test_2bpp_plane_interleave(self):
        # low plane 0x80 (leftmost pixel), high plane 0 -> first pixel loBit=1 -> 3-1 = 2.
        tile = bytes([0x80, 0x00] + [0] * 14)
        pixels = tg.decode_2bpp_tile(tile)
        self.assertEqual(pixels[0], 2)
        self.assertEqual(pixels[1:8], [3] * 7)

    def test_1bpp_leftmost_bit(self):
        tile = bytes([0x80] + [0] * 7)
        pixels = tg.decode_1bpp_tile(tile)
        self.assertEqual(pixels[0], 0)      # 1 - 1
        self.assertEqual(pixels[1:8], [1] * 7)


class DecodeRomGraphic(unittest.TestCase):
    def test_dimensions_and_fill_2bpp(self):
        grid = tg.decode_rom_graphic(_rom(), 0x200, 20, False)  # 20 tiles -> 2 rows of tiles
        self.assertEqual((grid["width"], grid["height"]), (128, 16))
        self.assertEqual(set(grid["values"]), {3})  # zero ROM -> all fill

    def test_dimensions_and_fill_1bpp(self):
        grid = tg.decode_rom_graphic(_rom(), 0x100, 4, True)
        self.assertEqual((grid["width"], grid["height"]), (128, 8))
        self.assertEqual(set(grid["values"]), {1})

    def test_window_escaping_rom_raises(self):
        with self.assertRaises(SystemExit):
            tg.decode_rom_graphic(_rom(), tg.ROM_SIZE - 8, 4, False)  # 4*16 > 8 bytes left


class ParsePngRoundTrip(unittest.TestCase):
    def _round_trip(self, width, height, bitdepth, values):
        png = _make_png(width, height, bitdepth, values)
        out = tg.parse_png(png, Path("x.png"))
        self.assertEqual((out["width"], out["height"], out["bitdepth"]), (width, height, bitdepth))
        self.assertEqual(out["values"], values)

    def test_2bpp_full_width(self):
        values = [(x + y) % 4 for y in range(8) for x in range(128)]
        self._round_trip(128, 8, 2, values)

    def test_1bpp_full_width(self):
        values = [(x ^ y) & 1 for y in range(24) for x in range(128)]
        self._round_trip(128, 24, 1, values)

    def test_2bpp_partial_final_byte(self):
        # width 6 at 2bpp = 12 bits -> 2 bytes, last byte half used; exercises the sub-byte unpack.
        values = [0, 1, 2, 3, 1, 2, 3, 0, 1, 2, 0, 3]
        self._round_trip(6, 2, 2, values)

    def test_1bpp_partial_final_byte(self):
        values = [1, 0, 1, 1, 0, 0, 1]  # width 7 -> 1 byte, 1 bit unused
        self._round_trip(7, 1, 1, values)


class Fnv(unittest.TestCase):
    def test_empty(self):
        self.assertEqual(tg.fnv1a64([]), 0xCBF29CE484222325)

    def test_known_vector_a(self):
        # FNV-1a-64 of the single byte 'a' (0x61) is a published test vector.
        self.assertEqual(tg.fnv1a64([0x61]), 0xAF63DC4C8601EC8C)

    def test_masks_to_64_bits(self):
        self.assertLessEqual(tg.fnv1a64([1, 2, 3] * 100), tg._U64)


class DumpTuples(unittest.TestCase):
    def test_extracts_all_four(self):
        tuples = tg.parse_dump_tuples(_dump_text(), DUMP)
        self.assertEqual([(t["name"], t["one_bpp"]) for t in tuples], tg.EXPECTED_BLOCKS)
        self.assertEqual([t["count"] for t in tuples], [4, 4, 4, 4])
        self.assertEqual([t["offset"] for t in tuples], [0x100, 0x200, 0x400, 0x600])

    def test_arithmetic_offset_expression(self):
        # The arithmetic form upstream actually uses for copyrightandtitlescreen.
        text = ('def dump_tiles(name, address, count, oneBPP=False):\n    pass\n'
                'if __name__ == "__main__":\n'
                '    dump_tiles("font", 0x100, 4, oneBPP=True)\n'
                '    dump_tiles("copyrightandtitlescreen", 0x100 + 39 * 8, 4, oneBPP=False)\n'
                '    dump_tiles("configandgameplay", 0x400, 4, oneBPP=False)\n'
                '    dump_tiles("multiplayerandburan", 0x600, 4, oneBPP=False)\n')
        tuples = tg.parse_dump_tuples(text, DUMP)
        self.assertEqual(tuples[1]["offset"], 0x100 + 39 * 8)

    def test_default_one_bpp_is_false(self):
        text = ('def dump_tiles(name, address, count, oneBPP=False):\n    pass\n'
                'dump_tiles("x", 0x10, 2)\n')
        tuples = tg.parse_dump_tuples(text, DUMP)
        self.assertEqual(tuples[0]["one_bpp"], False)

    def test_non_literal_offset_raises(self):
        text = ('def dump_tiles(name, address, count, oneBPP=False):\n    pass\n'
                'base = 5\ndump_tiles("x", base, 2)\n')
        with self.assertRaises(SystemExit):
            tg.parse_dump_tuples(text, DUMP)


class ParseValid(unittest.TestCase):
    def setUp(self):
        self.rom = _rom()
        self.sha1 = hashlib.sha1(self.rom).hexdigest()
        self.result = tg.parse_tile_graphics(
            _dump_text(), self.rom, self.sha1, _png_reader_for(self.rom), DUMP)

    def test_four_rows_in_order(self):
        rows = self.result["rows"]
        self.assertEqual([r["file_name"] for r in rows],
                         ["font.png", "copyrightandtitlescreen.png",
                          "configandgameplay.png", "multiplayerandburan.png"])

    def test_rows_carry_dims_and_hash(self):
        for r in self.result["rows"]:
            self.assertEqual(r["width"], 128)
            self.assertEqual(r["height"], 8)  # count 4 -> one tile row
            self.assertIsInstance(r["content_hash"], int)

    def test_hashes_recompute(self):
        # The stored hash is exactly FNV over that block's decoded content.
        for r, (_name, off, count, one) in zip(self.result["rows"], SYNTHETIC_BLOCKS):
            grid = tg.decode_rom_graphic(self.rom, off, count, one)
            self.assertEqual(r["content_hash"], tg.fnv1a64(grid["values"]))


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class EdgeCasesMustRaise(unittest.TestCase):
    def setUp(self):
        self.rom = _rom()
        self.sha1 = hashlib.sha1(self.rom).hexdigest()

    def _parse(self, *, rom=None, sha1=None, dump=None, reader=None):
        rom = self.rom if rom is None else rom
        return tg.parse_tile_graphics(
            _dump_text() if dump is None else dump,
            rom,
            self.sha1 if sha1 is None else sha1,
            _png_reader_for(self.rom) if reader is None else reader,
            DUMP)

    def test_wrong_rom_size_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(rom=_rom(size=tg.ROM_SIZE - 1))

    def test_wrong_sha1_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(sha1="0" * 40)

    def test_too_few_blocks_raises(self):
        with self.assertRaises(SystemExit):
            self._parse(dump=_dump_text(SYNTHETIC_BLOCKS[:3]))

    def test_wrong_block_name_raises(self):
        blocks = list(SYNTHETIC_BLOCKS)
        blocks[2] = ("configZZZ", 0x400, 4, False)
        with self.assertRaises(SystemExit):
            self._parse(dump=_dump_text(blocks))

    def test_wrong_block_order_raises(self):
        blocks = [SYNTHETIC_BLOCKS[1], SYNTHETIC_BLOCKS[0],
                  SYNTHETIC_BLOCKS[2], SYNTHETIC_BLOCKS[3]]
        with self.assertRaises(SystemExit):
            self._parse(dump=_dump_text(blocks))

    def test_wrong_format_flag_raises(self):
        blocks = list(SYNTHETIC_BLOCKS)
        blocks[0] = ("font", 0x100, 4, False)  # font must be 1bpp
        with self.assertRaises(SystemExit):
            self._parse(dump=_dump_text(blocks))

    def test_png_dimension_mismatch_raises(self):
        def reader(name):
            if name == "font":
                return _wrong  # a differently-sized PNG
            return _png_reader_for(self.rom)(name)
        _wrong = tg.parse_png(_make_png(128, 16, 1, [1] * (128 * 16)), Path("font.png"))
        with self.assertRaises(SystemExit):
            self._parse(reader=reader)

    def test_png_content_mismatch_raises(self):
        good = _png_reader_for(self.rom)

        def reader(name):
            out = dict(good(name))
            if name == "configandgameplay":
                out = dict(out)
                vals = list(out["values"])
                vals[0] = (vals[0] + 1) % 4  # flip one pixel
                out["values"] = vals
            return out
        with self.assertRaises(SystemExit):
            self._parse(reader=reader)

    def test_png_bitdepth_mismatch_raises(self):
        good = _png_reader_for(self.rom)

        def reader(name):
            if name == "font":
                # a 2bpp PNG where a 1bpp one is expected
                grid = tg.decode_rom_graphic(self.rom, 0x100, 4, True)
                return tg.parse_png(_make_png(grid["width"], grid["height"], 2, grid["values"]),
                                    Path("font.png"))
            return good(name)
        with self.assertRaises(SystemExit):
            self._parse(reader=reader)


class ParsePngRejects(unittest.TestCase):
    def test_bad_signature_raises(self):
        with self.assertRaises(SystemExit):
            tg.parse_png(b"not a png" + bytes(50), Path("x.png"))

    def test_truecolour_raises(self):
        png = bytearray(_make_png(8, 8, 2, [0] * 64))
        # IHDR colour-type byte is at signature(8)+len(4)+type(4)+9 = offset 25.
        png[25] = 2  # truecolour
        with self.assertRaises(SystemExit):
            tg.parse_png(bytes(png), Path("x.png"))

    def test_bitdepth_8_raises(self):
        png = bytearray(_make_png(8, 8, 2, [0] * 64))
        png[24] = 8  # IHDR bit-depth byte
        with self.assertRaises(SystemExit):
            tg.parse_png(bytes(png), Path("x.png"))

    def test_interlaced_raises(self):
        png = bytearray(_make_png(8, 8, 2, [0] * 64))
        png[28] = 1  # IHDR interlace byte (offset 8+8+9+... = 28)
        with self.assertRaises(SystemExit):
            tg.parse_png(bytes(png), Path("x.png"))


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        rom = _rom()
        self.result = tg.parse_tile_graphics(
            _dump_text(), rom, hashlib.sha1(rom).hexdigest(), _png_reader_for(rom), DUMP)

    def test_inc_holds_the_array_and_rows(self):
        inc = tg.emit_inc(self.result, "abc1234")
        self.assertIn("inline constexpr std::array<TileGraphic, 4> kTileGraphics{{", inc)
        self.assertIn('.fileName = "font.png", .romOffset = 0x0100, .tileCount = 4, '
                      '.format = TileGraphicFormat::OneBpp', inc)
        self.assertIn(".format = TileGraphicFormat::TwoBpp", inc)
        self.assertEqual(inc.count(".fileName ="), 4)
        self.assertNotIn("namespace kirpich {", inc)  # included mid-namespace; opens nothing
        self.assertTrue(inc.isascii())

    def test_fixture_is_independent_of_the_surface(self):
        fixture = tg.emit_fixture(self.result, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("struct TileGraphicExpected", fixture)
        self.assertIn("std::array<TileGraphicExpected, 4> kExpectedTileGraphics", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        self.assertIn(".contentHash = 0x", fixture)
        self.assertEqual(fixture.count(".fileName ="), 4)
        # Independent of the port surface: it neither includes nor names the header it guards.
        self.assertNotIn('#include "data/tile_graphics.h"', fixture)
        self.assertNotIn("TileGraphicFormat", fixture)
        self.assertTrue(fixture.isascii())


# --- Layer 3: end-to-end against the real disassembly + ROM -------------------------------------

def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "dump_gfx.py").is_file():
            return candidate
    return None


def _find_rom() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    home = Path(os.environ["HOME"]) if os.environ.get("HOME") else None
    candidates = [
        home / "ci-assets" / "kirpich" / "tetris.gb" if home else None,   # CI fixed path
        project_root.parent / "rom" / "Tetris (World) (Rev 1).gb",        # dev sibling
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        self.rom_path = _find_rom()
        if self.root is None or self.rom_path is None:
            self.skipTest("tetris disassembly and/or ROM not found (unit-only run)")
        dump_text = (self.root / "dump_gfx.py").read_bytes().decode("utf-8")
        rom = self.rom_path.read_bytes()
        expected_sha1 = (self.root / "rom.sha1").read_text(encoding="ascii").split()[0]

        def reader(name):
            return tg.parse_png((self.root / "gfx" / f"{name}.png").read_bytes(),
                                self.root / "gfx" / f"{name}.png")
        self.result = tg.parse_tile_graphics(dump_text, rom, expected_sha1, reader,
                                             self.root / "dump_gfx.py")

    def test_four_verified_rows(self):
        by_name = {r["file_name"]: r for r in self.result["rows"]}
        self.assertEqual(set(by_name), {"font.png", "copyrightandtitlescreen.png",
                                        "configandgameplay.png", "multiplayerandburan.png"})
        self.assertEqual(by_name["font.png"]["offset"], 0x415F)
        self.assertEqual(by_name["font.png"]["count"], 39)
        self.assertEqual(by_name["font.png"]["one_bpp"], True)
        # The corrected offset - a wrong one would fail assert (c) before reaching here.
        self.assertEqual(by_name["copyrightandtitlescreen.png"]["offset"], 0x4297)
        self.assertEqual(by_name["copyrightandtitlescreen.png"]["count"], 119)
        self.assertEqual(by_name["configandgameplay.png"]["offset"], 0x323F)
        self.assertEqual(by_name["configandgameplay.png"]["count"], 197)
        self.assertEqual(by_name["multiplayerandburan.png"]["offset"], 0x55AC)
        self.assertEqual(by_name["multiplayerandburan.png"]["count"], 207)

    def test_dimensions(self):
        by_name = {r["file_name"]: r for r in self.result["rows"]}
        self.assertEqual((by_name["font.png"]["width"], by_name["font.png"]["height"]), (128, 24))
        self.assertEqual((by_name["copyrightandtitlescreen.png"]["width"],
                          by_name["copyrightandtitlescreen.png"]["height"]), (128, 64))
        self.assertEqual((by_name["configandgameplay.png"]["width"],
                          by_name["configandgameplay.png"]["height"]), (128, 104))
        self.assertEqual((by_name["multiplayerandburan.png"]["width"],
                          by_name["multiplayerandburan.png"]["height"]), (128, 104))

    def test_hashes_stable_and_nonzero(self):
        for r in self.result["rows"]:
            self.assertNotEqual(r["content_hash"], 0)
            self.assertNotEqual(r["content_hash"], tg._FNV64_OFFSET)  # non-empty content


if __name__ == "__main__":
    unittest.main()
