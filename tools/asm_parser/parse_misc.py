#!/usr/bin/env python3
"""Parser for Kirpich's miscellaneous data unit - the loose tables and constants routed here.

Four data classes, all in tetris.asm:

  A - Raw-OAM object tables (4 tables / 25 objects / 100 bytes): {y, x, tile, attr} records copied
      verbatim into wOAMBuffer, bypassing the SpriteId renderer. The attr byte uses only bit 5 (OAM
      x-flip) across the whole corpus; the port struct OamObject carries {y, x, tile, xflip}. The
      tile is a raw gameplay-tileset VRAM index (NOT a SpriteId).
  B - Cursor coordinate tables (6 tables / 42 pairs / 84 bytes): {y, x} pairs indexed by a menu
      selection value x2. Five feed the generic digit-cursor positioner UpdateDigitCursor; the sixth
      (MusicTypeSpriteCoordinates) feeds the structurally identical PositionMusicTypeSprite. Three
      carry disassembler auto-names (Data_1615/16D2/1741); the port names them for their role
      (Type-A/Type-B level select, Type-B start-height select).
  C - Text strings (5 / 42 bytes): four win-screen strings in the raw gameplay letter tileset (NOT
      charmap values - a colliding byte draws a different glyph) plus PauseText, a `db "pause"`
      literal that assembles through charmap.asm into the CharTile space.
  D - Scalars (3): the $FF demo-recording sentinel (set once in StartRecordingDemo, compared twice in
      DemoSimulateJoypad + RecordDemo - three-site agreement, one emitted constant) and the
      CheckForCompletedRows quirk pair (the scan starts at field row 2 and checks 16 of 18 rows).

The four win strings and Push-Start object tiles share ONE gameplay letter tileset (A=$B5, U=$B2,
S=$0E, R=$BB, ...); the parser builds a tile->letter map spanning both and asserts it is globally
consistent - a strong cross-check that a byte always draws the same letter.

Emission set = the data `.inc` + the test fixture (no enum: the coordinate/OAM byte spaces have no
runtime symbol identity, and the string tiles are raw tileset indices; PauseText reuses CharTile):

  src/data/generated/misc_data.inc   4 OamObject arrays + 6 SpriteCoordinate arrays + 5 string arrays
                                     + 3 constants, at namespace scope inside src/data/misc.h
  tests/fixtures/misc_expected.h     the raw ROM bytes as plain integers + the 3 scalar values,
                                     independent of the typed surface so a defect there cannot mask
                                     the sweep in tests/test_misc.cpp

The field-geometry relation that turns the quirk's $C842 operand into a row index is imported from
parse_playing_field, so no address is hand-typed here; the row stride is additionally
re-derived from CheckForCompletedRows' own loop body and cross-checked.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Any deviation from the
expected structure is a hard error with a source citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import common
import parse_charmap
from parse_playing_field import ROW_STRIDE, WIPE_COUNT, WRAM_BASE

# --- Registries (upstream label, port accessor, count, ...) -------------------------------------
#
# Order here is the emission, fixture, and test order. The C++ test walks the same order.

# (label, accessor, object_count, letters) - letters annotates the Push-Start tiles for the
# cross-tileset consistency check and the emitted comments; None for the face tables.
OAM_TABLES: list[tuple[str, str, int, str | None]] = [
    ("MarioLuigiFaceObjects", "marioLuigiFaceObjects", 8, None),
    ("MarioFaceObjects", "marioFaceObjects", 4, None),
    ("LuigiFaceObjects", "luigiFaceObjects", 4, None),
    ("PushStartObjects", "pushStartObjects", 9, "PUSHSTART"),
]

# (label, accessor, pair_count, has_trailing_zero, index_note)
COORD_TABLES: list[tuple[str, str, int, bool, str]] = [
    ("Data_1615", "typeALevelCursorCoordinates", 10, False, "level 0-9"),
    ("Data_16D2", "typeBLevelCursorCoordinates", 10, False, "level 0-9"),
    ("Data_1741", "typeBStartHeightCursorCoordinates", 6, True, "start height 0-5"),
    ("MarioStartHeightCursorCoordinates", "marioStartHeightCursorCoordinates", 6, False,
     "start height 0-5"),
    ("LuigiStartHeightCursorCoordinates", "luigiStartHeightCursorCoordinates", 6, False,
     "start height 0-5"),
    ("MusicTypeSpriteCoordinates", "musicTypeSpriteCoordinates", 4, False, "MusicType $1C-$1F"),
]

# (label, accessor, expected_text) - raw gameplay-tileset strings; the text is the source contract.
RAW_STRINGS: list[tuple[str, str, str]] = [
    ("DeuceText", "deuceText", "DEUCE!"),
    ("MarioWinsText", "marioWinsText", "MARIO WINS!"),
    ("LuigiWinsText", "luigiWinsText", "LUIGI WINS!"),
    ("AdvantageText", "advantageText", "ADVANTAGE"),
]

PAUSE_LABEL = "PauseText"
PAUSE_ACCESSOR = "pauseText"
PAUSE_TEXT = "pause"

OAM_RECORD_BYTES = 4
COORD_RECORD_BYTES = 2
ATTR_XFLIP = 0x20          # OAM attribute bit 5 - the only attr bit the corpus uses
PADDING_BYTE = 0x00        # Data_1741's trailing dead byte

OAM_OBJECT_COUNT = sum(n for _l, _a, n, _t in OAM_TABLES)          # 25
COORD_PAIR_COUNT = sum(n for _l, _a, n, _z, _i in COORD_TABLES)    # 42

_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_.]*)::')
_TOP_LABEL_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*::')
_DB_RE = re.compile(r'^db\b(?P<operand>.*)$')
_HEX_BYTE_RE = re.compile(r'^\$([0-9A-Fa-f]{2})$')

# Scalar-site instruction anchors (decommented, whitespace-normalized lines).
_LD_A_IMM_RE = re.compile(r'^ld a, \$([0-9A-Fa-f]{2})$')
_CP_A_IMM_RE = re.compile(r'^cp a, \$([0-9A-Fa-f]{2})$')
_LD_HL_ADDR_RE = re.compile(r'^ld hl, \$([0-9A-Fa-f]{4})$')
_LD_DE_ADDR_RE = re.compile(r'^ld de, \$([0-9A-Fa-f]{4})$')
_LD_B_DEC_RE = re.compile(r'^ld b, (\d+)$')
_DEMO_RECORDING_STORE = "ldh [hDemoRecording], a"
_DEMO_RECORDING_LOAD = "ldh a, [hDemoRecording]"


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_misc"


# --- Parsed structures --------------------------------------------------------------------------

@dataclass(frozen=True)
class OamObject:
    """One raw-OAM object: {y, x, tile} plus the x-flip decoded from the attribute byte."""

    y: int
    x: int
    tile: int
    xflip: bool


@dataclass(frozen=True)
class Coordinate:
    """One {y, x} cursor coordinate pair (ROM byte order: y then x)."""

    y: int
    x: int


@dataclass
class OamTable:
    label: str
    accessor: str
    objects: list[OamObject]
    raw_bytes: list[int]


@dataclass
class CoordTable:
    label: str
    accessor: str
    index_note: str
    pairs: list[Coordinate]
    raw_bytes: list[int]      # table bytes only (trailing padding excluded)


@dataclass
class TextString:
    label: str
    accessor: str
    text: str                 # source contract (uppercase for raw strings, "pause" for PauseText)
    raw_bytes: list[int]      # tile bytes
    typed: bool               # True => emit CharTile enumerators (PauseText); False => raw uint8_t


@dataclass(frozen=True)
class Scalars:
    demo_recording_magic: int
    completed_row_first: int
    completed_row_count: int


# --- Operand + block helpers --------------------------------------------------------------------

def _split_operands(operand: str) -> list[str]:
    operand = operand.split(";", 1)[0]
    return [tok.strip() for tok in operand.split(",") if tok.strip()]


def _parse_byte(token: str, path: Path, lineno: int) -> int:
    match = _HEX_BYTE_RE.match(token)
    if not match:
        raise ParseError(f"{path}:{lineno}: not a two-digit hex byte: {token!r}")
    return int(match.group(1), 16)


def _collect_hex_block(lines: list[str], label: str, path: Path) -> list[int]:
    """The contiguous run of `db $HH` bytes under `label::`, up to the next label or non-db line.

    Blank lines and full-line comments inside the run are skipped (so a stray `db $00` a blank line
    below the table is still collected - Data_1741's trailing padding relies on this)."""
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{label}::")]
    if len(hits) != 1:
        raise ParseError(f"{path}: label {label}:: must appear exactly once, found {len(hits)}")

    out: list[int] = []
    started = False
    for offset, raw in enumerate(lines[hits[0] + 1:], start=hits[0] + 2):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if _LABEL_RE.match(line):
            break
        db_match = _DB_RE.match(line)
        if not db_match:
            break
        tokens = _split_operands(db_match.group("operand"))
        if not tokens:
            raise ParseError(f"{path}:{offset}: empty db operand under {label}::")
        for tok in tokens:
            out.append(_parse_byte(tok, path, offset))
        started = True
    if not started:
        raise ParseError(f"{path}: {label}:: has no db bytes")
    return out


def _routine_body(lines: list[str], label: str, path: Path) -> list[str]:
    """The decommented, whitespace-stripped instruction lines of a routine, from `label::` up to
    the next top-level `name::` label. Local `.labels` stay in the body."""
    start = None
    for i, raw in enumerate(lines):
        if raw.strip().startswith(f"{label}::"):
            start = i
            break
    if start is None:
        raise ParseError(f"{path}: routine {label}:: not found")
    body: list[str] = []
    for raw in lines[start + 1:]:
        stripped = raw.split(";", 1)[0].strip()
        if not stripped:
            continue
        if _TOP_LABEL_RE.match(stripped):
            break
        body.append(stripped)
    if not body:
        raise ParseError(f"{path}: routine {label}:: has an empty body")
    return body


# --- Shared letter tileset (win strings + Push-Start) --------------------------------------------

def _record_letter(table: dict[int, str], tile: int, char: str, ctx: str, path: Path) -> None:
    """Assert `tile` always draws the same letter across every string/object that uses it."""
    prior = table.get(tile)
    if prior is None:
        table[tile] = char
    elif prior != char:
        raise ParseError(
            f"{path}: {ctx}: tile ${tile:02X} draws {char!r} here but {prior!r} elsewhere "
            f"- the gameplay letter tileset is not self-consistent")


# --- Section A: raw-OAM object tables -----------------------------------------------------------

def _decode_oam_object(rec: list[int], ctx: str, path: Path) -> OamObject:
    y, x, tile, attr = rec
    if attr & ~ATTR_XFLIP:
        raise ParseError(
            f"{path}: {ctx}: attr byte ${attr:02X} sets a bit outside the x-flip bit (${ATTR_XFLIP:02X})")
    return OamObject(y=y, x=x, tile=tile, xflip=bool(attr & ATTR_XFLIP))


def _parse_oam_table(lines: list[str], label: str, accessor: str, count: int,
                     letters: str | None, letter_tiles: dict[int, str], path: Path) -> OamTable:
    raw = _collect_hex_block(lines, label, path)
    if len(raw) != count * OAM_RECORD_BYTES:
        raise ParseError(
            f"{path}: {label} holds {len(raw)} bytes, expected {count} x {OAM_RECORD_BYTES} "
            f"= {count * OAM_RECORD_BYTES}")
    if letters is not None and len(letters) != count:
        raise ParseError(
            f"{path}: {label} letter annotation {letters!r} has {len(letters)} chars, "
            f"expected {count}")
    objects: list[OamObject] = []
    for idx in range(count):
        rec = raw[idx * OAM_RECORD_BYTES:(idx + 1) * OAM_RECORD_BYTES]
        obj = _decode_oam_object(rec, f"{label}[{idx}]", path)
        objects.append(obj)
        if letters is not None:
            _record_letter(letter_tiles, obj.tile, letters[idx], f"{label}[{idx}]", path)
    return OamTable(label, accessor, objects, raw)


# --- Section B: cursor coordinate tables --------------------------------------------------------

def _parse_coord_table(lines: list[str], label: str, accessor: str, count: int,
                       has_trailing_zero: bool, index_note: str, path: Path) -> CoordTable:
    raw = _collect_hex_block(lines, label, path)
    table_bytes = count * COORD_RECORD_BYTES
    if has_trailing_zero:
        if len(raw) != table_bytes + 1:
            raise ParseError(
                f"{path}: {label} holds {len(raw)} bytes, expected {count} pairs "
                f"({table_bytes}) + one trailing padding byte")
        if raw[-1] != PADDING_BYTE:
            raise ParseError(
                f"{path}: {label} trailing byte is ${raw[-1]:02X}, expected the dead padding "
                f"${PADDING_BYTE:02X}")
        raw = raw[:-1]
    elif len(raw) != table_bytes:
        raise ParseError(
            f"{path}: {label} holds {len(raw)} bytes, expected {count} x {COORD_RECORD_BYTES} "
            f"= {table_bytes}")
    pairs = [Coordinate(y=raw[i], x=raw[i + 1]) for i in range(0, len(raw), COORD_RECORD_BYTES)]
    return CoordTable(label, accessor, index_note, pairs, raw)


# --- Section C: text strings --------------------------------------------------------------------

def _parse_raw_string(lines: list[str], label: str, accessor: str, text: str,
                      letter_tiles: dict[int, str], path: Path) -> TextString:
    raw = _collect_hex_block(lines, label, path)
    if len(raw) != len(text):
        raise ParseError(
            f"{path}: {label} holds {len(raw)} tile bytes but its text {text!r} is {len(text)} "
            f"characters")
    for ch, tile in zip(text, raw):
        _record_letter(letter_tiles, tile, ch, f"{label} {ch!r}", path)
    return TextString(label, accessor, text, raw, typed=False)


def _collect_string_literal(lines: list[str], label: str, path: Path) -> str:
    """The single `db "..."` literal under `label::` (PauseText)."""
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{label}::")]
    if len(hits) != 1:
        raise ParseError(f"{path}: label {label}:: must appear exactly once, found {len(hits)}")
    for offset, raw in enumerate(lines[hits[0] + 1:], start=hits[0] + 2):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        match = re.match(r'^db "([^"]*)"$', line.split(";", 1)[0].strip())
        if not match:
            raise ParseError(f'{path}:{offset}: {label}:: expected a single `db "..."`, got {line!r}')
        return match.group(1)
    raise ParseError(f"{path}: {label}:: has no db line")


def _greedy_segments(text: str, table: dict[str, int], path: Path) -> list[tuple[str, int]]:
    """Encode `text` through the charmap by greedy longest-match (RGBDS semantics), returning the
    (sequence, tile) segments so each tile can be named as a CharTile enumerator."""
    seqs = sorted(table.keys(), key=len, reverse=True)
    segments: list[tuple[str, int]] = []
    i = 0
    while i < len(text):
        for seq in seqs:
            if text.startswith(seq, i):
                segments.append((seq, table[seq]))
                i += len(seq)
                break
        else:
            raise ParseError(f"{path}: character {text[i]!r} in {text!r} has no charmap mapping")
    return segments


def _parse_pause_text(lines: list[str], charmap: dict[str, int],
                      charmap_path: Path, tetris_path: Path) -> tuple[TextString, list[str]]:
    literal = _collect_string_literal(lines, PAUSE_LABEL, tetris_path)
    if literal != PAUSE_TEXT:
        raise ParseError(
            f"{tetris_path}: {PAUSE_LABEL} literal is {literal!r}, expected {PAUSE_TEXT!r}")
    segments = _greedy_segments(literal, charmap, charmap_path)
    raw = [tile for _seq, tile in segments]
    names = [parse_charmap.tile_name(seq, charmap_path) for seq, _tile in segments]
    return TextString(PAUSE_LABEL, PAUSE_ACCESSOR, literal, raw, typed=True), names


# --- Section D: scalars -------------------------------------------------------------------------

def _parse_sentinel(lines: list[str], path: Path) -> int:
    """The $FF demo-recording sentinel: read where StartRecordingDemo sets it, assert both compare
    sites (DemoSimulateJoypad, RecordDemo) test the same value."""
    set_body = _routine_body(lines, "StartRecordingDemo", path)
    set_value = None
    for i, ln in enumerate(set_body):
        if ln == _DEMO_RECORDING_STORE:
            prev = set_body[i - 1] if i > 0 else ""
            match = _LD_A_IMM_RE.match(prev)
            if not match:
                raise ParseError(
                    f"{path}: StartRecordingDemo: {_DEMO_RECORDING_STORE!r} is not preceded by "
                    f"`ld a, $HH` (got {prev!r})")
            set_value = int(match.group(1), 16)
            break
    if set_value is None:
        raise ParseError(f"{path}: StartRecordingDemo: no {_DEMO_RECORDING_STORE!r} store found")

    for routine in ("DemoSimulateJoypad", "RecordDemo"):
        body = _routine_body(lines, routine, path)
        compared = None
        for i, ln in enumerate(body):
            if ln == _DEMO_RECORDING_LOAD:
                nxt = body[i + 1] if i + 1 < len(body) else ""
                match = _CP_A_IMM_RE.match(nxt)
                if not match:
                    raise ParseError(
                        f"{path}: {routine}: {_DEMO_RECORDING_LOAD!r} is not followed by "
                        f"`cp a, $HH` (got {nxt!r})")
                compared = int(match.group(1), 16)
                break
        if compared is None:
            raise ParseError(f"{path}: {routine}: no {_DEMO_RECORDING_LOAD!r} compare found")
        if compared != set_value:
            raise ParseError(
                f"{path}: sentinel disagreement: StartRecordingDemo sets ${set_value:02X} but "
                f"{routine} compares ${compared:02X}")
    return set_value


def _parse_quirk(lines: list[str], path: Path) -> tuple[int, int]:
    """The CheckForCompletedRows quirk: the first-row index (from $C842 via the playing-field
    relation, self-checked against the loop's own $20 row stride) and the 16-of-18 row count."""
    body = _routine_body(lines, "CheckForCompletedRows", path)
    row_addr = row_count = stride = None
    for ln in body:
        if row_addr is None:
            m = _LD_HL_ADDR_RE.match(ln)
            if m:
                row_addr = int(m.group(1), 16)
        if row_count is None:
            m = _LD_B_DEC_RE.match(ln)
            if m:
                row_count = int(m.group(1))
        if stride is None:
            m = _LD_DE_ADDR_RE.match(ln)
            if m:
                stride = int(m.group(1), 16)
    if row_addr is None or row_count is None or stride is None:
        raise ParseError(
            f"{path}: CheckForCompletedRows: could not read all of the row address / count / stride "
            f"(got addr={row_addr}, count={row_count}, stride={stride})")
    if stride != ROW_STRIDE:
        raise ParseError(
            f"{path}: CheckForCompletedRows: loop row stride ${stride:04X} != the playing-field stride "
            f"${ROW_STRIDE:04X}")
    offset = row_addr - WRAM_BASE
    if offset < 0 or offset % ROW_STRIDE:
        raise ParseError(
            f"{path}: CheckForCompletedRows: start address ${row_addr:04X} is not a whole number of "
            f"rows above the field origin ${WRAM_BASE:04X}")
    first_row = offset // ROW_STRIDE
    if not 0 <= first_row < WIPE_COUNT:
        raise ParseError(
            f"{path}: CheckForCompletedRows: derived first row {first_row} is outside 0..{WIPE_COUNT - 1}")
    if first_row + row_count != WIPE_COUNT:
        raise ParseError(
            f"{path}: CheckForCompletedRows: first row {first_row} + count {row_count} != "
            f"{WIPE_COUNT} field rows - the 'skip the top rows' quirk invariant is broken")
    return first_row, row_count


# --- Parse + assemble ---------------------------------------------------------------------------

@dataclass
class MiscData:
    oam_tables: list[OamTable]
    coord_tables: list[CoordTable]
    strings: list[TextString]
    pause_names: list[str]
    scalars: Scalars


def parse_misc(tetris_text: str, tetris_path: Path,
               charmap: dict[str, int], charmap_path: Path) -> MiscData:
    lines = tetris_text.splitlines()
    letter_tiles: dict[int, str] = {}

    oam_tables = [
        _parse_oam_table(lines, label, accessor, count, letters, letter_tiles, tetris_path)
        for label, accessor, count, letters in OAM_TABLES
    ]
    coord_tables = [
        _parse_coord_table(lines, label, accessor, count, trailing, note, tetris_path)
        for label, accessor, count, trailing, note in COORD_TABLES
    ]
    strings = [
        _parse_raw_string(lines, label, accessor, text, letter_tiles, tetris_path)
        for label, accessor, text in RAW_STRINGS
    ]
    pause, pause_names = _parse_pause_text(lines, charmap, charmap_path, tetris_path)
    strings.append(pause)

    first_row, row_count = _parse_quirk(lines, tetris_path)
    scalars = Scalars(
        demo_recording_magic=_parse_sentinel(lines, tetris_path),
        completed_row_first=first_row,
        completed_row_count=row_count,
    )

    total_objects = sum(len(t.objects) for t in oam_tables)
    if total_objects != OAM_OBJECT_COUNT:
        raise ParseError(f"{tetris_path}: parsed {total_objects} OAM objects, expected {OAM_OBJECT_COUNT}")
    total_pairs = sum(len(t.pairs) for t in coord_tables)
    if total_pairs != COORD_PAIR_COUNT:
        raise ParseError(f"{tetris_path}: parsed {total_pairs} coordinate pairs, expected {COORD_PAIR_COUNT}")
    return MiscData(oam_tables, coord_tables, strings, pause_names, scalars)


# --- Emit: misc_data.inc ------------------------------------------------------------------------

def _cap(accessor: str) -> str:
    return "k" + accessor[0].upper() + accessor[1:]


def _readable(char: str) -> str:
    return "space" if char == " " else char


def _emit_oam_block(table: OamTable, letters: str | None) -> str:
    rows = []
    for idx, obj in enumerate(table.objects):
        comment = f"  // obj {idx}"
        if letters is not None:
            comment += f" '{letters[idx]}'"
        if obj.xflip:
            comment += " (x-flip)"
        rows.append(
            f"    {{ .y = {obj.y}, .x = {obj.x}, .tile = 0x{obj.tile:02X}, "
            f".xflip = {'true' if obj.xflip else 'false'} }},{comment}")
    body = "\n".join(rows)
    return (f"// {table.label}\n"
            f"inline constexpr std::array<OamObject, {len(table.objects)}> {_cap(table.accessor)}{{{{\n"
            f"{body}\n}}}};")


def _emit_coord_block(table: CoordTable) -> str:
    rows = "\n".join(
        f"    {{ .y = {p.y}, .x = {p.x} }},  // {table.index_note.split()[0]} {idx}"
        for idx, p in enumerate(table.pairs))
    return (f"// {table.label} (index = {table.index_note})\n"
            f"inline constexpr std::array<SpriteCoordinate, {len(table.pairs)}> "
            f"{_cap(table.accessor)}{{{{\n{rows}\n}}}};")


def _emit_string_block(string: TextString, pause_names: list[str]) -> str:
    if string.typed:
        values = ", ".join(f"CharTile::{name}" for name in pause_names)
        return (f'// {string.label}  "{string.text}"  (charmap-encoded into the CharTile space)\n'
                f"inline constexpr std::array<CharTile, {len(string.raw_bytes)}> "
                f"{_cap(string.accessor)}{{{{ {values} }}}};")
    pieces = ", ".join(f"0x{b:02X}" for b in string.raw_bytes)
    letters = " ".join(_readable(c) for c in string.text)
    return (f'// {string.label}  "{string.text}"  (raw gameplay letter tileset, NOT charmap)\n'
            f"inline constexpr std::array<std::uint8_t, {len(string.raw_bytes)}> "
            f"{_cap(string.accessor)}{{{{ {pieces} }}}};  // {letters}")


def emit_inc(data: MiscData, source_commit: str) -> str:
    oam = "\n\n".join(
        _emit_oam_block(t, letters) for t, (_l, _a, _c, letters) in zip(data.oam_tables, OAM_TABLES))
    coords = "\n\n".join(_emit_coord_block(t) for t in data.coord_tables)
    strings = "\n\n".join(_emit_string_block(s, data.pause_names) for s in data.strings)
    scalars = (
        "// Section D scalars, all derived from the routines that use them (not hand-typed):\n"
        f"inline constexpr std::uint8_t kDemoRecordingEnabledMagic = 0x{data.scalars.demo_recording_magic:02X};\n"
        f"inline constexpr std::uint8_t kCompletedRowCheckFirstRow = {data.scalars.completed_row_first};\n"
        f"inline constexpr std::uint8_t kCompletedRowCheckRowCount = {data.scalars.completed_row_count};")
    return f"""{common.banner("parse_misc.py", source_commit)}\
// Included at namespace scope inside src/data/misc.h (inside `namespace kirpich`), which defines
// OamObject and SpriteCoordinate first and includes CharTile. Section A: raw-OAM object tables
// (bypass the SpriteId renderer). Section B: {len(data.coord_tables)} cursor coordinate tables.
// Section C: the win-screen strings (raw tileset) + PauseText (charmap). Section D: the three
// derived scalars.

// ==== A: raw-OAM object tables ====
{oam}

// ==== B: cursor coordinate tables ====
{coords}

// ==== C: text strings ====
{strings}

// ==== D: scalars ====
{scalars}
"""


# --- Emit: misc_expected.h (independent raw-byte fixture) ---------------------------------------

def _byte_row(values: list[int]) -> str:
    return ", ".join(f"0x{b:02X}" for b in values)


def _flat_block(name: str, tables_bytes: list[tuple[str, int, list[int]]]) -> tuple[str, str, int]:
    """Return (descriptor rows, flat-byte lines, total) for one group's fixture."""
    flat: list[int] = []
    desc: list[str] = []
    for label, count, raw in tables_bytes:
        desc.append(f"    {{ .byte_offset = {len(flat)}, .count = {count} }},  // {label}")
        flat.extend(raw)
    flat_lines = "\n".join("    " + _byte_row(flat[i:i + 16]) + "," for i in range(0, len(flat), 16))
    return "\n".join(desc), flat_lines, len(flat)


def emit_fixture(data: MiscData, source_commit: str) -> str:
    oam_desc, oam_flat, oam_total = _flat_block(
        "oam", [(t.label, len(t.objects), t.raw_bytes) for t in data.oam_tables])
    coord_desc, coord_flat, coord_total = _flat_block(
        "coord", [(t.label, len(t.pairs), t.raw_bytes) for t in data.coord_tables])
    raw_strings = [s for s in data.strings if not s.typed]
    text_desc, text_flat, text_total = _flat_block(
        "text", [(s.label, len(s.raw_bytes), s.raw_bytes) for s in raw_strings])
    pause = next(s for s in data.strings if s.typed)
    pause_bytes = _byte_row(pause.raw_bytes)
    return f"""#pragma once
{common.banner("parse_misc.py", source_commit)}\
// Independent fixture for the full-corpus misc sweep: the ROM's raw bytes as plain integers, plus
// the three scalar values. Each descriptor row slices its group's flat byte array for one table, in
// the same order as src/data/misc.h. It holds no port type, so a defect in misc.h cannot mask the
// sweep. tests/test_misc.cpp re-derives every OamObject / SpriteCoordinate / string from these
// bytes and compares to the generated arrays.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// One table's slice into its group's flat byte array.
struct MiscTableSlice {{
    std::uint16_t byte_offset;
    std::uint8_t  count;        // objects / pairs / characters, per group
}};

// --- A: raw-OAM object tables ({len(data.oam_tables)} tables, {oam_total} bytes; 4 bytes/object: y, x, tile, attr) ---
inline constexpr std::array<MiscTableSlice, {len(data.oam_tables)}> kExpectedMiscOamTables{{{{
{oam_desc}
}}}};
inline constexpr std::array<std::uint8_t, {oam_total}> kExpectedMiscOamBytes{{{{
{oam_flat}
}}}};

// --- B: cursor coordinate tables ({len(data.coord_tables)} tables, {coord_total} bytes; 2 bytes/pair: y, x) ---
inline constexpr std::array<MiscTableSlice, {len(data.coord_tables)}> kExpectedMiscCoordTables{{{{
{coord_desc}
}}}};
inline constexpr std::array<std::uint8_t, {coord_total}> kExpectedMiscCoordBytes{{{{
{coord_flat}
}}}};

// --- C: win-screen strings ({len(raw_strings)} strings, {text_total} bytes; raw gameplay tileset) ---
inline constexpr std::array<MiscTableSlice, {len(raw_strings)}> kExpectedMiscTextStrings{{{{
{text_desc}
}}}};
inline constexpr std::array<std::uint8_t, {text_total}> kExpectedMiscTextBytes{{{{
{text_flat}
}}}};

// PauseText: the charmap-encoded tile bytes ("pause" -> CharTile values).
inline constexpr std::array<std::uint8_t, {len(pause.raw_bytes)}> kExpectedPauseTextBytes{{{{ {pause_bytes} }}}};

// --- D: scalars ---
inline constexpr std::uint8_t kExpectedDemoRecordingMagic = 0x{data.scalars.demo_recording_magic:02X};
inline constexpr std::uint8_t kExpectedCompletedRowCheckFirstRow = {data.scalars.completed_row_first};
inline constexpr std::uint8_t kExpectedCompletedRowCheckRowCount = {data.scalars.completed_row_count};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's misc data unit + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    tetris_path: Path = args.source_root / "tetris.asm"
    charmap_path: Path = args.source_root / "charmap.asm"
    for needed in (tetris_path, charmap_path):
        if not needed.is_file():
            print(f"parse_misc: source file not found: {needed}", file=sys.stderr)
            return 2

    tetris_text = tetris_path.read_bytes().decode("utf-8")
    charmap = dict(parse_charmap.parse_charmap(charmap_path.read_bytes().decode("utf-8"), charmap_path))
    data = parse_misc(tetris_text, tetris_path, charmap, charmap_path)
    commit = common.source_commit_of(args.source_root)

    print(f"parse_misc: {len(data.oam_tables)} OAM tables / {sum(len(t.objects) for t in data.oam_tables)} "
          f"objects, {len(data.coord_tables)} coord tables / {sum(len(t.pairs) for t in data.coord_tables)} "
          f"pairs, {len(data.strings)} strings, 3 scalars; asserts passed.")

    outputs = {
        args.inc_out: emit_inc(data, commit),
        args.fixture_out: emit_fixture(data, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        _assert_ascii(content, str(out_path))
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_misc: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_misc: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
