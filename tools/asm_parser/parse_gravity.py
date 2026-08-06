#!/usr/bin/env python3
"""Parser for Kirpich's gravity table - the frames between automatic piece drops per level.

The disassembly holds the table as 21 decimal `db` rows under `FramesPerDropTable::` (tetris.asm).
Row N is the number of frames a piece waits before gravity pulls it down one cell at level N, so the
array position IS the level and the table's only reader indexes it directly. Upstream annotates
three of the rows with their level (`; Level 0`, `; Level 10`, `; Level 20`); those comments make the
row positions self-checking against the source and this parser asserts all three.

Emission set = the data `.inc` (the 21 designated-initializer rows) + the test fixture (the same 21
values as raw bytes, independent of the typed surface):

  src/data/generated/gravity_data.inc     `{ .level = N, .frames = F },` rows, included inside the
                                          kFramesPerDrop initializer in src/data/gravity.h
  tests/fixtures/gravity_expected.h       kExpectedFramesPerDrop, a plain std::array<std::uint8_t,
                                          21>, so a defect in the engine header cannot mask the
                                          full-corpus sweep

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

TABLE_LABEL = "FramesPerDropTable"

# Levels 0-20 inclusive. The level-up path gates at 20 (`cp $14 / ret z`), so the table is the whole
# reachable domain, not a prefix of a larger one.
ROW_COUNT = 21

# Upstream's own index annotations, by row. Every one of them must be present and exact - they are
# what pins each collected row to the level the source says it is.
COMMENT_ANCHORS = {
    0: "Level 0",
    10: "Level 10",
    20: "Level 20",
}

BYTE_MAX = 0xFF

# C++ emission.
CPP_TABLE = "kFramesPerDrop"
CPP_FIXTURE = "kExpectedFramesPerDrop"
FIXTURE_ROW_WIDTH = 7  # 21 values render as three self-labelled rows of seven

_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_.]*)::")
_DB_RE = re.compile(r"^db\s+(?P<value>\S+)\s*$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_gravity"


# --- Parse + assert -----------------------------------------------------------------------------

def parse_gravity(text: str, path: Path) -> list[int]:
    """Parse FramesPerDropTable into 21 byte values and assert the source contract."""
    lines = text.splitlines()

    label_idx = _find_table_label(lines, path)

    values: list[int] = []
    saw_terminal = False

    for offset, raw_line in enumerate(lines[label_idx + 1:], start=label_idx + 2):
        line = raw_line.strip()
        if not line or line.startswith(";"):
            continue

        if _LABEL_RE.match(line):
            saw_terminal = True
            break

        body, comment = _split_comment(line)
        db_match = _DB_RE.match(body)
        if not db_match:
            raise ParseError(
                f"{path}:{offset}: unexpected line inside {TABLE_LABEL} (only a single-value db, "
                f"blank, or comment allowed): {raw_line!r}"
            )

        row = len(values)
        if row >= ROW_COUNT:
            raise ParseError(
                f"{path}:{offset}: {TABLE_LABEL} has more than {ROW_COUNT} rows"
            )
        _assert_anchor(row, comment, path, offset)
        values.append(_parse_decimal_byte(db_match.group("value"), path, offset))

    if not saw_terminal:
        raise ParseError(
            f"{path}: reached end of file without a label terminating {TABLE_LABEL}"
        )
    if len(values) != ROW_COUNT:
        raise ParseError(
            f"{path}: {TABLE_LABEL} collected {len(values)} rows, expected {ROW_COUNT}"
        )

    return values


def _find_table_label(lines: list[str], path: Path) -> int:
    """Index of the sole `FramesPerDropTable::` line. Absent or duplicated is a hard error."""
    hits = [i for i, line in enumerate(lines)
            if _LABEL_RE.match(line.strip()) and line.strip().startswith(f"{TABLE_LABEL}::")]
    if not hits:
        raise ParseError(f"{path}: label {TABLE_LABEL}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {TABLE_LABEL}:: defined more than once (lines {found})")
    return hits[0]


def _split_comment(line: str) -> tuple[str, str]:
    """Split a source line into its directive and its trailing comment text (without the `;`)."""
    body, sep, comment = line.partition(";")
    return body.strip(), comment.strip() if sep else ""


def _assert_anchor(row: int, comment: str, path: Path, lineno: int) -> None:
    expected = COMMENT_ANCHORS.get(row)
    if expected is None:
        return
    if comment != expected:
        found = f"'; {comment}'" if comment else "no comment"
        raise ParseError(
            f"{path}:{lineno}: expected '; {expected}' anchor on row {row}, found {found}"
        )


def _parse_decimal_byte(token: str, path: Path, lineno: int) -> int:
    if not token.isdigit():
        raise ParseError(f"{path}:{lineno}: not a decimal byte: {token!r}")
    value = int(token, 10)
    if value > BYTE_MAX:
        raise ParseError(f"{path}:{lineno}: value {value} exceeds a byte (max {BYTE_MAX})")
    return value


# --- Emit ---------------------------------------------------------------------------------------

def emit_inc(values: list[int], source_commit: str) -> str:
    rows = "\n".join(
        f"{{ .level = {level:2d}, .frames = {frames:2d} }},"
        for level, frames in enumerate(values)
    )
    return f"""{common.banner("parse_gravity.py", source_commit)}\
// Included inside the {CPP_TABLE} initializer in src/data/gravity.h (inside `namespace kirpich`).
// One row per level 0-20: the frames a piece waits between automatic gravity drops at that level.
{rows}
"""


def _fixture_rows(values: list[int]) -> str:
    lines = []
    for start in range(0, len(values), FIXTURE_ROW_WIDTH):
        chunk = values[start:start + FIXTURE_ROW_WIDTH]
        cells = ", ".join(f"{value:2d}" for value in chunk)
        last = start + len(chunk) - 1
        lines.append(f"    {cells},  // levels {start}-{last}")
    return "\n".join(lines)


def emit_fixture(values: list[int], source_commit: str) -> str:
    return f"""#pragma once
{common.banner("parse_gravity.py", source_commit)}\
// Independent fixture for the full-corpus gravity sweep: the {ROW_COUNT} ROM bytes as plain
// integers, indexed by level. Deliberately holds no port type, so a defect in src/data/gravity.h
// cannot mask the sweep in tests/test_gravity.cpp.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

inline constexpr std::array<std::uint8_t, {ROW_COUNT}> {CPP_FIXTURE}{{{{
{_fixture_rows(values)}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's gravity table + fixture.")
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
        print(f"parse_gravity: source file not found: {asm_path}", file=sys.stderr)
        return 2

    text = asm_path.read_bytes().decode("utf-8")
    values = parse_gravity(text, asm_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(values, commit),
        args.fixture_out: emit_fixture(values, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_gravity: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_gravity: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
