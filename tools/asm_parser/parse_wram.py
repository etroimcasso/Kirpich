#!/usr/bin/env python3
"""Parser for Kirpich's WRAM layout contract - the whole of tetris/wram.asm - plus a raw-operand
census over tetris/tetris.asm.

wram.asm declares no data values; it is a pure address map. Every label names a WRAM address and
every `ds`/`db`/`dw` reserves space at the running address. Pass 1 walks the file, deriving each
label's address + size from the section origin and the reservations that precede it (never from a
build artifact - the addresses-from-source rule).

Pass 2 scans tetris.asm for every STATIC WRAM operand - the game-side accesses that name a byte in
$C000-$DFFF, whether by a raw literal or by a label. WRAM's absolute-load forms differ from HRAM's
`ldh`: they are the long-form `ld [$XXXX], a` / `ld a, [$XXXX]` and their symbolic twins
`ld [wLabel], a` / `ld a, [wLabel + N]`, plus 16-bit immediate loads of a WRAM address
(`ld hl/de/bc/sp, $XXXX` or `ld hl, wLabel + N`). Labels resolve through Pass 1's symbol table.
`HIGH(wLabel)` / `LOW(wLabel)` into an 8-bit register is address-byte arithmetic (a computed pointer
setup, e.g. the OAM-DMA source page), not a byte access - recognized and skipped, never counted.
`ld sp, $CFFF` (the stack init) is counted: sp is a recognized pointer-load register so the stack
top resolves to a census address the boundary test owns as a mechanism.

The census exists to machine-guard the whole state-layer ownership map: a downstream test resolves
every census address to exactly one owner (an engine-state field, an audio cue/slot byte, a sprite
or board window, the top-score region, or a mechanism address), so a byte reached only through a raw
$Cxxx / $Dxxx operand - the windows wram.asm leaves as anonymous gaps - is still a provable owner of
some state unit. audio.asm is NOT censused: the sound driver runs as extracted bytes on the port's
own machine, so its accesses are private by construction, and every driver-private claim is proven
by ABSENCE from this game-side census.

Two emitted tables, one fixture (`wram_expected.h`):
  * the layout table: {name, address, size, kind} per label / alias / anonymous region, tiling each
    section exactly;
  * the census table: {address, refCount} for every static WRAM operand access, sorted ascending.
Later state surfaces reuse this fixture. There is no enum or .inc emission - state structs carry no
ROM table values, so the layout table (widths + a section-tiling proof) and the census are the whole
contract.

Structural model - how many reservations a label owns:

  * A `db`/`dw` (a data slot) attaches to the field opened by the nearest preceding label, so a label
    followed by several bare slots is ONE field (wLineClearsList = dw dw dw dw db = 9 bytes).
  * A `ds` with a label is that label's own field (wOAMBuffer ds $A0, wPieceList ds $100).
  * A `ds` with NO preceding label is an anonymous gap - padding or an address-arithmetic jump - and
    is recorded as its own region, so it never inflates the field before it (wSinglesCount stays 1
    byte even though a `ds 4` pad follows it; wPieceList stays 256 even though a 3 KB gap follows).

Self-checks (any failure is a hard error with a source citation, never silent):

  * Every `ds $HIGH - $LOW` whose operands are absolute WRAM addresses asserts $LOW == the running
    address, so the file's own arithmetic double-checks the walk.
  * A `SECTION ... WRAM0[$XXXX]` with an explicit origin asserts the running address already equals
    $XXXX (the preceding gap landed exactly on the new section).
  * Every region has positive size and lies inside $C000..$DFFF; regions tile each section with no
    overlap and no hole.

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

WRAM0_BASE = 0xC000        # RGBDS WRAM0 default origin
WRAM0_END = 0xE000         # one past the last WRAM0 byte ($DFFF)

_SECTION_RE = re.compile(r'^SECTION\s+"([^"]*)",\s*WRAM0(?:\[\$([0-9A-Fa-f]+)\])?$')
# A label line: `name::` optionally followed by a directive on the same line (`wOAMBuffer:: ds $A0`).
_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)::\s*(.*)$')
_DS_RE = re.compile(r'^ds\s+(.+)$')
_ABS_SUB_RE = re.compile(r'^\$([0-9A-Fa-f]+)\s*-\s*\$([0-9A-Fa-f]+)$')
_TOKEN_RE = re.compile(r'\$[0-9A-Fa-f]+|\d+|[+\-*()]|\s+')

# Pass-2 census operand forms in tetris.asm (the WRAM analog of parse_hram's ldh / pointer forms).
_ABS_STORE_RE = re.compile(r'^ld\s+\[([^\]]*)\]\s*,\s*a$')          # ld [$C0CE], a  /  ld [wLabel], a
_ABS_LOAD_RE = re.compile(r'^ld\s+a\s*,\s*\[([^\]]*)\]$')           # ld a, [$C0CE]  /  ld a, [wLabel]
_REG16_LOAD_RE = re.compile(r'^ld\s+(hl|de|bc|sp)\s*,\s*(.+?)\s*$')  # ld hl/de/bc/sp, <operand>
_HIGHLOW_R8_RE = re.compile(r'^ld\s+[abcdehl]\s*,\s*(?:HIGH|LOW)\s*\(', re.IGNORECASE)
_WLABEL_RE = re.compile(r'\bw[A-Z][A-Za-z0-9_]*\b')                 # a WRAM data label (RGBDS w-prefix)
_ANY_HEX_RE = re.compile(r'\$[0-9A-Fa-f]+')


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_wram"


class Kind(Enum):
    FIELD = "Field"   # a labelled region; owns the reservation(s) after its label
    ALIAS = "Alias"   # a second label sharing an earlier field's address (wLineClearStats/wSinglesCount)
    GAP = "Gap"       # an anonymous ds - padding or an address jump; not a label


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


# --- Expression evaluation (ds operands: $hex / decimal / + - * / parens) ------------------------

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
    if abs_sub and int(abs_sub.group(1), 16) >= WRAM0_BASE and int(abs_sub.group(2), 16) >= WRAM0_BASE:
        # An absolute-address gap `ds $HIGH - $LOW`: assert $LOW is the running address, so the
        # file's own arithmetic double-checks the walk. (A relative form like `$E - $B`, both below
        # $C000, falls through to plain evaluation without the address check.)
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


# --- Walk ---------------------------------------------------------------------------------------

def _directive_bytes(text: str, addr: int, path: Path, lineno: int) -> tuple[int, bool]:
    """Return (size, is_ds) for one directive. is_ds distinguishes a reservation from a data slot."""
    ds = _DS_RE.match(text)
    if ds:
        return _ds_size(ds.group(1), addr, path, lineno), True
    if text == "db":
        return 1, False
    if text == "dw":
        return 2, False
    raise ParseError(f"{path}:{lineno}: unrecognized directive {text!r} (expected ds/db/dw)")


def parse_wram(text: str, path: Path) -> tuple[list[Region], list[Section]]:
    regions: list[Region] = []
    sections: list[Section] = []
    addr: int | None = None
    section: Section | None = None
    pending: list[str] = []          # labels seen since the last directive
    field_idx: int | None = None     # the open field a bare data slot would extend

    def close_section(next_addr: int) -> None:
        nonlocal section
        if section is not None:
            section.end = next_addr
            section.count = len(regions) - section.first
            sections.append(section)

    def consume(text: str, lineno: int) -> None:
        nonlocal addr, pending, field_idx
        assert addr is not None
        size, is_ds = _directive_bytes(text, addr, path, lineno)
        if pending:
            regions.append(Region(pending[0], addr, size, Kind.FIELD))
            field_idx = len(regions) - 1
            for alias in pending[1:]:
                regions.append(Region(alias, addr, size, Kind.ALIAS))
            pending = []
            addr += size
        elif not is_ds and field_idx is not None:
            # A bare data slot continues the current field (wLineClearsList's extra dw/dw/dw/db).
            field = regions[field_idx]
            for region in regions:
                if region.address == field.address and region.kind in (Kind.FIELD, Kind.ALIAS):
                    region.size += size
            addr += size
        else:
            regions.append(Region("", addr, size, Kind.GAP))
            field_idx = None
            addr += size

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue

        sec = _SECTION_RE.match(line)
        if sec:
            name = sec.group(1)
            origin = int(sec.group(2), 16) if sec.group(2) else WRAM0_BASE
            if addr is not None and sec.group(2) is not None and addr != origin:
                raise ParseError(
                    f"{path}:{lineno}: section {name!r} declares origin ${origin:04X} but the running "
                    f"address is ${addr:04X} - the preceding section did not tile up to it")
            if pending:
                raise ParseError(f"{path}:{lineno}: labels {pending} have no reservation before section {name!r}")
            close_section(addr if addr is not None else origin)
            addr = origin
            section = Section(name, origin, origin, len(regions), 0)
            field_idx = None
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
    close_section(addr if addr is not None else WRAM0_BASE)

    _validate(regions, sections, path)
    return regions, sections


def _validate(regions: list[Region], sections: list[Section], path: Path) -> None:
    for region in regions:
        if region.size <= 0:
            raise ParseError(f"{path}: region {region.name or '<gap>'} at ${region.address:04X} has size {region.size}")
        if not WRAM0_BASE <= region.address < WRAM0_END:
            raise ParseError(f"{path}: region {region.name or '<gap>'} address ${region.address:04X} is outside WRAM0")
        if region.address + region.size > WRAM0_END:
            raise ParseError(f"{path}: region {region.name or '<gap>'} at ${region.address:04X} overruns WRAM0")

    for section in sections:
        cursor = section.origin
        last_field_addr: int | None = None
        for region in regions[section.first:section.first + section.count]:
            if region.kind is Kind.ALIAS:
                # An alias shares the address of the field it follows; it does not advance the cursor.
                if region.address != last_field_addr:
                    raise ParseError(
                        f"{path}: alias {region.name} at ${region.address:04X} does not share the "
                        f"preceding field's address ${last_field_addr:04X}"
                        if last_field_addr is not None else
                        f"{path}: alias {region.name} at ${region.address:04X} has no preceding field")
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


# --- Pass 2: raw-operand census -----------------------------------------------------------------

def symbol_table(regions: list[Region]) -> dict[str, int]:
    """label name -> address, for every Field and Alias (aliases resolve too: wSinglesCount)."""
    return {r.name: r.address for r in regions if r.name and r.kind in (Kind.FIELD, Kind.ALIAS)}


def _resolve_operand(operand: str, symbols: dict[str, int], path: Path, lineno: int) -> int | None:
    """Resolve one WRAM operand (bracketed or a bare 16-bit immediate) to a $C000-$DFFF address.

    Returns the address when the operand names a WRAM byte, or None when it does not - a `HIGH()`
    / `LOW()` address-byte wrapper (a computed pointer setup, not a byte access) or an expression
    that lands outside WRAM0. Every `w`-label is substituted with its Pass-1 address before the
    arithmetic is evaluated; a label with no layout entry, or an operand the evaluator cannot parse,
    is a hard error - a new addressing form can never slip through as a silent skip.
    """
    operand = operand.strip()
    upper = operand.upper()
    if "HIGH(" in upper or "LOW(" in upper:
        return None  # `ld a, HIGH(wOAMBuffer)` - the DMA source page, not a byte access

    def _sub(m: re.Match[str]) -> str:
        label = m.group(0)
        if label not in symbols:
            raise ParseError(f"{path}:{lineno}: operand {operand!r} names undefined WRAM label {label!r}")
        return f"${symbols[label]:X}"

    substituted = _WLABEL_RE.sub(_sub, operand)
    try:
        value = _eval_expr(substituted, path, lineno)
    except ParseError:
        raise ParseError(f"{path}:{lineno}: unresolved WRAM operand {operand!r}")
    return value if WRAM0_BASE <= value < WRAM0_END else None


def _mentions_wram(line: str) -> bool:
    """True if the line names a WRAM-range hex literal or a `w`-label - the census pre-filter."""
    if _WLABEL_RE.search(line):
        return True
    return any(WRAM0_BASE <= int(tok[1:], 16) < WRAM0_END for tok in _ANY_HEX_RE.findall(line))


def census(text: str, symbols: dict[str, int], path: Path) -> list[CensusEntry]:
    """Scan tetris.asm; return every static WRAM operand access as {address, refCount} rows.

    Only lines that name a WRAM literal or a `w`-label are examined. Each such line must be one of
    the recognized forms - an absolute load/store, a 16-bit immediate load, or a HIGH/LOW address
    wrapper - or it is a hard error (never a silent skip). Dynamic flows (`ld a, [hl]` after a
    computed pointer, `add`-built addresses) name no static operand and are out of census scope by
    construction.
    """
    counts: dict[int, int] = {}

    def record(addr: int) -> None:
        if WRAM0_BASE <= addr < WRAM0_END:
            counts[addr] = counts.get(addr, 0) + 1

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].strip()
        if not line or not _mentions_wram(line):
            continue

        mem = _ABS_STORE_RE.match(line) or _ABS_LOAD_RE.match(line)
        if mem:
            addr = _resolve_operand(mem.group(1), symbols, path, lineno)
            if addr is not None:
                record(addr)
            continue

        reg16 = _REG16_LOAD_RE.match(line)
        if reg16:
            addr = _resolve_operand(reg16.group(2), symbols, path, lineno)
            if addr is not None:
                record(addr)
            continue

        if _HIGHLOW_R8_RE.match(line):
            continue  # `ld a, HIGH(wLabel)` / `ld h, LOW(wLabel)` - address arithmetic, not a byte

        raise ParseError(f"{path}:{lineno}: unrecognized WRAM-referencing operand form: {line!r}")

    return [CensusEntry(addr, counts[addr]) for addr in sorted(counts)]


# --- Emit: wram_expected.h ----------------------------------------------------------------------

def _kind_token(kind: Kind) -> str:
    return f"WramKind::{kind.value}"


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
{common.banner("parse_wram.py", source_commit)}\
// The WRAM layout contract and the raw-operand census, both derived from the upstream disassembly.
//
// kWramLabels: every label in tetris/wram.asm and every anonymous gap between labels, each as
// {{name, address, size}} with a kind tag. Addresses derive from the section origins and the
// reservations that precede each label - nothing here is hand-typed. Fields own their reservation;
// an Alias shares an earlier field's address (wLineClearStats aliases wSinglesCount); a Gap is
// anonymous padding or an address jump. Regions tile each section with no overlap and no hole. The
// $C000 gameplay rows are pinned against the EngineState struct widths; the top-scores and
// Audio-RAM rows are carried for the state types that reuse this layout.
//
// kWramCensus: {{address, refCount}} for every STATIC WRAM operand access in tetris/tetris.asm -
// absolute loads/stores and 16-bit immediate loads, by raw literal or by label. audio.asm is not
// scanned (the sound driver runs as extracted bytes; its accesses are private by construction). The
// census makes every WRAM byte a game-side operand reaches - including the sprite / board / staging
// windows wram.asm leaves as anonymous gaps - a provable owner of some state unit. Sorted by address.
//
// Totals: {field_count} fields, {alias_count} alias, {gap_count} gaps, {len(sections)} sections; {len(census_rows)} census addresses.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

enum class WramKind : std::uint8_t {{ Field, Alias, Gap }};

// One region of the WRAM map. `size` is bytes reserved; for an Alias it repeats the field's size.
struct WramLabel {{
    std::string_view name;      // "" for a Gap
    std::uint16_t    address;
    std::uint16_t    size;
    WramKind         kind;
}};

// A WRAM0 section: [origin, end) tiled exactly by its regions [first, first + count).
struct WramSection {{
    std::string_view name;
    std::uint16_t    origin;
    std::uint16_t    end;       // one past the last byte
    std::uint16_t    first;     // index of the first region in kWramLabels
    std::uint16_t    count;     // number of regions in this section
}};

// One WRAM address a static game-side operand reaches, and how many access sites reach it.
struct WramCensus {{
    std::uint16_t address;
    std::uint16_t refCount;
}};

inline constexpr std::array<WramLabel, {len(regions)}> kWramLabels{{{{
{label_rows}
}}}};

inline constexpr std::array<WramSection, {len(sections)}> kWramSections{{{{
{section_rows}
}}}};

inline constexpr std::array<WramCensus, {len(census_rows)}> kWramCensus{{{{
{census_out}
}}}};

}}  // namespace kirpich::fixtures
"""


def _row_note(region: Region) -> str:
    if region.kind is Kind.GAP:
        return "gap"
    if region.kind is Kind.ALIAS:
        return "alias"
    return ""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's WRAM layout fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true", help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    wram_path: Path = args.source_root / "wram.asm"
    tetris_path: Path = args.source_root / "tetris.asm"
    for p in (wram_path, tetris_path):
        if not p.is_file():
            print(f"parse_wram: source file not found: {p}", file=sys.stderr)
            return 2

    regions, sections = parse_wram(wram_path.read_bytes().decode("utf-8"), wram_path)
    symbols = symbol_table(regions)
    census_rows = census(tetris_path.read_bytes().decode("utf-8"), symbols, tetris_path)
    commit = common.source_commit_of(args.source_root)

    fields = sum(1 for r in regions if r.kind is Kind.FIELD)
    aliases = sum(1 for r in regions if r.kind is Kind.ALIAS)
    gaps = sum(1 for r in regions if r.kind is Kind.GAP)
    total_refs = sum(c.ref_count for c in census_rows)
    print(f"parse_wram: {fields} fields + {aliases} alias + {gaps} gaps across {len(sections)} sections; "
          f"{len(census_rows)} census addresses ({total_refs} access sites); asserts passed.")

    if args.fixture_out is not None:
        content = emit_fixture(regions, sections, census_rows, commit)
        _assert_ascii(content, str(args.fixture_out))
        args.fixture_out.parent.mkdir(parents=True, exist_ok=True)
        args.fixture_out.write_text(content, encoding="ascii")
        print(f"parse_wram: wrote {args.fixture_out}")
    else:
        print("parse_wram: no --fixture-out given; nothing written (structural asserts still ran).",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
