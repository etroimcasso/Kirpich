#!/usr/bin/env python3
"""Parser for Kirpich's tile-graphics extraction table and its content fixture.

The port ships no graphics. They are derived from the Game Boy Tetris ROM: four blocks of tiles at
fixed ROM offsets, decoded to greyscale PNGs the engine then loads. Upstream's own dumper,
`dump_gfx.py`, records the four (name, offset, tile count, format) facts and the exact decode; this
parser reads those facts, decodes the ROM the same way, checks the result against the four PNGs
upstream committed, and emits the port's extraction table plus the test fixture.

The three inputs are made to agree, and every disagreement is a hard parse error:

  dump_gfx.py    the four dump_tiles(name, offset, count, oneBPP) calls - the extraction facts,
                 read out of the file's syntax tree (never executed; pypng is not installed here).
  the ROM        read at each (offset, count, format) and decoded exactly as upstream's own
                 tile_2bpp_to_pixels / tile_1bpp_to_pixels do, padding included.
  gfx/<name>.png the committed PNG for each block, decoded here by a minimal greyscale reader.

Assert (c) - that the ROM decode reproduces the committed PNG pixel-for-pixel - is what pins each
offset: a wrong offset decodes to different pixels and fails here, which is exactly how the once
mis-recorded copyrightandtitlescreen offset was caught.

Emission set = the extraction table `.inc` + the content fixture:

  src/data/generated/tile_graphics_data.inc   kTileGraphics, the four TileGraphic rows (file name,
                                              ROM offset, tile count, format), included at namespace
                                              scope by src/data/tile_graphics.h.
  tests/fixtures/tile_graphics_expected.h     kExpectedTileGraphics, the four rows plus each block's
                                              decoded PNG dimensions and the FNV-1a-64 hash of its
                                              decoded index content - NEVER the pixels themselves,
                                              which are copyright-derived and may not be committed.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import sys
import zlib
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

# The four graphics blocks, in dump_gfx.py order: (name, is 1bpp). The font is the sole 1bpp block;
# decoding any other as 1bpp silently produces garbage at twice the height, so the format is pinned
# here per block rather than trusted from the call site alone.
EXPECTED_BLOCKS = [
    ("font", True),
    ("copyrightandtitlescreen", False),
    ("configandgameplay", False),
    ("multiplayerandburan", False),
]

ROM_SIZE = 32768          # a DMG Tetris ROM is exactly 32 KiB, no MBC
TILES_PER_ROW = 16        # the dump lays tiles 16 to a PNG row (128 px wide)
TILE_PIXELS = 8           # each tile is 8x8

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# C++ emission.
CPP_ARRAY = "kTileGraphics"
CPP_FIXTURE = "kExpectedTileGraphics"
CPP_FIXTURE_ROW = "TileGraphicExpected"


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_tile_graphics"


# --- Pure helpers: dump-tuple extraction --------------------------------------------------------

def _eval_int_expr(node: ast.AST, path: Path) -> int:
    """Evaluate a dump_gfx.py offset expression (e.g. `0x415F + 39 * 8`) over its syntax tree.

    Whitelisted to integer literals and +, -, * only - never `eval`. Anything else is a hard error,
    so a change to the file's shape surfaces here rather than being silently accepted."""
    if isinstance(node, ast.Constant) and isinstance(node.value, int) and not isinstance(node.value, bool):
        return node.value
    if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
        left = _eval_int_expr(node.left, path)
        right = _eval_int_expr(node.right, path)
        if isinstance(node.op, ast.Add):
            return left + right
        if isinstance(node.op, ast.Sub):
            return left - right
        return left * right
    raise ParseError(
        f"{path}: an offset expression is not a simple integer arithmetic of +/-/* over literals "
        f"(got {ast.dump(node)})"
    )


def parse_dump_tuples(text: str, path: Path) -> list[dict]:
    """Every dump_tiles(...) call in dump_gfx.py, as {name, offset, count, one_bpp}, in file order.

    Reads the file's AST (never runs it - dump_gfx.py imports pypng, absent here). The offset arg is
    evaluated arithmetically; count is an integer literal; oneBPP is the keyword (default False, per
    the function's own signature)."""
    try:
        tree = ast.parse(text, filename=str(path))
    except SyntaxError as exc:
        raise ParseError(f"{path}: could not parse as Python: {exc}") from exc

    tuples: list[dict] = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "dump_tiles"):
            continue
        if len(node.args) < 3:
            raise ParseError(
                f"{path}:{node.lineno}: dump_tiles() needs at least (name, address, count); "
                f"found {len(node.args)} positional args"
            )
        name_node = node.args[0]
        if not (isinstance(name_node, ast.Constant) and isinstance(name_node.value, str)):
            raise ParseError(f"{path}:{node.lineno}: dump_tiles() name must be a string literal")
        count_node = node.args[2]
        if not (isinstance(count_node, ast.Constant) and isinstance(count_node.value, int)
                and not isinstance(count_node.value, bool)):
            raise ParseError(f"{path}:{node.lineno}: dump_tiles() count must be an integer literal")

        one_bpp = False
        for kw in node.keywords:
            if kw.arg == "oneBPP":
                if not (isinstance(kw.value, ast.Constant) and isinstance(kw.value.value, bool)):
                    raise ParseError(
                        f"{path}:{node.lineno}: dump_tiles() oneBPP must be a bool literal")
                one_bpp = kw.value.value

        tuples.append({
            "name": name_node.value,
            "offset": _eval_int_expr(node.args[1], path),
            "count": count_node.value,
            "one_bpp": one_bpp,
        })
    return tuples


def bytes_per_tile(one_bpp: bool) -> int:
    """16 for a 2bpp tile (two bitplanes x 8 rows), 8 for a 1bpp tile."""
    return 8 if one_bpp else 16


# --- Pure helpers: tile decode (verbatim from upstream dump_gfx.py) ------------------------------

def decode_2bpp_tile(tile: bytes) -> list[int]:
    """One 16-byte 2bpp tile to 64 pixel indices (0..3), row-major. Two interleaved bitplanes per
    row, low plane first; the value is inverted (`3 - ...`) exactly as upstream writes it."""
    if len(tile) != 16:
        raise ValueError("a 2bpp tile is 16 bytes")
    pixels: list[int] = []
    for row in range(8):
        hi = tile[row * 2 + 1]
        lo = tile[row * 2]
        for col in reversed(range(8)):
            pixels.append(3 - (((hi * 2) >> col & 0b10) + (lo >> col & 0b01)))
    return pixels


def decode_1bpp_tile(tile: bytes) -> list[int]:
    """One 8-byte 1bpp tile to 64 pixel indices (0..1), row-major; inverted (`1 - bit`) as upstream
    writes it."""
    if len(tile) != 8:
        raise ValueError("a 1bpp tile is 8 bytes")
    pixels: list[int] = []
    for row in range(8):
        byte = tile[row]
        for col in reversed(range(8)):
            pixels.append(1 - ((byte >> col) & 0b1))
    return pixels


def decode_rom_graphic(rom: bytes, offset: int, count: int, one_bpp: bool) -> dict:
    """Decode a graphics block from the ROM into a padded index grid, exactly as dump_gfx.py does:
    16 tiles per row, the last row padded with the fill value (1 for 1bpp, 3 for 2bpp - the value a
    raw-0 pixel maps to). Returns {width, height, values} with values row-major."""
    per_tile = bytes_per_tile(one_bpp)
    span_end = offset + count * per_tile
    if offset < 0 or span_end > len(rom):
        raise ParseError(
            f"decode window [0x{offset:04X}, 0x{span_end:04X}) escapes the {len(rom)}-byte ROM "
            f"for {count} tiles of {per_tile} bytes"
        )

    rows_of_tiles = (count + TILES_PER_ROW - 1) // TILES_PER_ROW
    width = TILES_PER_ROW * TILE_PIXELS
    height = rows_of_tiles * TILE_PIXELS
    fill = 1 if one_bpp else 3
    grid = [fill] * (width * height)

    for i in range(count):
        tile_x = (i % TILES_PER_ROW) * TILE_PIXELS
        tile_y = (i // TILES_PER_ROW) * TILE_PIXELS
        tile = rom[offset + i * per_tile: offset + (i + 1) * per_tile]
        pixels = decode_1bpp_tile(tile) if one_bpp else decode_2bpp_tile(tile)
        for py in range(TILE_PIXELS):
            base = (tile_y + py) * width + tile_x
            src = py * TILE_PIXELS
            grid[base: base + TILE_PIXELS] = pixels[src: src + TILE_PIXELS]

    return {"width": width, "height": height, "values": grid}


# --- Pure helpers: minimal greyscale PNG reader -------------------------------------------------

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter(raw: bytes, height: int, stride: int, bpp: int, path: Path) -> bytearray:
    """Reverse PNG scanline filtering (all five types) into one contiguous unfiltered image. `bpp`
    is bytes-per-pixel rounded up to one, which is 1 for the sub-byte depths this reader accepts."""
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        if pos + 1 + stride > len(raw):
            raise ParseError(f"{path}: PNG image data ends mid-scanline at row {y}")
        ftype = raw[pos]
        pos += 1
        line = bytearray(raw[pos: pos + stride])
        pos += stride
        if ftype == 0:
            pass
        elif ftype == 1:  # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        else:
            raise ParseError(f"{path}: unknown PNG filter type {ftype} at row {y}")
        out += line
        prev = line
    return out


def parse_png(data: bytes, path: Path) -> dict:
    """Decode a non-interlaced greyscale PNG (bit depth 1 or 2) into {width, height, bitdepth,
    values}, values row-major with one sample (the raw index) per pixel. Only the shape upstream's
    dumper emits is accepted; anything else is a hard error."""
    if data[:8] != PNG_SIGNATURE:
        raise ParseError(f"{path}: not a PNG (bad signature)")

    pos = 8
    width = height = bitdepth = color_type = interlace = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # 4 length + 4 type + body + 4 CRC
        if ctype == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
            bitdepth = body[8]
            color_type = body[9]
            interlace = body[12]
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if width is None:
        raise ParseError(f"{path}: PNG has no IHDR")
    if color_type != 0:
        raise ParseError(f"{path}: PNG colour type {color_type} is not greyscale (0)")
    if interlace != 0:
        raise ParseError(f"{path}: interlaced PNGs are not supported")
    if bitdepth not in (1, 2):
        raise ParseError(f"{path}: PNG bit depth {bitdepth} is not 1 or 2")

    raw = zlib.decompress(bytes(idat))
    stride = (width * bitdepth + 7) // 8
    unfiltered = _unfilter(raw, height, stride, 1, path)

    maxval = (1 << bitdepth) - 1
    values: list[int] = []
    for y in range(height):
        row = unfiltered[y * stride:(y + 1) * stride]
        bitpos = 0
        for _x in range(width):
            byte = row[bitpos >> 3]
            shift = 8 - bitdepth - (bitpos & 7)
            values.append((byte >> shift) & maxval)
            bitpos += bitdepth
    return {"width": width, "height": height, "bitdepth": bitdepth, "values": values}


# --- Pure helper: FNV-1a-64 ---------------------------------------------------------------------

_FNV64_OFFSET = 0xCBF29CE484222325
_FNV64_PRIME = 0x100000001B3
_U64 = 0xFFFFFFFFFFFFFFFF


def fnv1a64(values) -> int:
    """FNV-1a over a sequence of byte-sized index values (0..255), 64-bit. Drift detection, not
    security: the C++ test recomputes the identical hash from the engine-decoded indices."""
    h = _FNV64_OFFSET
    for v in values:
        h = ((h ^ (v & 0xFF)) * _FNV64_PRIME) & _U64
    return h


# --- Parse + assert -----------------------------------------------------------------------------

def parse_tile_graphics(dump_text: str, rom: bytes, expected_sha1: str, png_reader,
                        dump_path: Path) -> dict:
    """Assert the three-way agreement and return {"rows": [...]}. `png_reader(name) -> parse_png
    dict` supplies each committed PNG's decode, so the file I/O stays in the driver and this stays
    a pure function the tests drive with synthetic inputs."""
    # (a) ROM identity.
    if len(rom) != ROM_SIZE:
        raise ParseError(f"ROM is {len(rom)} bytes, expected exactly {ROM_SIZE}")
    actual_sha1 = hashlib.sha1(rom).hexdigest()
    if actual_sha1.lower() != expected_sha1.lower():
        raise ParseError(
            f"ROM SHA-1 {actual_sha1} does not match the expected {expected_sha1} "
            f"(tetris/rom.sha1)"
        )

    # (b) exactly the four expected blocks, in order, with the expected formats.
    tuples = parse_dump_tuples(dump_text, dump_path)
    got = [(t["name"], t["one_bpp"]) for t in tuples]
    if got != EXPECTED_BLOCKS:
        raise ParseError(
            f"{dump_path}: dump_tiles blocks {got} do not match the expected "
            f"{EXPECTED_BLOCKS} (name + 1bpp flag, in order)"
        )

    rows = []
    for t in tuples:
        # (c) + (d): decode window fits, and the ROM decode reproduces the committed PNG exactly.
        rom_grid = decode_rom_graphic(rom, t["offset"], t["count"], t["one_bpp"])
        png = png_reader(t["name"])
        want_bitdepth = 1 if t["one_bpp"] else 2
        if png["bitdepth"] != want_bitdepth:
            raise ParseError(
                f"{t['name']}.png bit depth {png['bitdepth']} != expected {want_bitdepth}")
        if (png["width"], png["height"]) != (rom_grid["width"], rom_grid["height"]):
            raise ParseError(
                f"{t['name']}: ROM decode is {rom_grid['width']}x{rom_grid['height']} but "
                f"{t['name']}.png is {png['width']}x{png['height']}"
            )
        if png["values"] != rom_grid["values"]:
            first = next(i for i, (a, b) in enumerate(zip(png["values"], rom_grid["values"]))
                         if a != b)
            raise ParseError(
                f"{t['name']}: ROM decode at offset 0x{t['offset']:04X} does not reproduce "
                f"{t['name']}.png (first difference at pixel {first}: PNG={png['values'][first]}, "
                f"ROM={rom_grid['values'][first]}); the offset or format is wrong"
            )
        rows.append({
            "file_name": f"{t['name']}.png",
            "offset": t["offset"],
            "count": t["count"],
            "one_bpp": t["one_bpp"],
            "width": rom_grid["width"],
            "height": rom_grid["height"],
            "content_hash": fnv1a64(rom_grid["values"]),
        })
    return {"rows": rows}


# --- Emit ---------------------------------------------------------------------------------------

def _format_token(one_bpp: bool) -> str:
    return "TileGraphicFormat::OneBpp" if one_bpp else "TileGraphicFormat::TwoBpp"


def emit_inc(result: dict, source_commit: str) -> str:
    rows = result["rows"]
    body = "\n".join(
        f'    {{ .fileName = "{common.cpp_escape(r["file_name"])}", '
        f'.romOffset = 0x{r["offset"]:04X}, .tileCount = {r["count"]}, '
        f'.format = {_format_token(r["one_bpp"])} }},'
        for r in rows
    )
    return f"""{common.banner("parse_tile_graphics.py", source_commit)}\
// Included at namespace scope in src/data/tile_graphics.h (inside `namespace kirpich`). The four
// graphics blocks the port extracts from the Game Boy Tetris ROM: file name, ROM offset, tile
// count, and 1bpp/2bpp format, read from upstream's dump_gfx.py and verified against the ROM.
inline constexpr std::array<TileGraphic, {len(rows)}> {CPP_ARRAY}{{{{
{body}
}}}};
"""


def _fixture_rows(rows: list[dict]) -> str:
    return "\n".join(
        f'    {{ .fileName = "{common.cpp_escape(r["file_name"])}", '
        f'.romOffset = 0x{r["offset"]:04X}, .tileCount = {r["count"]}, '
        f'.bitDepth = {1 if r["one_bpp"] else 2}, '
        f'.pngWidth = {r["width"]}, .pngHeight = {r["height"]}, '
        f'.contentHash = 0x{r["content_hash"]:016X}ULL }},'
        for r in rows
    )


def emit_fixture(result: dict, source_commit: str) -> str:
    rows = result["rows"]
    return f"""#pragma once
{common.banner("parse_tile_graphics.py", source_commit)}\
// Independent fixture for the full-corpus tile-graphics sweep: the {len(rows)} extraction rows plus,
// per block, its decoded PNG dimensions and the FNV-1a-64 hash of its decoded index content. The
// pixels themselves are ROM-derived and never committed - the hash is what the C++ test recomputes
// from the engine-decoded indices to prove the decode. Independent of the typed surface it guards.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// One graphics block's expected facts: its extraction row, the greyscale PNG's dimensions and bit
// depth, and the FNV-1a-64 hash over its decoded index content (row-major, one byte per pixel).
struct {CPP_FIXTURE_ROW} {{
    const char*   fileName;     // bare file name written under assets/gfx/default/
    std::uint16_t romOffset;    // first byte of the block in the ROM
    std::uint16_t tileCount;    // number of 8x8 tiles in the block
    std::uint8_t  bitDepth;     // 1 (font) or 2; the PNG's greyscale bit depth
    int           pngWidth;     // decoded width in pixels (always 128 - 16 tiles across)
    int           pngHeight;    // decoded height in pixels (ceil(tileCount / 16) * 8)
    std::uint64_t contentHash;  // FNV-1a-64 of the decoded index content
}};

inline constexpr std::array<{CPP_FIXTURE_ROW}, {len(rows)}> {CPP_FIXTURE}{{{{
{_fixture_rows(rows)}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Emit Kirpich's tile-graphics extraction table + content fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--rom", type=Path, required=True,
                        help="Path to the Tetris (World) (Rev 1) ROM (never committed).")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    dump_path = source_root / "dump_gfx.py"
    sha1_path = source_root / "rom.sha1"
    gfx_dir = source_root / "gfx"
    for needed in (dump_path, sha1_path):
        if not needed.is_file():
            print(f"parse_tile_graphics: source file not found: {needed}", file=sys.stderr)
            return 2
    if not args.rom.is_file():
        print(f"parse_tile_graphics: ROM not found: {args.rom}", file=sys.stderr)
        return 2

    dump_text = dump_path.read_bytes().decode("utf-8")
    rom = args.rom.read_bytes()
    expected_sha1 = sha1_path.read_text(encoding="ascii").split()[0]

    def png_reader(name: str) -> dict:
        png_path = gfx_dir / f"{name}.png"
        if not png_path.is_file():
            raise ParseError(f"committed PNG not found: {png_path}")
        return parse_png(png_path.read_bytes(), png_path)

    result = parse_tile_graphics(dump_text, rom, expected_sha1, png_reader, dump_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(result, commit),
        args.fixture_out: emit_fixture(result, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_tile_graphics: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_tile_graphics: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
