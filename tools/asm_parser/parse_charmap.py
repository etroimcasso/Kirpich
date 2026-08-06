#!/usr/bin/env python3
"""Parser for Kirpich's charmap - the character-sequence -> tile-index table.

`charmap.asm` is the disassembly's text-encoding table: 47 `charmap "<seq>", $HH` lines, each
mapping a character sequence to the VRAM tile index it renders as. Two consumer shapes depend on
it downstream - the static `db "..."` text rows (encoded through this table exactly as RGBDS did)
and in-code single-character literals used directly in instructions. Both need one source of truth.

Provenance: the whole file is a transcribable corpus - the paradigm case for full parser emission.
The sequences are UTF-8 string keys, not a named symbol set, so there is no enum header here; the
tile values are plain numerics. Emission set = the data `.inc` + the test fixture.

  src/data/generated/charmap_data.inc   47 CharmapEntry rows, source order (included inside the
                                        constexpr array in src/data/charmap.cpp)
  tests/fixtures/charmap_expected.h      the same 47 rows in an independent fixture struct, so a
                                        defect in the engine header cannot mask the sweep

Every emitted artifact is ASCII-only: printable-ASCII sequence bytes go verbatim, every other byte
is a `\\xHH` escape, with the human-readable character and code points in a trailing comment. This
removes execution-charset variance across the five CI toolchains.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Any deviation from
the expected structure is a hard error with a source citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

ENTRY_COUNT = 47
DIGITS = "0123456789"          # -> $00..$09 (digit == tile index; load-bearing for BCD scores)
LETTERS = "abcdefghijklmnopqrstuvwxyz"  # -> $0A..$23 (contiguous)
DIGIT_BASE = 0x00
LETTER_BASE = 0x0A
# The one two-code-point sequence in the corpus: the ".<right-double-quote>" ligature -> $9D.
LIGATURE_SEQUENCE = ".”"
LIGATURE_TILE = 0x9D

# Enumerator names for the CharTile enum (include/kirpich/char_tile.h). Digits and letters derive
# mechanically; every other sequence must appear here — an unnamed sequence is a hard error,
# so a new upstream entry cannot silently emit a nameless enum.
SYMBOL_NAMES = {
    ".": "PERIOD",
    "-": "HYPHEN",
    "×": "MULTIPLICATION_SIGN",
    "♥": "HEART",
    "⋯": "MIDLINE_ELLIPSIS",
    " ": "SPACE",
    "©": "COPYRIGHT",
    "…": "ELLIPSIS",
    "”": "RIGHT_DOUBLE_QUOTE",
    ",": "COMMA",
    ".”": "PERIOD_RIGHT_DOUBLE_QUOTE",
}

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")

# One space between `charmap` and the quote, `, ` before the value; optional trailing `; comment`.
# The sequence delimiter is the ASCII double quote 0x22; no corpus sequence contains one (the
# curly right-quote is U+201D, three UTF-8 bytes, not 0x22), so `[^"]*` is unambiguous.
_CHARMAP_RE = re.compile(
    r'^charmap "(?P<seq>[^"]*)", \$(?P<tile>[0-9A-Fa-f]{2})\s*(?:;.*)?$'
)


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_charmap"


# --- Parse + assert -----------------------------------------------------------------------------

def parse_charmap(text: str, path: Path) -> list[tuple[str, int]]:
    """Parse `charmap "<seq>", $HH` lines into [(sequence, tile)] and assert the source contract."""
    rows: list[tuple[str, int]] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        match = _CHARMAP_RE.match(line)
        if not match:
            raise ParseError(
                f'{path}:{lineno}: line does not match `charmap "<seq>", $HH`: {line!r}'
            )
        rows.append((match.group("seq"), int(match.group("tile"), 16)))

    _assert_contract(rows, path)
    return rows


def _assert_contract(rows: list[tuple[str, int]], path: Path) -> None:
    if len(rows) != ENTRY_COUNT:
        raise ParseError(f"{path}: found {len(rows)} charmap entries, expected {ENTRY_COUNT}")

    sequences = [seq for seq, _ in rows]
    tiles = [tile for _, tile in rows]

    dup_seq = _first_duplicate(sequences)
    if dup_seq is not None:
        raise ParseError(f"{path}: duplicate sequence {dup_seq!r}")
    dup_tile = _first_duplicate(tiles)
    if dup_tile is not None:
        raise ParseError(f"{path}: duplicate tile value ${dup_tile:02X}")

    table = dict(rows)

    # Digit-identity anchor: "0".."9" -> $00..$09 in order (load-bearing for BCD score rendering).
    for i, ch in enumerate(DIGITS):
        if table.get(ch) != DIGIT_BASE + i:
            raise ParseError(
                f"{path}: digit-identity anchor broken: {ch!r} -> "
                f"{_fmt(table.get(ch))}, expected ${DIGIT_BASE + i:02X}"
            )
    # Letter anchor: "a".."z" -> $0A..$23 in order (contiguous).
    for i, ch in enumerate(LETTERS):
        if table.get(ch) != LETTER_BASE + i:
            raise ParseError(
                f"{path}: letter anchor broken: {ch!r} -> "
                f"{_fmt(table.get(ch))}, expected ${LETTER_BASE + i:02X}"
            )

    # Ligature anchor: exactly one multi-code-point sequence, the ".<U+201D>" ligature -> $9D.
    # RGBDS resolves string literals by greedy longest-match, so this ligature must win over "."
    # when encoding text; the encoder relies on it being the sole multi-code-point key.
    multi = {seq for seq in sequences if len(seq) > 1}
    if multi != {LIGATURE_SEQUENCE}:
        raise ParseError(
            f"{path}: multi-code-point sequences are {sorted(multi)!r}, expected exactly "
            f"[{LIGATURE_SEQUENCE!r}] (the ligature) - the encoder's longest-match contract changed"
        )
    if table[LIGATURE_SEQUENCE] != LIGATURE_TILE:
        raise ParseError(
            f"{path}: ligature {LIGATURE_SEQUENCE!r} -> {_fmt(table[LIGATURE_SEQUENCE])}, "
            f"expected ${LIGATURE_TILE:02X}"
        )

    # Every sequence must have a Tile enumerator name — digits and letters derive mechanically,
    # everything else must be in SYMBOL_NAMES. Raises with the offending sequence.
    for seq, _ in rows:
        tile_name(seq, path)


def _first_duplicate(items):
    seen = set()
    for item in items:
        if item in seen:
            return item
        seen.add(item)
    return None


def _fmt(value) -> str:
    return "missing" if value is None else f"${value:02X}"


# --- Emit ---------------------------------------------------------------------------------------

def tile_name(seq: str, path: Path) -> str:
    """The CharTile enumerator name for a charmap sequence. Hard error on an unnamed sequence."""
    if len(seq) == 1 and "0" <= seq <= "9":
        return f"DIGIT_{seq}"
    if len(seq) == 1 and "a" <= seq <= "z":
        return f"LETTER_{seq.upper()}"
    name = SYMBOL_NAMES.get(seq)
    if name is None:
        raise ParseError(
            f"{path}: sequence {seq!r} has no CharTile enumerator name - add it to SYMBOL_NAMES"
        )
    return name


def cpp_byte_string(seq: str) -> str:
    """Render `seq` as one or more concatenated C++ string literals containing only ASCII bytes.

    Printable-ASCII bytes go verbatim (with `"` and `\\` escaped); every other byte becomes a
    two-digit `\\xHH` escape. C++ hex escapes are greedy, so when a `\\xHH` escape would be
    followed by a literal ASCII hex-digit character, the string is split (`"\\xE2" "a"`) to keep
    each escape exactly two digits. (Unreached by this corpus, but guarded.)
    """
    out = ['"']
    prev_was_hex_escape = False
    for byte in seq.encode("utf-8"):
        if byte == 0x22:      # "
            out.append('\\"')
            prev_was_hex_escape = False
        elif byte == 0x5C:    # backslash
            out.append("\\\\")
            prev_was_hex_escape = False
        elif 0x20 <= byte <= 0x7E:
            ch = chr(byte)
            if prev_was_hex_escape and ch in _HEX_DIGITS:
                out.append('" "')  # terminate the previous \xHH before an ASCII hex digit
            out.append(ch)
            prev_was_hex_escape = False
        else:
            out.append(f"\\x{byte:02X}")
            prev_was_hex_escape = True
    out.append('"')
    return "".join(out)


def readable_comment(seq: str) -> str:
    """Human-readable rendering of `seq`: printable ASCII verbatim, else `<U+XXXX>` per code point."""
    parts = []
    for cp in seq:
        parts.append(cp if 0x20 <= ord(cp) <= 0x7E else f"<U+{ord(cp):04X}>")
    return "".join(parts)


def _emit_rows(rows: list[tuple[str, int]], indent: str, path: Path, typed: bool) -> str:
    """Designated-initializer rows. typed=True names the tile as a Tile enumerator (engine table);
    typed=False keeps the raw byte (test fixture — independent of tile.h by design)."""
    lines = []
    for seq, tile in rows:
        value = f"CharTile::{tile_name(seq, path)}" if typed else f"0x{tile:02X}"
        lines.append(
            f'{indent}{{ .sequence = {cpp_byte_string(seq)}, .tile = {value} }},'
            f'  // "{readable_comment(seq)}" -> $'f'{tile:02X}'
        )
    return "\n".join(lines)


def emit_inc(rows: list[tuple[str, int]], source_commit: str,
             path: Path = Path("charmap.asm")) -> str:
    return f"""{common.banner("parse_charmap.py", source_commit)}\
// Included inside the `constexpr std::array<CharmapEntry, {ENTRY_COUNT}> kCharmap` initializer in
// src/data/charmap.cpp. Source order (charmap.asm line order); the encoder is order-independent.
// Tiles are named: the charmap is the ROM's symbol table for the text glyph space, so each row's
// tile is the CharTile enumerator (include/kirpich/char_tile.h) minted from this same source.
{_emit_rows(rows, "    ", path, typed=True)}
"""


def emit_enum(rows: list[tuple[str, int]], source_commit: str,
              path: Path = Path("charmap.asm")) -> str:
    width = max(len(tile_name(seq, path)) for seq, _ in rows)
    body = "\n".join(
        f'    {tile_name(seq, path):<{width}} = 0x{tile:02X},  // "{readable_comment(seq)}"'
        for seq, tile in rows
    )
    return f"""#pragma once
{common.banner("parse_charmap.py", source_commit)}\
// CharTile - a text glyph's VRAM tile index, named. The character map (charmap.asm) is the ROM's
// naming table for the text glyph space: every sequence it maps names the glyph at its tile index,
// and those names are minted here as enumerators, byte values preserved. The tile sheet holds
// further glyphs the charmap never names (used directly by tilemaps and code); enumerators for
// those are added as the surfaces that use them land, so this enum grows with the port.

#include <cstdint>

namespace kirpich {{

enum class CharTile : std::uint8_t {{
{body}
}};

}}  // namespace kirpich
"""


def emit_fixture(rows: list[tuple[str, int]], source_commit: str,
                 path: Path = Path("charmap.asm")) -> str:
    return f"""#pragma once
{common.banner("parse_charmap.py", source_commit)}\
// Independent fixture for the full-corpus charmap sweep. Mirrors the {ENTRY_COUNT} rows the engine
// table holds, in its own struct with RAW byte tiles - independent of both include/kirpich/charmap.h
// and include/kirpich/tile.h, so a defect in either cannot mask the sweep. tests/test_charmap.cpp
// asserts the engine table (and therefore each Tile enumerator's value) equals these bytes.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

struct CharmapRow {{
    std::string_view sequence;  // UTF-8 bytes exactly as upstream; 1..4 bytes, 1..2 code points
    std::uint8_t     tile;      // VRAM tile index this sequence encodes to, as a raw byte
}};

inline constexpr std::array<CharmapRow, {ENTRY_COUNT}> kCharmapExpected{{{{
{_emit_rows(rows, "    ", path, typed=False)}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(
            f"{label}: emitted output contains a non-ASCII byte "
            f"(U+{ord(bad):04X}) - the emitter must escape every non-ASCII byte"
        )


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's charmap table + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    parser.add_argument("--enum-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    charmap_path = source_root / "charmap.asm"
    if not charmap_path.is_file():
        print(f"parse_charmap: source file not found: {charmap_path}", file=sys.stderr)
        return 2

    try:
        text = charmap_path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ParseError(f"{charmap_path}: not valid UTF-8: {exc}")

    rows = parse_charmap(text, charmap_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(rows, commit, charmap_path),
        args.fixture_out: emit_fixture(rows, commit, charmap_path),
        args.enum_out: emit_enum(rows, commit, charmap_path),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        _assert_ascii(content, str(out_path))
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_charmap: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_charmap: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
