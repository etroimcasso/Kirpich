#!/usr/bin/env python3
"""Parser for Kirpich's garbage-fill data - the Type B demo garbage table and the constants the
procedural garbage fill and its call sites consume.

A Type B game starts with rows of "garbage": partly-filled rows the player digs out from under. Two
paths produce them in the original. During the attract-mode demo the garbage cannot be random, so the
game stamps a fixed 40-byte table, TypeBDemoGarbage (4 rows x 10 cells). Everywhere else InitGarbage
fills the rows procedurally, using the DIV register as an RNG. This unit ports the one data table and
the handful of constants the fill and its three call sites consume; the fill routine itself and the
demo stamp are RNG/timing-dependent gameplay logic and port later, so their memory-map operands are
pinned in the parser's asserts and recorded in docs/contracts/garbage-init.md, not emitted here.

The cell space is raw tile indices - a space ($2F, the charmap's " ") or one of the eight block tiles
($80-$87) - with no upstream names, so there is no enum: every cell is a std::uint8_t (the tilemaps
precedent). The emitted surface is the composed 4 x 10 grid plus six constants read from the routines
and call sites.

Emission set = the data `.inc` + the test fixture:

  src/data/generated/garbage_data.inc   the six constants + the composed 4x10 grid, included inside
                                        `namespace kirpich` by src/data/garbage.h
  tests/fixtures/garbage_expected.h     the flat 40-byte array in serialization order, independent of
                                        the composed grid so a defect in the header cannot mask the sweep

The empty tile is resolved through charmap.asm (the character map already ported) so the $2F value is
tied to the same table the rest of the port decodes text with. The charmap parse is reused from
parse_charmap (its 47-entry contract is asserted there); only the " " lookup is used here.

Every value is read from the source and every structural expectation is asserted as it is read; any
deviation is a hard error with a file:line citation, never silently accepted.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import common
import parse_charmap

# --- Expected structure (the source contract this parser asserts) -------------------------------

# The demo garbage table: 4 rows of 10 cells. The row count is cross-checked against InitDemoGarbage's
# `ld c, 4`; the width is the playing field's (kPlayingFieldCols), cross-checked against the stamp's
# inner `ld b, 10`.
EXPECTED_DEMO_ROWS = 4
FIELD_COLS = 10                 # the playing-field contract; the demo table is field-width by construction

# The block-tile range: $80 through $87, eight tiles. A garbage cell is one of these or the empty tile.
BLOCK_TILE_BASE = 0x80          # `or a, $80` in InitGarbage's tile pick (and its `ld a, $80` block arm)
BLOCK_TILE_MASK = 0x07          # `and a, $07`; the tile count is this + 1
BLOCK_TILE_COUNT = BLOCK_TILE_MASK + 1

# Memory-map / stride operands the parser pins but does not emit (they describe the DMG memory map and
# the fill's mechanism; the routine bodies port with the gameplay logic).
ROW_STRIDE = 0x20               # one BG-map row; the negative call-site offsets are multiples of it
DEMO_DEST = 0x99C2              # InitDemoGarbage destination (field row 14)
DEMO_ROW_STRIDE = 0x0020        # InitDemoGarbage row advance
DEMO_BUFFER_SWITCH = 0x30       # `add a, $30` high-byte switch to the field buffer mirror
TYPEB_DEST = 0x9A02             # Type B call-site destination (field row 16)
TYPEB_STRIDE_EXPR_VALUE = -2 * 0x20     # `ld de, -2 * $20` (two rows per height, climbing)
MULTIPLAYER_DEST = 0xC9A2       # multiplayer round-start destination (WRAM buffer)
MULTIPLAYER_STRIDE_EXPR_VALUE = -0x20   # `ld de, -$20`
FILL_BUFFER_OFFSET = 0x3000     # `ld de, $3000` BG->buffer mirror in InitGarbage
FILL_ROW_WRAP_EXPR_VALUE = 0x20 - 10    # `ld de, $20 - 10` row wrap (confirms 10-wide, $20 stride)
FILL_RIGHTMOST_NIBBLE = 0x0B    # `cp a, $0B` rightmost-cell check (ensure at least one hole)
FILL_ROW_END_NIBBLE = 0x0C      # `cp a, $0C` end-of-row nibble
FILL_TERM_HIGH_NIBBLE = 0x0A    # `cp a, $0A` termination (second row from bottom starts at $9A00)
FILL_TERM_LOW = 0x2C            # `cp a, $2C` termination low byte

# Labels (structural anchors the parser matches against; not mirrored as C++ names).
TABLE_LABEL = "TypeBDemoGarbage"
DEMO_STAMP_LABEL = "InitDemoGarbage"
FILL_LABEL = "InitGarbage"
DEMO_NUMBER_VAR = "hDemoNumber"
MULTIPLAYER_VAR = "hIsMultiplayer"

# C++ emission.
CPP_GRID = "kTypeBDemoGarbage"
CPP_FIXTURE = "kExpectedTypeBDemoGarbageBytes"

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")

_TOP_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
_CALL_RE = re.compile(r"^call\s+(\S+)$")
_LD_A_IMM_RE = re.compile(r"^ld\s+a,\s*(?:\$([0-9A-Fa-f]+)|(\d+))$")
_LD_C_IMM_RE = re.compile(r"^ld\s+c,\s*(?:\$([0-9A-Fa-f]+)|(\d+))$")
_LD_B_IMM_RE = re.compile(r"^ld\s+b,\s*(?:\$([0-9A-Fa-f]+)|(\d+))$")
_LD_HL_RE = re.compile(r"^ld\s+hl,\s*\$([0-9A-Fa-f]{4})$")
_LD_DE_HEX_RE = re.compile(r"^ld\s+de,\s*\$([0-9A-Fa-f]+)$")
_LD_DE_EXPR_RE = re.compile(r"^ld\s+de,\s*(.+)$")
_LD_DE_LABEL_RE = re.compile(r"^ld\s+de,\s*([A-Za-z_][A-Za-z0-9_]*)$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_garbage"


# --- Pure helpers -------------------------------------------------------------------------------

_EXPR_TOKEN_RE = re.compile(r"\s*(\$[0-9A-Fa-f]+|\d+|[+\-*()])")


def eval_expr(text: str, path: Path, lineno: int) -> int:
    """Evaluate an RGBDS integer operand expression over `$hex`/decimal literals, unary +/-, and the
    binary + - * operators (all that the garbage call sites use: `-2 * $20`, `-$20`, `$20 - 10`).
    A hard error on any token or shape outside that grammar; no eval() of source text."""
    tokens: list[str] = []
    pos = 0
    while pos < len(text):
        if text[pos].isspace():
            pos += 1
            continue
        match = _EXPR_TOKEN_RE.match(text, pos)
        if not match or match.start(1) != pos:
            raise ParseError(f"{path}:{lineno}: unexpected token in expression {text!r} at {text[pos:]!r}")
        tokens.append(match.group(1))
        pos = match.end()
    if not tokens:
        raise ParseError(f"{path}:{lineno}: empty expression")

    index = 0

    def _peek() -> str | None:
        return tokens[index] if index < len(tokens) else None

    def _next() -> str:
        nonlocal index
        tok = tokens[index]
        index += 1
        return tok

    def _atom() -> int:
        tok = _peek()
        if tok is None:
            raise ParseError(f"{path}:{lineno}: expression {text!r} ended early")
        if tok == "(":
            _next()
            value = _sum()
            if _peek() != ")":
                raise ParseError(f"{path}:{lineno}: unbalanced parentheses in {text!r}")
            _next()
            return value
        if tok in ("+", "-"):
            _next()
            operand = _atom()
            return operand if tok == "+" else -operand
        _next()
        if tok.startswith("$"):
            return int(tok[1:], 16)
        if tok.isdigit():
            return int(tok)
        raise ParseError(f"{path}:{lineno}: unexpected token {tok!r} in expression {text!r}")

    def _product() -> int:
        value = _atom()
        while _peek() == "*":
            _next()
            value *= _atom()
        return value

    def _sum() -> int:
        value = _product()
        while _peek() in ("+", "-"):
            op = _next()
            rhs = _product()
            value = value + rhs if op == "+" else value - rhs
        return value

    result = _sum()
    if index != len(tokens):
        raise ParseError(f"{path}:{lineno}: trailing tokens in expression {text!r}")
    return result


def charmap_space(source_root: Path) -> int:
    """The tile index the charmap maps the space `" "` to ($2F). Uses the contract-asserted
    charmap parse so the empty-tile constant is tied to the character map, not hand-typed."""
    charmap_path = source_root / "charmap.asm"
    text = charmap_path.read_bytes().decode("utf-8")
    table = dict(parse_charmap.parse_charmap(text, charmap_path))
    if " " not in table:
        raise ParseError(f"{charmap_path}: the charmap has no mapping for a space \" \"")
    return table[" "]


def _decomment(line: str) -> str:
    """The directive on a source line, without its trailing `; comment` and surrounding space.
    No garbage-region line contains `;` inside a string, so splitting on `;` is unambiguous."""
    body, _sep, _comment = line.partition(";")
    return body.strip()


def _imm_value(match: re.Match) -> int:
    """The integer value of an `ld r, $hex | decimal` match (hex group 1, decimal group 2)."""
    return int(match.group(1), 16) if match.group(1) is not None else int(match.group(2))


# --- line / routine extraction ------------------------------------------------------------------

def _all_instrs(lines: list[str]) -> list[tuple[int, str]]:
    """Every meaningful line of the file as (1-based line number, decommented body). Blank and
    comment-only lines drop out; labels survive as their own bodies."""
    out: list[tuple[int, str]] = []
    for lineno, raw in enumerate(lines, start=1):
        body = _decomment(raw)
        if body:
            out.append((lineno, body))
    return out


def _find_top_label(lines: list[str], label: str, path: Path) -> int:
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label}:: defined more than once (lines {found})")
    return hits[0]


def _routine_instrs(lines: list[str], label: str, path: Path) -> list[tuple[int, str]]:
    """Meaningful lines of the routine at `label::`, from just after the label to the next top-level
    label (or EOF): (1-based line number, decommented body)."""
    idx = _find_top_label(lines, label, path)
    out: list[tuple[int, str]] = []
    for offset, raw in enumerate(lines[idx + 1:], start=idx + 2):
        stripped = raw.strip()
        if _TOP_LABEL_RE.match(stripped):
            break
        body = _decomment(raw)
        if body:
            out.append((offset, body))
    return out


# --- the demo garbage table ---------------------------------------------------------------------

def _parse_table(lines: list[str], empty_tile: int, path: Path) -> list[list[int]]:
    """The TypeBDemoGarbage table: exactly EXPECTED_DEMO_ROWS `db` rows of FIELD_COLS bytes, every
    cell the empty tile or a block tile ($80-$87), every row holding at least one empty cell (the
    data-side mirror of InitGarbage's ensure-one-hole rule)."""
    idx = _find_top_label(lines, TABLE_LABEL, path)
    rows: list[list[int]] = []
    for offset, raw in enumerate(lines[idx + 1:], start=idx + 2):
        stripped = _decomment(raw)
        if not stripped:
            if rows:
                break
            continue
        if not (stripped == "db" or stripped.startswith("db ") or stripped.startswith("db\t")):
            break
        rows.append(_parse_db_bytes(stripped[2:].strip(), offset, path))

    if len(rows) != EXPECTED_DEMO_ROWS:
        raise ParseError(
            f"{path}: {TABLE_LABEL} has {len(rows)} db rows, expected {EXPECTED_DEMO_ROWS}"
        )
    for row_i, row in enumerate(rows):
        lineno = idx + 2 + row_i
        if len(row) != FIELD_COLS:
            raise ParseError(
                f"{path}:{lineno}: {TABLE_LABEL} row {row_i + 1} is {len(row)} bytes wide, "
                f"expected {FIELD_COLS}"
            )
        for cell_i, cell in enumerate(row):
            if cell != empty_tile and not (BLOCK_TILE_BASE <= cell <= BLOCK_TILE_BASE + BLOCK_TILE_MASK):
                raise ParseError(
                    f"{path}:{lineno}: {TABLE_LABEL} row {row_i + 1} cell {cell_i} = ${cell:02X}, "
                    f"expected ${empty_tile:02X} or ${BLOCK_TILE_BASE:02X}-"
                    f"${BLOCK_TILE_BASE + BLOCK_TILE_MASK:02X}"
                )
        if empty_tile not in row:
            raise ParseError(
                f"{path}:{lineno}: {TABLE_LABEL} row {row_i + 1} has no empty (${empty_tile:02X}) "
                f"cell; every garbage row must leave at least one hole"
            )
    return rows


def _parse_db_bytes(operand: str, lineno: int, path: Path) -> list[int]:
    """A `db $HH, $HH, ...` line to its raw bytes. The demo table is pure `$HH` literals - no strings,
    no bare decimals - so anything else is a hard error."""
    out: list[int] = []
    for token in operand.split(","):
        token = token.strip()
        if not token:
            raise ParseError(f"{path}:{lineno}: empty byte in {TABLE_LABEL} db operand {operand!r}")
        if not (token.startswith("$") and len(token) > 1 and all(c in _HEX_DIGITS for c in token[1:])):
            raise ParseError(
                f"{path}:{lineno}: {TABLE_LABEL} must be `$HH` byte literals, found {token!r}"
            )
        out.append(int(token[1:], 16))
    return out


# --- the demo stamp -----------------------------------------------------------------------------

def _assert_demo_stamp(lines: list[str], table_rows: int, path: Path) -> None:
    """InitDemoGarbage's shape: destination $99C2, source TypeBDemoGarbage, `ld c, 4` (== the table's
    row count), inner `ld b, 10` (== the field width), the `add a, $30` buffer switch, and the $0020
    row stride. Extracts nothing new; cross-checks the table row/col counts against the stamp."""
    instrs = _routine_instrs(lines, DEMO_STAMP_LABEL, path)
    bodies = [body for _l, body in instrs]

    def _require(predicate, what: str) -> None:
        if not any(predicate(b) for b in bodies):
            raise ParseError(f"{path}: {DEMO_STAMP_LABEL} must contain {what}")

    _require(lambda b: (m := _LD_HL_RE.match(b)) and int(m.group(1), 16) == DEMO_DEST,
             f"`ld hl, ${DEMO_DEST:04X}` (the demo garbage destination)")
    _require(lambda b: (m := _LD_DE_LABEL_RE.match(b)) and m.group(1) == TABLE_LABEL,
             f"`ld de, {TABLE_LABEL}` (the table source)")
    _require(lambda b: b == f"add a, ${DEMO_BUFFER_SWITCH:02X}",
             f"`add a, ${DEMO_BUFFER_SWITCH:02X}` (the buffer-mirror high-byte switch)")
    _require(lambda b: (m := _LD_DE_HEX_RE.match(b)) and int(m.group(1), 16) == DEMO_ROW_STRIDE,
             f"`ld de, ${DEMO_ROW_STRIDE:04X}` (the row stride)")

    rows_c = _sole_imm(bodies, _LD_C_IMM_RE, f"{DEMO_STAMP_LABEL} row count `ld c, <n>`", path)
    if rows_c != table_rows:
        raise ParseError(
            f"{path}: {DEMO_STAMP_LABEL} stamps `ld c, {rows_c}` rows but {TABLE_LABEL} has "
            f"{table_rows} db rows"
        )
    cols_b = _sole_imm(bodies, _LD_B_IMM_RE, f"{DEMO_STAMP_LABEL} column count `ld b, <n>`", path)
    if cols_b != FIELD_COLS:
        raise ParseError(
            f"{path}: {DEMO_STAMP_LABEL} stamps `ld b, {cols_b}` columns, expected {FIELD_COLS}"
        )


def _sole_imm(bodies: list[str], regex: re.Pattern, what: str, path: Path) -> int:
    """The single `ld r, imm` value matching regex among bodies. Absent or repeated is a hard error."""
    hits = [_imm_value(m) for b in bodies if (m := regex.match(b))]
    if len(hits) != 1:
        raise ParseError(f"{path}: expected exactly one {what}, found {len(hits)}")
    return hits[0]


# --- the fill call sites ------------------------------------------------------------------------

def _parse_call_sites(instrs: list[tuple[int, str]], path: Path) -> tuple[int, int]:
    """Classify every `call InitGarbage`. Returns (rows_per_height, multiplayer_rows).

    Exactly three call sites are expected: one Type B start (dest $9A02, offset -2*$20, `ld a, b`)
    and two multiplayer round starts (dest $C9A2, offset -$20, `ld a, 6`), whose row counts must be
    equal. A `call InitDemoGarbage` reached from an `ldh a, [hDemoNumber]` branch is required too (the
    demo path that stamps the fixed table instead of filling procedurally)."""
    bodies = [body for _l, body in instrs]

    call_positions = [i for i, b in enumerate(bodies) if (m := _CALL_RE.match(b)) and m.group(1) == FILL_LABEL]
    if len(call_positions) != 3:
        raise ParseError(
            f"{path}: expected exactly 3 `call {FILL_LABEL}` sites, found {len(call_positions)}"
        )

    typeb: list[int] = []
    multiplayer: list[int] = []
    for pos in call_positions:
        lineno = instrs[pos][0]
        prev = _preceding(bodies, pos, 3)          # the three instructions before the call
        hl = _find_ld_hl(prev)
        if hl is None:
            raise ParseError(f"{path}:{lineno}: `call {FILL_LABEL}` site has no `ld hl, $XXXX` destination")
        de_value = _find_ld_de_value(prev, lineno, path)

        if hl == TYPEB_DEST:
            # The row count is dynamic here (`ld a, b`, b = the Type B start height); the rows-per-
            # height constant is the offset magnitude / row stride, not an immediate at the call site.
            if "ld a, b" not in prev:
                raise ParseError(
                    f"{path}:{lineno}: Type B `call {FILL_LABEL}` site must load the height with `ld a, b`"
                )
            if de_value != TYPEB_STRIDE_EXPR_VALUE:
                raise ParseError(
                    f"{path}:{lineno}: Type B garbage offset is {de_value}, expected "
                    f"{TYPEB_STRIDE_EXPR_VALUE} (`-2 * $20`)"
                )
            if abs(de_value) % ROW_STRIDE != 0:
                raise ParseError(f"{path}:{lineno}: Type B offset {de_value} is not a multiple of the row stride")
            typeb.append(abs(de_value) // ROW_STRIDE)
        elif hl == MULTIPLAYER_DEST:
            if de_value != MULTIPLAYER_STRIDE_EXPR_VALUE:
                raise ParseError(
                    f"{path}:{lineno}: multiplayer garbage offset is {de_value}, expected "
                    f"{MULTIPLAYER_STRIDE_EXPR_VALUE} (`-$20`)"
                )
            rows = _find_ld_a_imm(prev)
            if rows is None:
                raise ParseError(
                    f"{path}:{lineno}: multiplayer `call {FILL_LABEL}` site has no `ld a, <rows>` count"
                )
            multiplayer.append(rows)
        else:
            raise ParseError(
                f"{path}:{lineno}: `call {FILL_LABEL}` destination ${hl:04X} is neither the Type B "
                f"(${TYPEB_DEST:04X}) nor the multiplayer (${MULTIPLAYER_DEST:04X}) site"
            )

    if len(typeb) != 1:
        raise ParseError(f"{path}: expected exactly 1 Type B `call {FILL_LABEL}` site, found {len(typeb)}")
    if len(multiplayer) != 2:
        raise ParseError(
            f"{path}: expected exactly 2 multiplayer `call {FILL_LABEL}` sites, found {len(multiplayer)}"
        )
    if multiplayer[0] != multiplayer[1]:
        raise ParseError(
            f"{path}: the two multiplayer garbage sites disagree on row count: {multiplayer}"
        )

    _assert_demo_branch(bodies, path)
    return typeb[0], multiplayer[0]


def _assert_demo_branch(bodies: list[str], path: Path) -> None:
    """The demo path: an `ldh a, [hDemoNumber]` load reaching a `call InitDemoGarbage` within a small
    window - the branch that stamps the fixed table instead of filling procedurally. Exactly one
    `call InitDemoGarbage` in the file."""
    demo_calls = [i for i, b in enumerate(bodies) if (m := _CALL_RE.match(b)) and m.group(1) == DEMO_STAMP_LABEL]
    if len(demo_calls) != 1:
        raise ParseError(
            f"{path}: expected exactly one `call {DEMO_STAMP_LABEL}`, found {len(demo_calls)}"
        )
    call_pos = demo_calls[0]
    window = bodies[max(0, call_pos - 6):call_pos]
    if not any(b == f"ldh a, [{DEMO_NUMBER_VAR}]" for b in window):
        raise ParseError(
            f"{path}: `call {DEMO_STAMP_LABEL}` is not reached from an `ldh a, [{DEMO_NUMBER_VAR}]` "
            f"demo branch"
        )


def _preceding(bodies: list[str], pos: int, count: int) -> list[str]:
    return bodies[max(0, pos - count):pos]


def _find_ld_hl(prev: list[str]) -> int | None:
    for body in prev:
        m = _LD_HL_RE.match(body)
        if m:
            return int(m.group(1), 16)
    return None


def _find_ld_de_value(prev: list[str], lineno: int, path: Path) -> int | None:
    for body in prev:
        m = _LD_DE_EXPR_RE.match(body)
        if m and not _LD_DE_LABEL_RE.match(body):
            return eval_expr(m.group(1), path, lineno)
    return None


def _find_ld_a_imm(prev: list[str]) -> int | None:
    for body in prev:
        m = _LD_A_IMM_RE.match(body)
        if m:
            return _imm_value(m)
    return None


# --- the fill mechanism -------------------------------------------------------------------------

def _assert_fill_mechanism(lines: list[str], empty_tile: int, path: Path) -> tuple[int, int]:
    """InitGarbage's mechanism anchors (the routine body ports later; these pin what it does). Returns
    (block_tile_base, block_tile_count) read from the tile pick."""
    instrs = _routine_instrs(lines, FILL_LABEL, path)
    bodies = [body for _l, body in instrs]

    def _count(predicate) -> int:
        return sum(1 for b in bodies if predicate(b))

    def _require(predicate, what: str) -> None:
        if not any(predicate(b) for b in bodies):
            raise ParseError(f"{path}: {FILL_LABEL} must contain {what} (mechanism anchor)")

    rdiv_reads = _count(lambda b: b == "ldh a, [rDIV]")
    if rdiv_reads != 2:
        raise ParseError(
            f"{path}: {FILL_LABEL} must read `ldh a, [rDIV]` exactly twice (block pick and tile pick), "
            f"found {rdiv_reads}"
        )

    _require(lambda b: (m := _LD_A_IMM_RE.match(b)) and _imm_value(m) == BLOCK_TILE_BASE,
             f"the `ld a, ${BLOCK_TILE_BASE:02X}` block arm")
    _require(lambda b: b == 'ld a, " "', 'the `ld a, " "` empty arm')
    _require(lambda b: b == f"and a, ${BLOCK_TILE_MASK:02X}", f"`and a, ${BLOCK_TILE_MASK:02X}` (the tile mask)")
    _require(lambda b: b == f"or a, ${BLOCK_TILE_BASE:02X}", f"`or a, ${BLOCK_TILE_BASE:02X}` (the block-tile base)")
    _require(lambda b: b == f"cp a, ${FILL_RIGHTMOST_NIBBLE:02X}",
             f"`cp a, ${FILL_RIGHTMOST_NIBBLE:02X}` (the rightmost-cell ensure-one-hole check)")
    _require(lambda b: b == f"cp a, ${FILL_ROW_END_NIBBLE:02X}", f"`cp a, ${FILL_ROW_END_NIBBLE:02X}` (row-end nibble)")
    _require(lambda b: b == f"ldh a, [{MULTIPLAYER_VAR}]", f"`ldh a, [{MULTIPLAYER_VAR}]` (the buffer-write skip)")
    _require(lambda b: (m := _LD_DE_HEX_RE.match(b)) and int(m.group(1), 16) == FILL_BUFFER_OFFSET,
             f"`ld de, ${FILL_BUFFER_OFFSET:04X}` (the BG->buffer mirror offset)")
    _require(lambda b: b == f"cp a, ${FILL_TERM_HIGH_NIBBLE:02X}",
             f"`cp a, ${FILL_TERM_HIGH_NIBBLE:02X}` (termination high-nibble)")
    _require(lambda b: b == f"cp a, ${FILL_TERM_LOW:02X}", f"`cp a, ${FILL_TERM_LOW:02X}` (termination low byte)")

    # The row-wrap `ld de, $20 - 10` - evaluated, confirming the 10-wide row and $20 stride.
    wrap = None
    for body in bodies:
        m = _LD_DE_EXPR_RE.match(body)
        if m and not _LD_DE_HEX_RE.match(body) and not _LD_DE_LABEL_RE.match(body):
            value = eval_expr(m.group(1), path, 0)
            if value == FILL_ROW_WRAP_EXPR_VALUE:
                wrap = value
                break
    if wrap is None:
        raise ParseError(
            f"{path}: {FILL_LABEL} must contain the `ld de, $20 - 10` row wrap "
            f"(= {FILL_ROW_WRAP_EXPR_VALUE})"
        )

    # The empty arm resolves through the charmap to the same empty tile the table uses.
    if empty_tile != 0x2F:
        raise ParseError(
            f"{path}: charmap resolves \" \" to ${empty_tile:02X}, but {FILL_LABEL} and the table "
            f"assume $2F"
        )
    return BLOCK_TILE_BASE, BLOCK_TILE_COUNT


# --- parse + assert -----------------------------------------------------------------------------

def parse_garbage(asm_text: str, empty_tile: int, path: Path) -> dict:
    """Parse the demo table and assert the demo-stamp, call-site, and fill-mechanism shapes. Returns
    {"grid": [[int]*10]*4, "rows_per_height": int, "multiplayer_rows": int, "block_base": int,
     "block_count": int, "empty_tile": int}."""
    lines = asm_text.splitlines()

    grid = _parse_table(lines, empty_tile, path)
    _assert_demo_stamp(lines, len(grid), path)

    instrs = _all_instrs(lines)
    rows_per_height, multiplayer_rows = _parse_call_sites(instrs, path)
    block_base, block_count = _assert_fill_mechanism(lines, empty_tile, path)

    return {
        "grid": grid,
        "rows_per_height": rows_per_height,
        "multiplayer_rows": multiplayer_rows,
        "block_base": block_base,
        "block_count": block_count,
        "empty_tile": empty_tile,
    }


# --- emit ---------------------------------------------------------------------------------------

def _byte_row(values: list[int]) -> str:
    return ", ".join(f"0x{b:02X}" for b in values)


def emit_inc(result: dict, source_commit: str) -> str:
    grid = result["grid"]
    rows = len(grid)
    grid_lines = "\n".join(f"    {{{{ {_byte_row(row)} }}}}," for row in grid)
    return f"""{common.banner("parse_garbage.py", source_commit)}\
// Included at namespace scope in src/data/garbage.h (inside `namespace kirpich`), which includes
// <array>, <cstdint>, and playing_field.h (for kPlayingFieldCols) before this.
// The Type B demo garbage table and the constants the procedural fill (InitGarbage) and its call
// sites consume. Cells are raw tile indices: the empty tile or one of the eight block tiles. The
// fill routine itself and the demo stamp port with the gameplay logic (see
// docs/contracts/garbage-init.md); only the table and these constants live here.

// Number of rows in the fixed demo garbage table (InitDemoGarbage stamps this many).
inline constexpr std::uint8_t kTypeBDemoGarbageRows = {rows};
// Rows of garbage per Type B starting height (the fill climbs two rows per height level).
inline constexpr std::uint8_t kTypeBGarbageRowsPerHeight = {result["rows_per_height"]};
// Rows of garbage each multiplayer round starts the loser's field with.
inline constexpr std::uint8_t kMultiplayerRoundStartGarbageRows = {result["multiplayer_rows"]};
// The first block tile; the eight block tiles are this base through base + count - 1.
inline constexpr std::uint8_t kGarbageBlockTileBase = 0x{result["block_base"]:02X};
inline constexpr std::uint8_t kGarbageBlockTileCount = {result["block_count"]};
// The empty cell tile (the character map's space).
inline constexpr std::uint8_t kGarbageEmptyTile = 0x{result["empty_tile"]:02X};

// The fixed demo garbage: {rows} rows of kPlayingFieldCols cells, row-major (row 0 drawn topmost).
inline constexpr std::array<std::array<std::uint8_t, kPlayingFieldCols>, kTypeBDemoGarbageRows>
    {CPP_GRID} = {{{{
{grid_lines}
}}}};
"""


def emit_fixture(result: dict, source_commit: str) -> str:
    grid = result["grid"]
    flat = [b for row in grid for b in row]
    rows_lines = "\n".join(f"    {_byte_row(row)}," for row in grid)
    return f"""#pragma once
{common.banner("parse_garbage.py", source_commit)}\
// Independent fixture for the full-corpus demo-garbage sweep: the {len(flat)} bytes of TypeBDemoGarbage
// in serialization order (row-major), flat and independent of the composed grid in src/data/garbage.h
// so a defect in the header cannot mask the sweep. tests/test_garbage.cpp asserts the composed grid
// equals these bytes cell for cell.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

inline constexpr std::array<std::uint8_t, {len(flat)}> {CPP_FIXTURE} = {{{{
{rows_lines}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(
            f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})"
        )


# --- driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Emit Kirpich's demo garbage table + garbage-fill constants and fixture.")
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
        print(f"parse_garbage: source file not found: {asm_path}", file=sys.stderr)
        return 2
    if not (source_root / "charmap.asm").is_file():
        print(f"parse_garbage: source file not found: {source_root / 'charmap.asm'}", file=sys.stderr)
        return 2

    empty_tile = charmap_space(source_root)
    asm_text = asm_path.read_bytes().decode("utf-8")
    result = parse_garbage(asm_text, empty_tile, asm_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(result, commit),
        args.fixture_out: emit_fixture(result, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        _assert_ascii(content, str(out_path))
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_garbage: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_garbage: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
