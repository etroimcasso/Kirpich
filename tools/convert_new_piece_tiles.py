#!/usr/bin/env python3
"""Convert the new-piece tile strip into the index format the game loads.

The art is drawn as an 8-bit grayscale+alpha PNG — four grey levels (0, 85, 170, 255) and alpha-0
holes where a tile is meant to be see-through. The game's tile loader keeps a PNG's own sample value
AS the index and never scales it, which is right for the extracted sheets (2-bit grayscale, samples
0..3) and wrong for an 8-bit one: the indices would come out 0/85/170/255 and land outside the
four-entry palettes every sheet is drawn through.

So newPieces.png stays as the source of truth and this writes the sibling the game actually loads,
in the same format the extracted sheets use: 2-bit grayscale, one sample per pixel.

    index = grey / 85          0 darkest .. 3 lightest
    alpha == 0  ->  index 3    the lightest level, which is the see-through one on an object palette

Run from the repository root after any re-export of the source art:

    python3 tools/convert_new_piece_tiles.py

Both paths are fixed because there is exactly one such asset; nothing here is a general tool.
"""

import pathlib
import struct
import sys
import zlib

SOURCE = pathlib.Path("src/assets/gfx/newPieces.png")
TARGET = pathlib.Path("src/assets/gfx/newPieces-indexed.png")

# The four levels the art is drawn in, and the index each becomes.
GREY_STEP = 85
LEVELS = 4
TRANSPARENT_INDEX = LEVELS - 1


def _unfilter(raw, width, height, bytes_per_pixel, row_bytes):
    """Undo the PNG per-scanline filters, returning one bytes object per row."""
    rows = []
    previous = bytearray(row_bytes)
    pos = 0
    for _ in range(height):
        method = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + row_bytes])
        pos += row_bytes
        for i in range(row_bytes):
            left = line[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            up = previous[i]
            up_left = previous[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            if method == 1:
                line[i] = (line[i] + left) & 0xFF
            elif method == 2:
                line[i] = (line[i] + up) & 0xFF
            elif method == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif method == 4:
                estimate = left + up - up_left
                da, db, dc = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
                if da <= db and da <= dc:
                    line[i] = (line[i] + left) & 0xFF
                elif db <= dc:
                    line[i] = (line[i] + up) & 0xFF
                else:
                    line[i] = (line[i] + up_left) & 0xFF
            elif method != 0:
                raise ValueError(f"unknown PNG filter {method}")
        rows.append(bytes(line))
        previous = line
    return rows


def read_grey_alpha(path):
    """Read an 8-bit grayscale+alpha PNG as a grid of (grey, alpha) pairs."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")

    idat = b""
    width = height = depth = colortype = None
    offset = 8
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colortype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                raise ValueError("interlaced PNGs are not supported")
        elif kind == b"IDAT":
            idat += body
        offset += 12 + length

    if (depth, colortype) != (8, 4):
        raise ValueError(f"{path}: expected 8-bit grayscale+alpha, got depth {depth} colortype {colortype}")

    rows = _unfilter(zlib.decompress(idat), width, height, 2, width * 2)
    return width, height, [[(row[x * 2], row[x * 2 + 1]) for x in range(width)] for row in rows]


def to_indices(pixels):
    """Apply the mapping. Refuses anything that is not one of the four levels."""
    grid = []
    for y, row in enumerate(pixels):
        out = []
        for x, (grey, alpha) in enumerate(row):
            if alpha == 0:
                out.append(TRANSPARENT_INDEX)
                continue
            if grey % GREY_STEP != 0 or grey // GREY_STEP >= LEVELS:
                raise ValueError(
                    f"pixel ({x},{y}) is grey {grey}, which is not one of the four levels "
                    f"{[n * GREY_STEP for n in range(LEVELS)]}"
                )
            out.append(grey // GREY_STEP)
        grid.append(out)
    return grid


def write_two_bit_grey(path, width, height, grid):
    """Write a 2-bit grayscale PNG — the format the extracted sheets are in."""
    row_bytes = (width * 2 + 7) // 8
    raw = bytearray()
    for row in grid:
        raw.append(0)  # filter: none, so the bytes read the same as they are stored
        packed = bytearray(row_bytes)
        for x, index in enumerate(row):
            packed[x // 4] |= (index & 0x03) << (6 - 2 * (x % 4))  # PNG packs MSB-first
        raw.extend(packed)

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 2, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def main():
    if not SOURCE.exists():
        sys.exit(f"{SOURCE} not found — run this from the repository root")
    width, height, pixels = read_grey_alpha(SOURCE)
    write_two_bit_grey(TARGET, width, height, to_indices(pixels))
    print(f"{SOURCE} -> {TARGET} ({width}x{height}, 2-bit grayscale)")


if __name__ == "__main__":
    main()
