#!/usr/bin/env python3
"""Parser for Kirpich's playing-field geometry and its wipe schedule.

The disassembly does not store the field wipe as a table. It stores 18 near-identical VBlank
routines, PlayingFieldWipe02 through PlayingFieldWipe19 (tetris.asm; upstream's own comment above
them: "Absolute garbage. I wonder if they used a macro..."). Each routine gates on hWipeCounter
equal to its own number and, when it matches, copies one 10-byte field row from the shadow field in
WRAM to the BG map in VRAM via WipePlayingFieldRow, which then increments the counter. VBlank calls
all 18 every frame in descending order, so at most one body runs per frame and the field redraws one
row per frame, bottom to top, over 18 frames.

The only per-routine data is one address pair, and all 18 are exactly closed-form:

    VRAM destination = $9802 + (19 - counter) * $20   (BG map, field origin at row 0 column 2)
    WRAM source      = $C802 + (19 - counter) * $20   (shadow field, same row stride)

Those addresses describe the DMG memory map - a mechanism the port does not replicate - so they are
pinned here and recorded in the test fixture, but they are not part of the emitted port surface.
What the port surface needs is the composition: the field's extent and the counter's domain.

Emission set = the constants `.inc` + the test fixture:

  src/data/generated/playing_field_data.inc   the four namespace-scope constants (rows, cols, the
                                              counter's first and last value), included inside
                                              `namespace kirpich` by src/data/playing_field.h
  tests/fixtures/playing_field_expected.h     kExpectedPlayingFieldWipes, the 18 raw (counter, vram,
                                              wram) triples, independent of the port surface so the
                                              closed-form sweep checks source values not a re-derivation

Every value is read from the source and every structural expectation is asserted as it is read;
any deviation is a hard error with a file:line citation, never silently accepted.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

# The wipe routines are numbered by the counter value that fires them: 2 (bottom row) through 19
# (top row), contiguous. 18 routines, one per field row.
COUNTER_FIRST = 2
COUNTER_LAST = 19
WIPE_COUNT = COUNTER_LAST - COUNTER_FIRST + 1  # 18

# The closed form every routine's address pair obeys. Base = the field's top row; each lower row is
# one BG-map row ($20 bytes) further on. VRAM is the BG map, WRAM the shadow field copied from.
VRAM_BASE = 0x9802
WRAM_BASE = 0xC802
ROW_STRIDE = 0x20

COUNTER_VAR = "hWipeCounter"
WIPE_ROW_LABEL = "WipePlayingFieldRow"

# C++ emission.
CPP_FIXTURE = "kExpectedPlayingFieldWipes"
CPP_FIXTURE_ROW = "PlayingFieldWipeExpectedRow"

_TOP_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
_WIPE_LABEL_RE = re.compile(r"^PlayingFieldWipe(\d{2})::")

# Gate: `ldh a, [hWipeCounter]` / `cp a, <NN>` / `ret nz`.
_LDH_LOAD_COUNTER_RE = re.compile(rf"^ldh\s+a,\s*\[{COUNTER_VAR}\]$")
_CP_RE = re.compile(r"^cp\s+a,\s*(\d+)$")
_RET_NZ_RE = re.compile(r"^ret\s+nz$")

# The row-copy skeleton: one `ld hl, $XXXX` + one `ld de, $XXXX` + `call WipePlayingFieldRow`.
_LD_HL_RE = re.compile(r"^ld\s+hl,\s*\$([0-9A-Fa-f]{4})$")
_LD_DE_RE = re.compile(r"^ld\s+de,\s*\$([0-9A-Fa-f]{4})$")
_CALL_WIPE_ROW_RE = re.compile(rf"^call\s+{WIPE_ROW_LABEL}$")

# WipePlayingFieldRow's own anchors: the row width, and the counter increment.
_LD_B_RE = re.compile(r"^ld\s+b,\s*(\d+)$")
_INC_A_RE = re.compile(r"^inc\s+a$")
_LDH_STORE_COUNTER_RE = re.compile(rf"^ldh\s+\[{COUNTER_VAR}\],\s*a$")
_XOR_A_RE = re.compile(r"^xor\s+a$")

# The VBlank dispatch: `call PlayingFieldWipe<NN>`.
_CALL_WIPE_DISPATCH_RE = re.compile(r"^call\s+PlayingFieldWipe(\d{2})$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_playing_field"


# --- Pure helpers (the closed form) -------------------------------------------------------------

def expected_vram(counter: int) -> int:
    """The BG-map destination address the wipe at this counter copies to."""
    return VRAM_BASE + (COUNTER_LAST - counter) * ROW_STRIDE


def expected_wram(counter: int) -> int:
    """The shadow-field source address the wipe at this counter copies from."""
    return WRAM_BASE + (COUNTER_LAST - counter) * ROW_STRIDE


def _split_comment(line: str) -> str:
    """The directive on a source line, without its trailing `; comment` and surrounding space."""
    body, _sep, _comment = line.partition(";")
    return body.strip()


def _instructions(lines: list[str], label_idx: int) -> list[tuple[int, str]]:
    """Meaningful lines of the routine at label_idx: from just after the label to the next
    top-level label (or EOF). Blank and comment-only lines drop out; each survivor is returned as
    (1-based line number, directive text with any trailing comment stripped)."""
    out: list[tuple[int, str]] = []
    for offset, raw in enumerate(lines[label_idx + 1:], start=label_idx + 2):
        stripped = raw.strip()
        if _TOP_LABEL_RE.match(stripped):
            break
        body = _split_comment(stripped)
        if not body:
            continue
        out.append((offset, body))
    return out


# --- Parse + assert -----------------------------------------------------------------------------

def parse_playing_field(text: str, path: Path) -> dict:
    """Parse the wipe routines, WipePlayingFieldRow, and the VBlank dispatch. Returns
    {"rows": [(counter, vram, wram), ...] ascending by counter, "cols": int}."""
    lines = text.splitlines()

    wipe_labels = _collect_wipe_labels(lines, path)

    rows: list[tuple[int, int, int]] = []
    for counter, label_idx in wipe_labels:
        vram, wram = _parse_wipe_routine(lines, label_idx, counter, path)
        rows.append((counter, vram, wram))

    _assert_wipe19_reset(lines, path)
    cols = _parse_wipe_row_routine(lines, path)
    _assert_vblank_dispatch(lines, path)

    return {"rows": rows, "cols": cols}


def _collect_wipe_labels(lines: list[str], path: Path) -> list[tuple[int, int]]:
    """Every PlayingFieldWipeNN:: label in file order as (counter, line_index). Asserts there are
    exactly 18 and that their numbers are the contiguous ascending run 2..19."""
    found: list[tuple[int, int]] = []
    for idx, raw in enumerate(lines):
        match = _WIPE_LABEL_RE.match(raw.strip())
        if match:
            found.append((int(match.group(1)), idx))

    numbers = [counter for counter, _idx in found]
    if len(numbers) != WIPE_COUNT:
        raise ParseError(
            f"{path}: expected {WIPE_COUNT} PlayingFieldWipe routines, found {len(numbers)} "
            f"({numbers})"
        )
    expected = list(range(COUNTER_FIRST, COUNTER_LAST + 1))
    if numbers != expected:
        for got, want in zip(numbers, expected):
            if got != want:
                lineno = next(idx + 1 for counter, idx in found if counter == got)
                raise ParseError(
                    f"{path}:{lineno}: PlayingFieldWipe labels must be the contiguous ascending run "
                    f"{expected[0]:02d}..{expected[-1]:02d} in file order; expected {want:02d} "
                    f"here, found {got:02d}"
                )
        raise ParseError(f"{path}: PlayingFieldWipe label set {numbers} != {expected}")
    return found


def _parse_wipe_routine(lines: list[str], label_idx: int, counter: int,
                        path: Path) -> tuple[int, int]:
    """Assert one wipe routine's gate + row-copy skeleton, returning its (vram, wram) address pair.
    Documented per-routine extras (SFX tails, print tails, Wipe19's `ld [$C0C7], a` between the gate
    and the load) are tolerated as opaque code; the gate/hl/de/call skeleton is mandatory."""
    instrs = _instructions(lines, label_idx)
    label = f"PlayingFieldWipe{counter:02d}"

    # The gate is the first three instructions, in order.
    if len(instrs) < 3:
        raise ParseError(f"{path}: {label} is too short to hold its gate")
    _assert_match(_LDH_LOAD_COUNTER_RE, instrs[0], path,
                  f"{label} must open with `ldh a, [{COUNTER_VAR}]`")
    cp_lineno, cp_body = instrs[1]
    cp_match = _CP_RE.match(cp_body)
    if not cp_match:
        raise ParseError(f"{path}:{cp_lineno}: {label} gate must test `cp a, <n>`, found {cp_body!r}")
    if int(cp_match.group(1)) != counter:
        raise ParseError(
            f"{path}:{cp_lineno}: {label} gate tests `cp a, {cp_match.group(1)}` but the label "
            f"number is {counter}"
        )
    _assert_match(_RET_NZ_RE, instrs[2], path, f"{label} gate must be `ret nz` after the compare")

    # The row copy: exactly one `ld hl, $XXXX`, then one `ld de, $XXXX`, then `call WipeRow`.
    call_pos = _index_of(instrs, _CALL_WIPE_ROW_RE)
    if call_pos is None:
        raise ParseError(f"{path}: {label} never calls {WIPE_ROW_LABEL}")

    hl_hits = [(lineno, m) for lineno, m in _matches(_LD_HL_RE, instrs[3:call_pos])]
    de_hits = [(lineno, m) for lineno, m in _matches(_LD_DE_RE, instrs[3:call_pos])]
    if len(hl_hits) != 1:
        raise ParseError(
            f"{path}: {label} must load exactly one `ld hl, $XXXX` before {WIPE_ROW_LABEL}, "
            f"found {len(hl_hits)}"
        )
    if len(de_hits) != 1:
        raise ParseError(
            f"{path}: {label} must load exactly one `ld de, $XXXX` before {WIPE_ROW_LABEL}, "
            f"found {len(de_hits)}"
        )
    hl_lineno, hl_match = hl_hits[0]
    de_lineno, de_match = de_hits[0]
    if hl_lineno > de_lineno:
        raise ParseError(f"{path}:{hl_lineno}: {label} loads hl after de; expected hl then de")

    vram = int(hl_match.group(1), 16)
    wram = int(de_match.group(1), 16)

    want_vram = expected_vram(counter)
    want_wram = expected_wram(counter)
    if vram != want_vram:
        raise ParseError(
            f"{path}:{hl_lineno}: {label} hl=${vram:04X}, expected ${want_vram:04X} "
            f"(${VRAM_BASE:04X} + {COUNTER_LAST - counter}*${ROW_STRIDE:02X})"
        )
    if wram != want_wram:
        raise ParseError(
            f"{path}:{de_lineno}: {label} de=${wram:04X}, expected ${want_wram:04X} "
            f"(${WRAM_BASE:04X} + {COUNTER_LAST - counter}*${ROW_STRIDE:02X})"
        )
    return vram, wram


def _assert_wipe19_reset(lines: list[str], path: Path) -> None:
    """PlayingFieldWipe19 (the top row) must clear the counter: `xor a` then `ldh [hWipeCounter], a`
    as consecutive instructions, after its row copy."""
    label_idx = _find_label(lines, "PlayingFieldWipe19", path)
    instrs = _instructions(lines, label_idx)
    call_pos = _index_of(instrs, _CALL_WIPE_ROW_RE)
    tail = instrs[call_pos + 1:] if call_pos is not None else instrs
    if not _has_consecutive(tail, (_XOR_A_RE, _LDH_STORE_COUNTER_RE)):
        raise ParseError(
            f"{path}: PlayingFieldWipe19 must reset the counter with `xor a` / "
            f"`ldh [{COUNTER_VAR}], a` after its row copy"
        )


def _parse_wipe_row_routine(lines: list[str], path: Path) -> int:
    """Assert WipePlayingFieldRow's row width (`ld b, <n>`) and its counter increment
    (`ldh a, [hWipeCounter]` / `inc a` / `ldh [hWipeCounter], a`). Returns the row width."""
    label_idx = _find_label(lines, WIPE_ROW_LABEL, path)
    instrs = _instructions(lines, label_idx)

    cols = None
    for lineno, body in instrs:
        match = _LD_B_RE.match(body)
        if match:
            cols = int(match.group(1))
            break
    if cols is None:
        raise ParseError(f"{path}: {WIPE_ROW_LABEL} has no `ld b, <n>` row-width load")

    if not _has_consecutive(instrs, (_LDH_LOAD_COUNTER_RE, _INC_A_RE, _LDH_STORE_COUNTER_RE)):
        raise ParseError(
            f"{path}: {WIPE_ROW_LABEL} must increment the counter with "
            f"`ldh a, [{COUNTER_VAR}]` / `inc a` / `ldh [{COUNTER_VAR}], a`"
        )
    return cols


def _assert_vblank_dispatch(lines: list[str], path: Path) -> None:
    """Every `call PlayingFieldWipe<NN>` in file order must be the 18-long descending run 19..02."""
    calls = [int(m.group(1))
             for m in (_CALL_WIPE_DISPATCH_RE.match(_split_comment(raw.strip())) for raw in lines)
             if m]
    expected = list(range(COUNTER_LAST, COUNTER_FIRST - 1, -1))
    if calls != expected:
        raise ParseError(
            f"{path}: the VBlank wipe dispatch must call PlayingFieldWipe19..02 in descending "
            f"order; found {calls}"
        )


# --- Small structural utilities -----------------------------------------------------------------

def _find_label(lines: list[str], label: str, path: Path) -> int:
    """Line index of the sole `<label>::`. Absent or duplicated is a hard error."""
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label}:: defined more than once (lines {found})")
    return hits[0]


def _assert_match(regex: re.Pattern, instr: tuple[int, str], path: Path, what: str) -> None:
    lineno, body = instr
    if not regex.match(body):
        raise ParseError(f"{path}:{lineno}: {what}; found {body!r}")


def _index_of(instrs: list[tuple[int, str]], regex: re.Pattern) -> int | None:
    for i, (_lineno, body) in enumerate(instrs):
        if regex.match(body):
            return i
    return None


def _matches(regex: re.Pattern, instrs: list[tuple[int, str]]):
    for lineno, body in instrs:
        match = regex.match(body)
        if match:
            yield lineno, match


def _has_consecutive(instrs: list[tuple[int, str]], regexes: tuple[re.Pattern, ...]) -> bool:
    """True if some run of consecutive instructions matches regexes in order."""
    bodies = [body for _lineno, body in instrs]
    for start in range(len(bodies) - len(regexes) + 1):
        if all(regex.match(bodies[start + k]) for k, regex in enumerate(regexes)):
            return True
    return False


# --- Emit ---------------------------------------------------------------------------------------

def emit_inc(result: dict, source_commit: str) -> str:
    rows = result["rows"]
    rows_count = len(rows)
    cols = result["cols"]
    first = rows[0][0]
    last = rows[-1][0]
    return f"""{common.banner("parse_playing_field.py", source_commit)}\
// Included at namespace scope in src/data/playing_field.h (inside `namespace kirpich`).
// The playing field's fixed extent and the wipe counter's domain, read from the per-row wipe
// routines (PlayingFieldWipe02..19) and WipePlayingFieldRow in the disassembly.
inline constexpr std::uint8_t kPlayingFieldRows = {rows_count};
inline constexpr std::uint8_t kPlayingFieldCols = {cols};
inline constexpr std::uint8_t kPlayingFieldWipeCounterFirst = {first};
inline constexpr std::uint8_t kPlayingFieldWipeCounterLast = {last};
"""


def _fixture_rows(rows: list[tuple[int, int, int]]) -> str:
    return "\n".join(
        f"    {{ .counter = {counter:2d}, .vram = 0x{vram:04X}, .wram = 0x{wram:04X} }},"
        for counter, vram, wram in rows
    )


def emit_fixture(result: dict, source_commit: str) -> str:
    rows = result["rows"]
    rows_count = len(rows)
    return f"""#pragma once
{common.banner("parse_playing_field.py", source_commit)}\
// Independent fixture for the full-corpus playing-field wipe sweep: the {rows_count} address triples
// (counter, VRAM destination, WRAM source) exactly as the original's wipe routines hold them. It
// keeps the raw ROM serialization - addresses the port itself does not use - so the closed-form
// checks in tests/test_playing_field.cpp verify the geometry against source values, not against the
// port's own re-derivation of them.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// One wipe routine's data: the counter value that fires it, and the DMG addresses it copies a
// 10-byte field row between (BG-map destination <- shadow-field source).
struct {CPP_FIXTURE_ROW} {{
    std::uint8_t  counter;  // hWipeCounter value that triggers this row copy
    std::uint16_t vram;     // BG-map destination address in the original
    std::uint16_t wram;     // shadow-field source address in the original
}};

inline constexpr std::array<{CPP_FIXTURE_ROW}, {rows_count}> {CPP_FIXTURE}{{{{
{_fixture_rows(rows)}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Emit Kirpich's playing-field geometry constants + wipe fixture.")
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
        print(f"parse_playing_field: source file not found: {asm_path}", file=sys.stderr)
        return 2

    text = asm_path.read_bytes().decode("utf-8")
    result = parse_playing_field(text, asm_path)
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
        print(f"parse_playing_field: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_playing_field: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
