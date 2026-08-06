#!/usr/bin/env python3
"""Parser for Kirpich's sprite layout grids - the shared (y, x) pixel-offset frames.

The disassembly holds five composite-sprite layout grids in one contiguous, unnamed section
(`SECTION "TODO Name", ROM0[$31A9]`, tetris.asm). Each grid is a run of `db` bytes forming (y, x)
offset pairs; the sprite renderer walks a sprite's tile list and one of these grids in lockstep,
adding one pair per drawn cell to place each OAM entry. The grids are shared geometry - many kinds
of sprite (piece sprites, characters, the shuttle, rockets, digits, labels) reference the same
frames - not piece-specific rotation data.

Provenance: five hand-labelled runs of plain `db` bytes with no symbol set, so there is no enum
header. Every byte is a multiple of 8 in [0x00, 0x38] and pairs are atomic. Emission set = the data
`.inc` (five full array definitions) + the test fixture (the same five arrays, independent).

  src/data/generated/sprite_grids_data.inc   five `inline constexpr std::array<SpriteGridOffset, N>`
                                             definitions, included at namespace scope in
                                             src/data/sprite_grids.h
  tests/fixtures/sprite_grids_expected.h      the same five arrays as kExpectedSpriteGrid<Geom>, in
                                             an independent fixture, so a defect in the engine
                                             header cannot mask the sweep

The five table byte-counts are self-checking: four are the address deltas encoded in the upstream
label suffixes ($31C9-$31A9=32, etc.); the last is fixed at 18 bytes because its terminal boundary
(GameplayTiles::) carries no address suffix.

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

# Upstream label -> its ROM address (from the label's hex suffix). Order is load-bearing: the five
# tables appear in exactly this sequence inside the section.
TABLES = [
    ("Matrix_31A9", 0x31A9),
    ("Matrix_31C9", 0x31C9),
    ("Matrix_31D9", 0x31D9),
    ("Matrix_31F5", 0x31F5),
    ("Matrix_322D", 0x322D),
]

# The section that holds the grids, and the label that terminates the last one.
SECTION_TEXT = 'SECTION "TODO Name", ROM0[$31A9]'
TERMINAL_LABEL = "GameplayTiles"

# Matrix_322D's byte count. Unlike the first four it cannot be an address delta - the next label
# (GameplayTiles::) has no hex suffix. $323F - $322D = 18.
LAST_TABLE_BYTES = 18

# C++ emission: table name, geometry slug, element count N. Index-aligned with TABLES.
EMIT = [
    ("kSpriteGrid4x4",        "4x4",        16),
    ("kSpriteGrid1x8",        "1x8",         8),
    ("kSpriteGrid7x2",        "7x2",        14),
    ("kSpriteGrid8x4Notched", "8x4Notched", 28),
    ("kSpriteGrid3x3",        "3x3",         9),
]

BYTE_STEP = 8       # every offset is a multiple of 8 (one 8x8 tile)
BYTE_MAX = 0x38     # largest offset the grids use

_SECTION_RE = re.compile(r'^SECTION "TODO Name", ROM0\[\$31A9\]$')
_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)::')
_DB_RE = re.compile(r'^db\s+(?P<bytes>.+)$')
_BYTE_RE = re.compile(r'^\$([0-9A-Fa-f]{2})$')


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_sprite_grids"


# --- Parse + assert -----------------------------------------------------------------------------

def parse_sprite_grids(text: str, path: Path) -> list[list[tuple[int, int]]]:
    """Parse the five grids into per-table lists of (y, x) pairs and assert the source contract."""
    lines = text.splitlines()

    section_idx = _find_section(lines, path)

    tables: dict[str, list[int]] = {}
    expect_idx = 0            # index into TABLES for the next label expected
    current: str | None = None
    saw_terminal = False

    for offset, raw_line in enumerate(lines[section_idx + 1:], start=section_idx + 2):
        line = raw_line.strip()
        if not line or line.startswith(";"):
            continue

        label_match = _LABEL_RE.match(line)
        if label_match:
            label = label_match.group(1)
            if label == TERMINAL_LABEL:
                if expect_idx != len(TABLES):
                    raise ParseError(
                        f"{path}:{offset}: reached {TERMINAL_LABEL}:: after only {expect_idx} of "
                        f"{len(TABLES)} tables"
                    )
                saw_terminal = True
                break
            if expect_idx >= len(TABLES) or label != TABLES[expect_idx][0]:
                expected = TABLES[expect_idx][0] if expect_idx < len(TABLES) else TERMINAL_LABEL
                raise ParseError(
                    f"{path}:{offset}: expected label {expected}::, found {label}::"
                )
            current = label
            tables[label] = []
            expect_idx += 1
            continue

        db_match = _DB_RE.match(line)
        if db_match:
            if current is None:
                raise ParseError(
                    f"{path}:{offset}: db line before any {TABLES[0][0]} label (section anchor "
                    f"must sit immediately before {TABLES[0][0]}::)"
                )
            tables[current].extend(_parse_db_bytes(db_match.group("bytes"), path, offset))
            continue

        raise ParseError(
            f"{path}:{offset}: unexpected line between grid tables (only db/blank/comment allowed): "
            f"{raw_line!r}"
        )

    if not saw_terminal:
        raise ParseError(
            f"{path}: reached end of file without the {TERMINAL_LABEL}:: terminal boundary"
        )
    if expect_idx != len(TABLES):
        raise ParseError(
            f"{path}: found {expect_idx} grid tables, expected {len(TABLES)}"
        )

    return [_finalise_table(i, tables[TABLES[i][0]], path) for i in range(len(TABLES))]


def _find_section(lines: list[str], path: Path) -> int:
    for i, line in enumerate(lines):
        if _SECTION_RE.match(line.strip()):
            return i
    raise ParseError(f"{path}: section anchor `{SECTION_TEXT}` not found")


def _parse_db_bytes(operand: str, path: Path, lineno: int) -> list[int]:
    operand = operand.split(";", 1)[0]  # drop any inline comment
    values: list[int] = []
    for token in operand.split(","):
        token = token.strip()
        if not token:
            raise ParseError(f"{path}:{lineno}: empty byte in db operand")
        match = _BYTE_RE.match(token)
        if not match:
            raise ParseError(f"{path}:{lineno}: not a two-digit hex byte: {token!r}")
        values.append(int(match.group(1), 16))
    return values


def _finalise_table(index: int, byte_list: list[int], path: Path) -> list[tuple[int, int]]:
    label, addr = TABLES[index]
    cpp_name, _geom, n = EMIT[index]

    if index + 1 < len(TABLES):
        expected_bytes = TABLES[index + 1][1] - addr
    else:
        expected_bytes = LAST_TABLE_BYTES

    # Internal consistency: the address delta must agree with the pre-locked element count.
    if expected_bytes != 2 * n:
        raise ParseError(
            f"{path}: {label} address delta implies {expected_bytes} bytes but {cpp_name} is "
            f"declared with N={n} ({2 * n} bytes)"
        )
    if len(byte_list) != expected_bytes:
        raise ParseError(
            f"{path}: {label} collected {len(byte_list)} bytes, expected {expected_bytes} "
            f"(${addr:04X} delta)"
        )
    if len(byte_list) % 2 != 0:
        raise ParseError(f"{path}: {label} has an odd byte count (pairs are atomic)")
    for i, byte in enumerate(byte_list):
        if byte % BYTE_STEP != 0:
            raise ParseError(
                f"{path}: {label} byte {i} = ${byte:02X} is not a multiple of {BYTE_STEP}"
            )
        if byte > BYTE_MAX:
            raise ParseError(
                f"{path}: {label} byte {i} = ${byte:02X} exceeds the grid maximum ${BYTE_MAX:02X}"
            )

    return [(byte_list[i], byte_list[i + 1]) for i in range(0, len(byte_list), 2)]


# --- Emit ---------------------------------------------------------------------------------------

def _emit_rows(pairs: list[tuple[int, int]], indent: str) -> str:
    return "\n".join(
        f"{indent}{{ .y = 0x{y:02X}, .x = 0x{x:02X} }}," for y, x in pairs
    )


def _emit_array(cpp_name: str, geom: str, n: int, label: str,
                pairs: list[tuple[int, int]]) -> str:
    return (
        f"// {cpp_name} - {label} ({geom} grid, {n} pairs).\n"
        f"inline constexpr std::array<SpriteGridOffset, {n}> {cpp_name}{{{{\n"
        f"{_emit_rows(pairs, '    ')}\n"
        f"}}}};\n"
    )


def emit_inc(tables: list[list[tuple[int, int]]], source_commit: str) -> str:
    arrays = "\n".join(
        _emit_array(cpp_name, geom, n, TABLES[i][0], tables[i])
        for i, (cpp_name, geom, n) in enumerate(EMIT)
    )
    return f"""{common.banner("parse_sprite_grids.py", source_commit)}\
// Included at namespace scope inside src/data/sprite_grids.h (inside `namespace kirpich`). Five
// composite-sprite layout grids; each element is one (y, x) pixel-offset pair added to a sprite's
// origin as the renderer walks the sprite's tile list.
{arrays}"""


def _emit_fixture_array(cpp_name: str, geom: str, n: int, label: str,
                        pairs: list[tuple[int, int]]) -> str:
    fixture_name = f"kExpectedSpriteGrid{geom}"
    return (
        f"// {fixture_name} - {label} ({geom}).\n"
        f"inline constexpr std::array<SpriteGridOffset, {n}> {fixture_name}{{{{\n"
        f"{_emit_rows(pairs, '    ')}\n"
        f"}}}};\n"
    )


def emit_fixture(tables: list[list[tuple[int, int]]], source_commit: str) -> str:
    arrays = "\n".join(
        _emit_fixture_array(cpp_name, geom, n, TABLES[i][0], tables[i])
        for i, (cpp_name, geom, n) in enumerate(EMIT)
    )
    return f"""#pragma once
{common.banner("parse_sprite_grids.py", source_commit)}\
// Independent fixture for the full-corpus sprite-grid sweep. Mirrors the five grids the engine
// header holds, in its own arrays, so a defect in src/data/sprite_grids.h cannot mask the sweep.
// tests/test_sprite_grids.cpp asserts the engine arrays equal these element-for-element.

#include <array>

#include "data/sprite_grids.h"

namespace kirpich::fixtures {{

{arrays}}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's sprite layout grids + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    asm_path = source_root / "tetris.asm"
    if not asm_path.is_file():
        print(f"parse_sprite_grids: source file not found: {asm_path}", file=sys.stderr)
        return 2

    text = asm_path.read_bytes().decode("utf-8")
    tables = parse_sprite_grids(text, asm_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(tables, commit),
        args.fixture_out: emit_fixture(tables, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_sprite_grids: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_sprite_grids: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
