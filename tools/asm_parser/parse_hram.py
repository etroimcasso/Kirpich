#!/usr/bin/env python3
"""Parser for Kirpich's HRAM layout contract - the whole of tetris/hram.asm - plus a raw-operand
census over tetris/tetris.asm.

hram.asm, like wram.asm, declares no data values; it is a pure address map of the $FF80-$FFFE high
RAM. Every label names an HRAM address and every `ds`/`db`/`dw` reserves space at the running
address. Pass 1 walks the file, deriving each label's address + size from the section origin
($FF80) and the reservations that precede it (never from a build artifact - the addresses-from-source
rule).

Pass 2 scans tetris.asm for every STATIC raw-operand HRAM access - the accesses that reach a byte
WITHOUT naming its label, so the layout table alone cannot see them: numeric `ldh` operands
(`[$98]`, `[$FFD1]`, `[$86 + 6]`) and 16-bit pointer loads of an HRAM address (`ld hl, $FFC6`,
`ld de, hLines + 1`). Symbolic `ldh [hLabel]` accesses are NOT census rows - the label already
appears in the layout table, so those bytes are visibly owned. The census exists to prove that the
bytes reached only by a raw operand - the `ds` gaps and the seven commented-out slots - are still
accounted for by some state unit; a test resolves every census address to an owner.

Two emitted tables, one fixture (`hram_expected.h`):
  * the layout table: {name, address, size, kind} per label / alias / anonymous region, tiling
    $FF80-$FFFE exactly;
  * the census table: {address, refCount} for every static raw-operand HRAM access, sorted ascending.
Later state surfaces reuse this fixture, exactly as the engine-state surface reuses wram_expected.h.
No enum/.inc emission - state structs carry no ROM table values.

Structural model - how many reservations a label owns (differs from parse_wram):

  * A label owns exactly the NEXT data directive. A label followed immediately by `db`/`dw`/`ds` is
    one Field of that size (hGameState:: db = 1 byte; hLines:: dw = 2 bytes; hDMARoutine:: ds $A = 10).
  * Several labels stacked before one directive alias one address (hTopScorePointerLo:: /
    hTempPreviewPiece:: share $FFFC): the first is the Field, the rest are Aliases.
  * A directive with NO unconsumed preceding label is an anonymous region (kind Gap) - a `ds` pad,
    or the bare `db` left after a commented-out slot label (`;hFF94::`). It never folds into the
    field before it. (parse_wram extends the prior field with a bare slot; hram.asm never does that,
    so this parser keeps the simpler rule and rejects the ambiguous case.)

Self-checks (any failure is a hard error with a source citation, never silent):

  * Every `ds $HIGH - $LOW` whose operands are absolute HRAM addresses asserts $LOW == the running
    address, so the file's own arithmetic double-checks the walk.
  * Every region has positive size and lies inside $FF80..$FFFE; the section tiles with no overlap
    and no hole; the walk must end exactly at $FFFE (HRAM proper stops one below rIE at $FFFF).
  * Every `ldh` operand in tetris.asm parses to a known form (numeric / dynamic `[c]` / a symbol);
    an unrecognized operand form is a hard error, never a skipped line.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

import common

HRAM_BASE = 0xFF80        # RGBDS HRAM section origin
HRAM_TOP = 0xFFFE         # one past the last reserved HRAM byte ($FFFD); rIE lives at $FFFF
HW_REG_TOP = 0xFF80       # $FF00..$FF7F are hardware registers - below this is out of census scope

_SECTION_RE = re.compile(r'^SECTION\s+"([^"]*)",\s*HRAM$')
# A label line: `name:` or `name::`, optionally followed by a directive on the same line.
_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)::?\s*(.*)$')
_DS_RE = re.compile(r'^ds\s+(.+)$')
_ABS_SUB_RE = re.compile(r'^\$([0-9A-Fa-f]+)\s*-\s*\$([0-9A-Fa-f]+)$')
_TOKEN_RE = re.compile(r'\$[0-9A-Fa-f]+|\d+|[+\-*()]|\s+')

# Census operand forms in tetris.asm.
_LDH_RE = re.compile(r'\bldh\b')
_BRACKET_RE = re.compile(r'\[([^\]]*)\]')
_PTR_LOAD_RE = re.compile(r'^ld\s+(hl|de|bc)\s*,\s*(.+?)\s*$')
_SYMBOL_OPERAND_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*(\s*[+\-]\s*(\$[0-9A-Fa-f]+|\d+))?$')
_LABEL_OFFSET_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)(?:\s*\+\s*(\$[0-9A-Fa-f]+|\d+))?$')
_HEX_LITERAL_RE = re.compile(r'^\$([0-9A-Fa-f]+)$')


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_hram"


class Kind(Enum):
    FIELD = "Field"   # a labelled region; owns the reservation after its label
    ALIAS = "Alias"   # a second label sharing an earlier field's address (hTempPreviewPiece)
    GAP = "Gap"       # an anonymous ds / a bare slot after a commented-out label; not a label


@dataclass
class Region:
    name: str          # "" for gaps
    address: int
    size: int
    kind: Kind


@dataclass
class Section:
    name: str
    origin: int
    end: int           # one past the last byte (origin + tiled span)
    first: int         # index of this section's first region in the flat region list
    count: int         # number of regions in this section


@dataclass
class CensusEntry:
    address: int
    ref_count: int


# --- Expression evaluation (ds operands and numeric ldh operands) --------------------------------

def _tokenize(expr: str, path: Path, lineno: int) -> list[str]:
    tokens: list[str] = []
    pos = 0
    while pos < len(expr):
        m = _TOKEN_RE.match(expr, pos)
        if not m:
            raise ParseError(f"{path}:{lineno}: unexpected character in expression {expr!r} at {expr[pos]!r}")
        tok = m.group(0)
        pos = m.end()
        if not tok.isspace():
            tokens.append(tok)
    if not tokens:
        raise ParseError(f"{path}:{lineno}: empty expression")
    return tokens


def _eval_expr(expr: str, path: Path, lineno: int) -> int:
    """Evaluate a simple integer expression (+, -, *, parentheses, $hex and decimal literals)."""
    tokens = _tokenize(expr, path, lineno)
    pos = 0

    def peek() -> str | None:
        return tokens[pos] if pos < len(tokens) else None

    def advance() -> str:
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        return tok

    def parse_atom() -> int:
        tok = peek()
        if tok is None:
            raise ParseError(f"{path}:{lineno}: expression ended early: {expr!r}")
        if tok == "(":
            advance()
            value = parse_add()
            if peek() != ")":
                raise ParseError(f"{path}:{lineno}: unbalanced parentheses in {expr!r}")
            advance()
            return value
        advance()
        if tok.startswith("$"):
            return int(tok[1:], 16)
        if tok.isdigit():
            return int(tok, 10)
        raise ParseError(f"{path}:{lineno}: expected a number, got {tok!r} in {expr!r}")

    def parse_mul() -> int:
        value = parse_atom()
        while peek() == "*":
            advance()
            value *= parse_atom()
        return value

    def parse_add() -> int:
        value = parse_mul()
        while peek() in ("+", "-"):
            op = advance()
            rhs = parse_mul()
            value = value + rhs if op == "+" else value - rhs
        return value

    result = parse_add()
    if pos != len(tokens):
        raise ParseError(f"{path}:{lineno}: trailing tokens in expression {expr!r}")
    return result


def _ds_size(operand: str, addr: int, path: Path, lineno: int) -> int:
    """Bytes reserved by `ds <operand>`, with the absolute-address subtraction self-check."""
    operand = operand.strip()
    abs_sub = _ABS_SUB_RE.match(operand)
    if abs_sub and int(abs_sub.group(1), 16) >= HRAM_BASE and int(abs_sub.group(2), 16) >= HRAM_BASE:
        # An absolute-address gap `ds $HIGH - $LOW`: assert $LOW is the running address, so the
        # file's own arithmetic double-checks the walk.
        hi, lo = int(abs_sub.group(1), 16), int(abs_sub.group(2), 16)
        if lo != addr:
            raise ParseError(
                f"{path}:{lineno}: gap `ds ${hi:04X} - ${lo:04X}` starts at ${lo:04X} but the "
                f"running address is ${addr:04X} - the layout walk and the file disagree")
        size = hi - lo
    else:
        size = _eval_expr(operand, path, lineno)
    if size <= 0:
        raise ParseError(f"{path}:{lineno}: `ds {operand}` reserves {size} bytes (must be positive)")
    return size


# --- Pass 1: layout walk ------------------------------------------------------------------------

def _directive_bytes(text: str, addr: int, path: Path, lineno: int) -> int:
    """Return the size in bytes for one directive (ds / db / dw)."""
    ds = _DS_RE.match(text)
    if ds:
        return _ds_size(ds.group(1), addr, path, lineno)
    if text == "db":
        return 1
    if text == "dw":
        return 2
    raise ParseError(f"{path}:{lineno}: unrecognized directive {text!r} (expected ds/db/dw)")


def parse_layout(text: str, path: Path) -> tuple[list[Region], list[Section]]:
    regions: list[Region] = []
    sections: list[Section] = []
    addr: int | None = None
    section: Section | None = None
    pending: list[str] = []          # labels seen since the last directive

    def close_section(next_addr: int) -> None:
        nonlocal section
        if section is not None:
            section.end = next_addr
            section.count = len(regions) - section.first
            sections.append(section)

    def consume(directive: str, lineno: int) -> None:
        nonlocal addr, pending
        assert addr is not None
        size = _directive_bytes(directive, addr, path, lineno)
        if pending:
            regions.append(Region(pending[0], addr, size, Kind.FIELD))
            for alias in pending[1:]:
                regions.append(Region(alias, addr, size, Kind.ALIAS))
            pending = []
        else:
            regions.append(Region("", addr, size, Kind.GAP))
        addr += size

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue

        sec = _SECTION_RE.match(line)
        if sec:
            if pending:
                raise ParseError(f"{path}:{lineno}: labels {pending} have no reservation before section {sec.group(1)!r}")
            close_section(addr if addr is not None else HRAM_BASE)
            addr = HRAM_BASE
            section = Section(sec.group(1), HRAM_BASE, HRAM_BASE, len(regions), 0)
            continue

        if section is None:
            raise ParseError(f"{path}:{lineno}: content before any SECTION: {line!r}")

        label = _LABEL_RE.match(line)
        if label:
            pending.append(label.group(1))
            rest = label.group(2).strip()
            if rest:
                consume(rest, lineno)
            continue

        consume(line, lineno)

    if pending:
        raise ParseError(f"{path}: file ends with labels {pending} that reserve no space")
    close_section(addr if addr is not None else HRAM_BASE)

    _validate_layout(regions, sections, path)
    return regions, sections


def _validate_layout(regions: list[Region], sections: list[Section], path: Path) -> None:
    for region in regions:
        if region.size <= 0:
            raise ParseError(f"{path}: region {region.name or '<gap>'} at ${region.address:04X} has size {region.size}")
        if not HRAM_BASE <= region.address < HRAM_TOP:
            raise ParseError(f"{path}: region {region.name or '<gap>'} address ${region.address:04X} is outside HRAM $FF80..$FFFD")
        if region.address + region.size > HRAM_TOP:
            raise ParseError(f"{path}: region {region.name or '<gap>'} at ${region.address:04X} overruns HRAM (ends past $FFFE)")

    for section in sections:
        cursor = section.origin
        last_field_addr: int | None = None
        for region in regions[section.first:section.first + section.count]:
            if region.kind is Kind.ALIAS:
                if region.address != last_field_addr:
                    raise ParseError(
                        f"{path}: alias {region.name} at ${region.address:04X} does not share the "
                        f"preceding field's address"
                        + (f" ${last_field_addr:04X}" if last_field_addr is not None else " (no preceding field)"))
                continue
            if region.address != cursor:
                raise ParseError(
                    f"{path}: section {section.name!r} does not tile: expected ${cursor:04X}, "
                    f"region {region.name or '<gap>'} is at ${region.address:04X}")
            if region.kind is Kind.FIELD:
                last_field_addr = region.address
            cursor += region.size
        if cursor != section.end:
            raise ParseError(
                f"{path}: section {section.name!r} tiles to ${cursor:04X} but ends at ${section.end:04X}")

    if sections and sections[-1].end != HRAM_TOP:
        raise ParseError(
            f"{path}: the HRAM walk ends at ${sections[-1].end:04X} but must end at $FFFE "
            f"(HRAM proper is $FF80..$FFFD, one below rIE)")


# --- Pass 2: raw-operand census -----------------------------------------------------------------

def _resolve_ldh_operand(operand: str, path: Path, lineno: int) -> int | None:
    """Resolve one `ldh [...]` operand to a full $FFxx address, or None to skip (dynamic/symbolic).

    A numeric operand is $FF00 + n8 (short form) or the literal itself when already >= $FF00 (long
    form). A symbolic operand - `[c]`, a hardware register (`[rLCDC]`), or an HRAM label
    (`[hGameState]`) - is skipped: the register is out of scope and the labelled access is already
    visible in the layout table. Any operand that is neither numeric nor a plain symbol is a hard
    error, so a new addressing form can never slip through as a silent skip.
    """
    operand = operand.strip()
    try:
        value = _eval_expr(operand, path, lineno)
    except ParseError:
        if _SYMBOL_OPERAND_RE.match(operand):
            return None  # `[c]`, `[rLCDC]`, `[hGameState]`, `[hLines + 1]` - recognized, not a raw byte
        raise ParseError(f"{path}:{lineno}: unrecognized ldh operand form [{operand}]")
    addr = value if value >= 0xFF00 else 0xFF00 + value
    if not 0xFF00 <= addr <= 0xFFFF:
        raise ParseError(f"{path}:{lineno}: ldh operand [{operand}] resolves to ${addr:04X}, outside $FF00..$FFFF")
    return addr


def _resolve_ptr_operand(operand: str, symbols: dict[str, int]) -> int | None:
    """Resolve a `ld hl/de/bc, <operand>` pointer load to an HRAM address, or None to skip.

    Two forms are HRAM accesses: a numeric $FFxx literal, and an HRAM label with an optional
    `+ N` offset (resolved through the pass-1 symbol table). Anything else - a non-HRAM label
    (`wOAMBuffer`, `DMARoutine`), a low literal (`$FEFF`) - is not an HRAM access.
    """
    operand = operand.strip()
    hexlit = _HEX_LITERAL_RE.match(operand)
    if hexlit:
        value = int(hexlit.group(1), 16)
        return value if HRAM_BASE <= value <= HRAM_TOP else None
    labeled = _LABEL_OFFSET_RE.match(operand)
    if labeled and labeled.group(1) in symbols:
        base = symbols[labeled.group(1)]
        offset = 0
        if labeled.group(2):
            off = labeled.group(2)
            offset = int(off[1:], 16) if off.startswith("$") else int(off, 10)
        addr = base + offset
        if HRAM_BASE <= addr <= HRAM_TOP:
            return addr
    return None


def census(text: str, symbols: dict[str, int], path: Path) -> list[CensusEntry]:
    """Scan tetris.asm; return every static raw-operand HRAM access as {address, refCount} rows.

    Census scope is HRAM state, $FF80..$FFFE: $FF00..$FF7F (hardware registers) and $FFFF (rIE) are
    out of scope. $FFFE is in scope (the boot HRAM-clear loop points there), even though the layout
    table stops one below it.
    """
    counts: dict[int, int] = {}

    def record(addr: int) -> None:
        if HRAM_BASE <= addr <= HRAM_TOP:
            counts[addr] = counts.get(addr, 0) + 1

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue

        if _LDH_RE.search(line):
            bracket = _BRACKET_RE.search(line)
            if not bracket:
                raise ParseError(f"{path}:{lineno}: ldh with no [operand]: {line!r}")
            addr = _resolve_ldh_operand(bracket.group(1), path, lineno)
            if addr is not None:
                record(addr)
            continue

        ptr = _PTR_LOAD_RE.match(line)
        if ptr:
            addr = _resolve_ptr_operand(ptr.group(2), symbols)
            if addr is not None:
                record(addr)

    return [CensusEntry(addr, counts[addr]) for addr in sorted(counts)]


def symbol_table(regions: list[Region]) -> dict[str, int]:
    """label name -> address, for every Field and Alias (aliases resolve too: hTempPreviewPiece)."""
    return {r.name: r.address for r in regions if r.name and r.kind in (Kind.FIELD, Kind.ALIAS)}


# --- Emit: hram_expected.h ----------------------------------------------------------------------

def _kind_token(kind: Kind) -> str:
    return f"HramKind::{kind.value}"


def _row_note(region: Region) -> str:
    if region.kind is Kind.GAP:
        return "gap"
    if region.kind is Kind.ALIAS:
        return "alias"
    return ""


def emit_fixture(regions: list[Region], sections: list[Section],
                 census_rows: list[CensusEntry], source_commit: str) -> str:
    label_rows = "\n".join(
        f'    {{ .name = "{r.name}", .address = 0x{r.address:04X}, .size = {r.size}, '
        f'.kind = {_kind_token(r.kind)} }},'
        + (f"  // {_row_note(r)}" if _row_note(r) else "")
        for r in regions)
    section_rows = "\n".join(
        f'    {{ .name = "{s.name}", .origin = 0x{s.origin:04X}, .end = 0x{s.end:04X}, '
        f'.first = {s.first}, .count = {s.count} }},'
        for s in sections)
    census_out = "\n".join(
        f'    {{ .address = 0x{c.address:04X}, .refCount = {c.ref_count} }},'
        for c in census_rows)
    field_count = sum(1 for r in regions if r.kind is Kind.FIELD)
    alias_count = sum(1 for r in regions if r.kind is Kind.ALIAS)
    gap_count = sum(1 for r in regions if r.kind is Kind.GAP)
    return f"""#pragma once
{common.banner("parse_hram.py", source_commit)}\
// The HRAM layout contract and the raw-operand census, both derived from the upstream disassembly.
//
// kHramLabels: every label in tetris/hram.asm and every anonymous region between labels, each as
// {{name, address, size}} with a kind tag. Addresses derive from the $FF80 section origin and the
// reservations that precede each label - nothing here is hand-typed. A Field owns its reservation;
// an Alias shares an earlier field's address (hTempPreviewPiece aliases hTopScorePointerLo at
// $FFFC); a Gap is anonymous padding or the bare slot left by a commented-out label. Regions tile
// $FF80..$FFFD exactly (the walk ends at $FFFE, one below rIE).
//
// kHramCensus: {{address, refCount}} for every STATIC raw-operand HRAM access in tetris/tetris.asm -
// numeric `ldh` operands and 16-bit pointer loads of an HRAM address. Symbolic `ldh [hLabel]`
// accesses are not counted (the label is already visible in kHramLabels). The census makes the
// bytes reached only through a raw operand - the gaps and commented-out slots - provable owners of
// some state unit. Sorted by address.
//
// Totals: {field_count} fields, {alias_count} alias, {gap_count} gaps, {len(sections)} section; {len(census_rows)} census addresses.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

enum class HramKind : std::uint8_t {{ Field, Alias, Gap }};

// One region of the HRAM map. `size` is bytes reserved; for an Alias it repeats the field's size.
struct HramLabel {{
    std::string_view name;      // "" for a Gap
    std::uint16_t    address;
    std::uint16_t    size;
    HramKind         kind;
}};

// The HRAM section: [origin, end) tiled exactly by its regions [first, first + count).
struct HramSection {{
    std::string_view name;
    std::uint16_t    origin;
    std::uint16_t    end;       // one past the last byte
    std::uint16_t    first;     // index of the first region in kHramLabels
    std::uint16_t    count;     // number of regions in this section
}};

// One raw-operand HRAM address and how many static access sites reach it.
struct HramCensus {{
    std::uint16_t address;
    std::uint16_t refCount;
}};

inline constexpr std::array<HramLabel, {len(regions)}> kHramLabels{{{{
{label_rows}
}}}};

inline constexpr std::array<HramSection, {len(sections)}> kHramSections{{{{
{section_rows}
}}}};

inline constexpr std::array<HramCensus, {len(census_rows)}> kHramCensus{{{{
{census_out}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's HRAM layout + census fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true", help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    hram_path: Path = args.source_root / "hram.asm"
    tetris_path: Path = args.source_root / "tetris.asm"
    for p in (hram_path, tetris_path):
        if not p.is_file():
            print(f"parse_hram: source file not found: {p}", file=sys.stderr)
            return 2

    regions, sections = parse_layout(hram_path.read_bytes().decode("utf-8"), hram_path)
    symbols = symbol_table(regions)
    census_rows = census(tetris_path.read_bytes().decode("utf-8"), symbols, tetris_path)
    commit = common.source_commit_of(args.source_root)

    fields = sum(1 for r in regions if r.kind is Kind.FIELD)
    aliases = sum(1 for r in regions if r.kind is Kind.ALIAS)
    gaps = sum(1 for r in regions if r.kind is Kind.GAP)
    total_refs = sum(c.ref_count for c in census_rows)
    print(f"parse_hram: {fields} fields + {aliases} alias + {gaps} gaps in 1 section; "
          f"{len(census_rows)} census addresses ({total_refs} access sites); asserts passed.")

    if args.fixture_out is not None:
        content = emit_fixture(regions, sections, census_rows, commit)
        _assert_ascii(content, str(args.fixture_out))
        args.fixture_out.parent.mkdir(parents=True, exist_ok=True)
        args.fixture_out.write_text(content, encoding="ascii")
        print(f"parse_hram: wrote {args.fixture_out}")
    else:
        print("parse_hram: no --fixture-out given; nothing written (structural asserts still ran).",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
