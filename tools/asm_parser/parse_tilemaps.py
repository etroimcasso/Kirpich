#!/usr/bin/env python3
"""Parser for Kirpich's background tilemaps - the static screens the game draws.

The disassembly stores each screen as a run of `db` lines: a rectangle of tile indices, sometimes
written as raw `$HH` bytes, sometimes as `db "text"` rows the assembler resolves through charmap.asm
(the character map already ported), sometimes a mix of both on one line. There is no runtime lookup table -
the game code references each label directly at a screen transition - so this unit ports the tile
grids themselves, nothing more. Twenty-two labels fall into six consumption classes distinguished by
their loader and shape:

  C1  full-screen 20x18 maps         (LoadTilemap)              9 labels
  C2  20-wide banner strips          (LoadTilemap.columnLoop)   3 labels
  C3  10x18 playing-field overlays   (LoadPlayingFieldTilemap)  2 labels, $FF-terminated
  C4  8-wide window message blocks   (Call_1F7D)                3 labels
  C5  1-wide 7-tall tower columns    (LoadTilemap9C00Row)       4 labels, written vertically
  C6  16-tile congratulations strip  (GameState_2C, printed)    1 local label

The cell space is raw tile indices with no upstream names, so there is no enum: every cell is a
`std::uint8_t`. The emitted surface is the composition - each screen as a row-major grid (the field
overlays' $FF terminator and the towers' vertical stride are serialization, dropped from the grid) -
plus four geometry constants read from the loaders. Destination addresses, per-screen cell pokes,
and the print cadence are consumer behavior: recorded in docs/contracts/tilemaps.md with line
anchors, not ported here.

Emission set = the data `.inc` + the test fixture:

  src/data/generated/tilemaps_data.inc   the four geometry constants + all 22 composed grids,
                                         included inside `namespace kirpich` by src/data/tilemaps.h
  tests/fixtures/tilemaps_expected.h     the 22 flat byte arrays in serialization order and form
                                         ($FF sentinels kept), independent of the composed surface

Text rows are decoded through charmap.asm by the same greedy longest-match RGBDS uses - so the one
two-code-point ligature ".<right-double-quote>" ($9D) wins over "." when it appears, exactly as the
assembler resolved it. The charmap parse is reused from parse_charmap (its 47-entry contract is
asserted there); the encoder is local so this unit does not reopen the charmap parser's byte-identity gates.

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

# Full-screen and banner maps are 20 tiles wide (the DMG BG viewport, SCRN_X_B); C1 maps are 18 tall
# (SCRN_Y_B). Window blocks are 8 wide. Tower strips are 7 tall. Field overlays are the playing
# field's shape (10 x 18). Each value is re-derived from the source below and asserted to equal these.
EXPECTED_SCREEN_COLS = 20
EXPECTED_SCREEN_ROWS = 18
EXPECTED_WINDOW_COLS = 8
EXPECTED_TOWER_ROWS = 7
PLAYING_FIELD_COLS = 10   # the playing-field contract; the field overlays are field-shaped by construction
PLAYING_FIELD_ROWS = 18
FIELD_SENTINEL = 0xFF     # LoadPlayingFieldTilemap stops on this byte, then starts the wipe

CORPUS_TOTAL = 4110       # sum of every label's serialized byte count

# The congratulations strip lives as a local label inside a game-state routine, not at top level.
CONGRATS_PARENT = "GameState_2C"
CONGRATS_LOCAL = ".data_12F5"

# Loader routines whose shape constants and sentinels this unit reads or cross-checks.
LOAD_TILEMAP = "LoadTilemap"
LOAD_PLAYING_FIELD = "LoadPlayingFieldTilemap"
LOAD_TOWER_ROW = "LoadTilemap9C00Row"
WINDOW_LOADER = "Call_1F7D"
COLUMN_LOOP = "LoadTilemap.columnLoop"

# The label registry: (upstream label, port constant, class). Upstream labels are structural anchors
# the parser matches against; they are not mirrored as C++ names. The three anonymous upstream labels
# (Data_293E, Data_2976, .data_12F5) are named by role.
LABELS = [
    # C1 - full-screen 20x18.
    ("TypeAGameplayTilemap",           "kTypeAGameplayTilemap",           "C1"),
    ("TypeBGameplayTilemap",           "kTypeBGameplayTilemap",           "C1"),
    ("CopyrightScreenTilemap",         "kCopyrightScreenTilemap",         "C1"),
    ("TitleScreenTilemap",             "kTitleScreenTilemap",             "C1"),
    ("ConfigScreenTilemap",            "kConfigScreenTilemap",            "C1"),
    ("TypeADifficultyTilemap",         "kTypeADifficultyTilemap",         "C1"),
    ("TypeBDifficultyTilemap",         "kTypeBDifficultyTilemap",         "C1"),
    ("MultiplayerDifficultyTilemap",   "kMultiplayerDifficultyTilemap",   "C1"),
    ("MultiplayerGameplayTilemap",     "kMultiplayerGameplayTilemap",     "C1"),
    # C2 - 20-wide banner strips (rows vary: 4 / 6 / 4).
    ("MultiplayerVictoryTopTilemap",    "kMultiplayerVictoryTopTilemap",    "C2"),
    ("MultiplayerVictoryBottomTilemap", "kMultiplayerVictoryBottomTilemap", "C2"),
    ("BuranBackdropTilemap",            "kBuranBackdropTilemap",            "C2"),
    # C3 - 10x18 playing-field overlays, $FF-terminated.
    ("ScoreboardTilemap",              "kScoreboardTilemap",              "C3"),
    ("DancersTilemap",                 "kDancersTilemap",                 "C3"),
    # C4 - 8-wide window blocks (rows vary: 10 / 7 / 6).
    ("PauseMessageTilemap",            "kPauseMessageTilemap",            "C4"),
    ("Data_293E",                      "kGameOverTilemap",                "C4"),
    ("Data_2976",                      "kTryAgainTilemap",                "C4"),
    # C5 - 1-wide 7-tall tower columns (stored as one 7-byte line, written vertically).
    ("LeftTowerLeftSideTilemap",       "kLeftTowerLeftSideTilemap",       "C5"),
    ("LeftTowerRightSideTilemap",      "kLeftTowerRightSideTilemap",      "C5"),
    ("RightTowerLeftSideTilemap",      "kRightTowerLeftSideTilemap",      "C5"),
    ("RightTowerRightSideTilemap",     "kRightTowerRightSideTilemap",     "C5"),
    # C6 - 16-tile congratulations print strip (local label).
    (CONGRATS_LOCAL,                   "kCongratulationsTilemap",         "C6"),
]

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")

_TOP_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
_LD_DE_LABEL_RE = re.compile(r"^ld\s+de,\s*(\S+)$")
_LD_B_RE = re.compile(r"^ld\s+b,\s*(?:\$([0-9A-Fa-f]+)|(\d+))$")
_CALL_RE = re.compile(r"^call\s+(\S+)$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_tilemaps"


# --- charmap decode (greedy longest-match, kept local) -------------------------------------------

def build_charmap_table(source_root: Path) -> dict[str, int]:
    """The charmap.asm sequence -> tile table, parsed and contract-asserted by parse_charmap."""
    charmap_path = source_root / "charmap.asm"
    text = charmap_path.read_bytes().decode("utf-8")
    return dict(parse_charmap.parse_charmap(text, charmap_path))


def encode_text(text: str, table: dict[str, int], path: Path, lineno: int) -> list[int]:
    """Encode a `db "..."` run to tile bytes exactly as RGBDS did: greedy longest-match, so a
    multi-code-point sequence (the ".<U+201D>" ligature) is consumed whole before its prefix."""
    seqs = sorted(table.keys(), key=len, reverse=True)
    out: list[int] = []
    i = 0
    while i < len(text):
        for seq in seqs:
            if text.startswith(seq, i):
                out.append(table[seq])
                i += len(seq)
                break
        else:
            raise ParseError(
                f"{path}:{lineno}: character {text[i]!r} in {text!r} has no charmap mapping"
            )
    return out


# --- db line tokenizing + decoding --------------------------------------------------------------

def _decomment(line: str) -> str:
    """Strip a trailing `; comment` and surrounding whitespace. No corpus string contains `;`."""
    body, _sep, _comment = line.partition(";")
    return body.strip()


def _tokenize_db(operand: str, path: Path, lineno: int) -> list[tuple[str, object]]:
    """Split a `db` operand into ordered segments: ('str', text) for a "..." run, ('byte', value)
    for a $HH literal. Quotes bound strings and no corpus string contains an ASCII double quote, so
    scanning by quote is unambiguous; segments are comma-separated outside strings."""
    segs: list[tuple[str, object]] = []
    i, n = 0, len(operand)
    while i < n:
        ch = operand[i]
        if ch in " \t,":
            i += 1
            continue
        if ch == '"':
            end = operand.find('"', i + 1)
            if end == -1:
                raise ParseError(f"{path}:{lineno}: unterminated string in db operand {operand!r}")
            segs.append(("str", operand[i + 1:end]))
            i = end + 1
        elif ch == "$":
            j = i + 1
            while j < n and operand[j] in _HEX_DIGITS:
                j += 1
            if j == i + 1:
                raise ParseError(f"{path}:{lineno}: `$` without hex digits in {operand!r}")
            segs.append(("byte", int(operand[i + 1:j], 16)))
            i = j
        else:
            raise ParseError(
                f"{path}:{lineno}: unexpected token in db operand at {operand[i:]!r}"
            )
    if not segs:
        raise ParseError(f"{path}:{lineno}: empty db operand")
    return segs


def _decode_db_line(operand: str, table: dict[str, int], path: Path,
                    lineno: int) -> tuple[list[int], list[tuple[str, object]]]:
    """Decode one db line to its tile bytes, returning (bytes, segments). String segments encode
    through the charmap; byte segments pass through verbatim."""
    segs = _tokenize_db(operand, path, lineno)
    out: list[int] = []
    for kind, val in segs:
        if kind == "str":
            out.extend(encode_text(val, table, path, lineno))  # type: ignore[arg-type]
        else:
            out.append(val)  # type: ignore[arg-type]
    return out, segs


def _row_comment(segs: list[tuple[str, object]]) -> str | None:
    """An ASCII-safe rendering of a db line that carried text, mirroring the source: string runs as
    "..." (non-ASCII shown as <U+XXXX>), byte runs as $HH. None if the line was pure bytes."""
    if not any(kind == "str" for kind, _ in segs):
        return None
    parts = []
    for kind, val in segs:
        if kind == "str":
            parts.append('"' + parse_charmap.readable_comment(val) + '"')  # type: ignore[arg-type]
        else:
            parts.append(f"${val:02X}")  # type: ignore[str-format]
    return " ".join(parts)


# --- label extraction ---------------------------------------------------------------------------

def _find_top_label(lines: list[str], label: str, path: Path) -> int:
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label}:: defined more than once (lines {found})")
    return hits[0]


def _db_lines_from(lines: list[str], start_idx: int) -> list[tuple[int, str]]:
    """The consecutive db lines beginning after start_idx: (1-based line number, operand text).
    Leading blank/comment lines are skipped; collection stops at the first non-db meaningful line
    (the next label or directive) or a blank line once data has begun."""
    out: list[tuple[int, str]] = []
    for offset, raw in enumerate(lines[start_idx + 1:], start=start_idx + 2):
        stripped = _decomment(raw)
        if not stripped:
            if out:
                break
            continue
        if stripped == "db" or stripped.startswith("db ") or stripped.startswith("db\t"):
            out.append((offset, stripped[2:].strip()))
        else:
            break
    return out


def _db_lines_for_top_label(lines: list[str], label: str, path: Path) -> list[tuple[int, str]]:
    return _db_lines_from(lines, _find_top_label(lines, label, path))


def _db_lines_for_local_label(lines: list[str], parent: str, local: str,
                              path: Path) -> list[tuple[int, str]]:
    """The db lines of a local label (`.name`) inside a parent routine. Searching stops at the next
    top-level label so a same-named local under another routine cannot be matched."""
    parent_idx = _find_top_label(lines, parent, path)
    for offset in range(parent_idx + 1, len(lines)):
        stripped = _decomment(lines[offset])
        if _TOP_LABEL_RE.match(stripped):
            break
        if stripped == local or stripped == f"{local}:":
            return _db_lines_from(lines, offset)
    raise ParseError(f"{path}: local label {local} not found inside {parent}")


# --- consumer anchors (transcription cross-checks) ----------------------------------------------

def _top_label_instrs(lines: list[str], label: str, path: Path) -> list[tuple[int, str]]:
    """Meaningful instruction lines of a routine, from just after its label to the next top-level
    label (or EOF): (1-based line number, directive with any trailing comment stripped)."""
    idx = _find_top_label(lines, label, path)
    out: list[tuple[int, str]] = []
    for offset, raw in enumerate(lines[idx + 1:], start=idx + 2):
        stripped = _decomment(raw)
        if _TOP_LABEL_RE.match(stripped):
            break
        if stripped:
            out.append((offset, stripped))
    return out


def _ld_b_value(body: str) -> int | None:
    m = _LD_B_RE.match(body)
    if not m:
        return None
    return int(m.group(1), 16) if m.group(1) is not None else int(m.group(2))


def _find_indices(lines: list[str], predicate) -> list[int]:
    return [i for i, raw in enumerate(lines) if predicate(_decomment(raw))]


def _call_site_ld_b(lines: list[str], de_label: str, call_target: str, path: Path,
                    window: int = 10) -> int:
    """The `ld b, N` of the call site `ld de, <de_label>` ... `call <call_target>`. Scans forward a
    bounded window from the de-load; asserts both the ld b and the matching call are present."""
    de_hits = _find_indices(lines, lambda s: (m := _LD_DE_LABEL_RE.match(s)) and m.group(1) == de_label)
    if len(de_hits) != 1:
        raise ParseError(
            f"{path}: expected exactly one `ld de, {de_label}` call site, found {len(de_hits)}"
        )
    start = de_hits[0]
    ld_b = None
    saw_call = False
    for raw in lines[start + 1:start + 1 + window]:
        body = _decomment(raw)
        if not body:
            continue
        if ld_b is None:
            ld_b = _ld_b_value(body)
        cm = _CALL_RE.match(body)
        if cm and cm.group(1) == call_target:
            saw_call = True
            break
    if ld_b is None or not saw_call:
        raise ParseError(
            f"{path}:{start + 1}: call site `ld de, {de_label}` must reach "
            f"`ld b, <n>` + `call {call_target}` within {window} lines"
        )
    return ld_b


def _bottom_banner_rows(lines: list[str], path: Path) -> int:
    """MultiplayerVictoryBottomTilemap loads with no `ld de` of its own - de is left pointing at it
    when the Top banner's copy finishes (the two labels are contiguous). Its row count is the
    `ld b, N` before the second `call LoadTilemap.columnLoop` after the Top de-load."""
    de_hits = _find_indices(
        lines, lambda s: (m := _LD_DE_LABEL_RE.match(s)) and m.group(1) == "MultiplayerVictoryTopTilemap")
    if len(de_hits) != 1:
        raise ParseError(f"{path}: expected one `ld de, MultiplayerVictoryTopTilemap` site")
    start = de_hits[0]
    calls_seen = 0
    ld_b = None
    for raw in lines[start + 1:start + 1 + 16]:
        body = _decomment(raw)
        if not body:
            continue
        val = _ld_b_value(body)
        if val is not None:
            ld_b = val
        cm = _CALL_RE.match(body)
        if cm and cm.group(1) == COLUMN_LOOP:
            calls_seen += 1
            if calls_seen == 2:
                if ld_b is None:
                    raise ParseError(f"{path}: no `ld b, <n>` before the bottom banner load")
                return ld_b
    raise ParseError(f"{path}: could not locate the bottom banner's `call {COLUMN_LOOP}`")


def _derive_window_cols(lines: list[str], path: Path) -> int:
    """The window-block width from `Call_1F7D`'s `ld b, $08`."""
    for _lineno, body in _top_label_instrs(lines, WINDOW_LOADER, path):
        val = _ld_b_value(body)
        if val is not None:
            return val
    raise ParseError(f"{path}: {WINDOW_LOADER} has no `ld b, <n>` width load")


def _assert_load_tilemap(lines: list[str], path: Path) -> None:
    bodies = [body for _l, body in _top_label_instrs(lines, LOAD_TILEMAP, path)]
    if not any(re.match(r"^ld\s+b,\s*SCRN_Y_B$", b) for b in bodies):
        raise ParseError(f"{path}: {LOAD_TILEMAP} must load `ld b, SCRN_Y_B`")
    if not any(re.match(r"^ld\s+c,\s*SCRN_X_B$", b) for b in bodies):
        raise ParseError(f"{path}: {LOAD_TILEMAP} must load `ld c, SCRN_X_B`")


def _assert_load_playing_field(lines: list[str], path: Path) -> None:
    bodies = [body for _l, body in _top_label_instrs(lines, LOAD_PLAYING_FIELD, path)]
    if not any(_ld_b_value(b) == PLAYING_FIELD_COLS for b in bodies):
        raise ParseError(f"{path}: {LOAD_PLAYING_FIELD} must load `ld b, {PLAYING_FIELD_COLS}`")
    if not any(re.match(rf"^cp\s+a,\s*\${FIELD_SENTINEL:02X}$", b, re.IGNORECASE) for b in bodies):
        raise ParseError(f"{path}: {LOAD_PLAYING_FIELD} must test `cp a, ${FIELD_SENTINEL:02X}`")


# --- parse + assert -----------------------------------------------------------------------------

def parse_tilemaps(asm_text: str, table: dict[str, int], path: Path) -> dict:
    """Parse every tilemap label, assert its class shape and the loader anchors, and return the
    composed result: {"labels": [info, ...], "window_cols": int, "tower_rows": int}."""
    lines = asm_text.splitlines()

    # Loader anchors + derived geometry (validated before the labels that rely on them).
    _assert_load_tilemap(lines, path)
    _assert_load_playing_field(lines, path)
    window_cols = _derive_window_cols(lines, path)
    if window_cols != EXPECTED_WINDOW_COLS:
        raise ParseError(
            f"{path}: window width from {WINDOW_LOADER} is {window_cols}, expected {EXPECTED_WINDOW_COLS}"
        )

    tower_rows = _derive_tower_rows(lines, path)
    if tower_rows != EXPECTED_TOWER_ROWS:
        raise ParseError(f"{path}: tower height is {tower_rows}, expected {EXPECTED_TOWER_ROWS}")

    # Banner call-site row counts must equal each banner's parsed row count (checked per label below).
    banner_rows = {
        "MultiplayerVictoryTopTilemap": _call_site_ld_b(lines, "MultiplayerVictoryTopTilemap", COLUMN_LOOP, path),
        "MultiplayerVictoryBottomTilemap": _bottom_banner_rows(lines, path),
        "BuranBackdropTilemap": _call_site_ld_b(lines, "BuranBackdropTilemap", COLUMN_LOOP, path),
    }

    infos = []
    for upstream, port_name, cls in LABELS:
        if cls == "C6":
            db_lines = _db_lines_for_local_label(lines, CONGRATS_PARENT, CONGRATS_LOCAL, path)
        else:
            db_lines = _db_lines_for_top_label(lines, upstream, path)
        info = _build_label(upstream, port_name, cls, db_lines, table, window_cols, tower_rows,
                            banner_rows, path)
        infos.append(info)

    total = sum(len(info["flat"]) for info in infos)
    if total != CORPUS_TOTAL:
        raise ParseError(f"{path}: corpus total is {total} bytes, expected {CORPUS_TOTAL}")

    return {"labels": infos, "window_cols": window_cols, "tower_rows": tower_rows}


def _derive_tower_rows(lines: list[str], path: Path) -> int:
    """The tower strip height from the four `ld de, <TowerLabel>` / `ld b, 7` / `call
    LoadTilemap9C00Row` call sites; all four must agree."""
    tower_labels = [name for name, _p, cls in LABELS if cls == "C5"]
    values = {_call_site_ld_b(lines, name, LOAD_TOWER_ROW, path) for name in tower_labels}
    if len(values) != 1:
        raise ParseError(f"{path}: tower call sites disagree on `ld b`: {sorted(values)}")
    return values.pop()


def _build_label(upstream: str, port_name: str, cls: str, db_lines: list[tuple[int, str]],
                 table: dict[str, int], window_cols: int, tower_rows: int,
                 banner_rows: dict[str, int], path: Path) -> dict:
    """Decode one label's db lines and assert its class shape. Returns an info dict with the composed
    grid (grid_rows or flat_1d), the flat serialization, and per-row comments."""
    decoded = [(lineno, *_decode_db_line(op, table, path, lineno)) for lineno, op in db_lines]

    def rows_of(width: int, expected_count: int, sentinel: bool = False):
        data = decoded[:-1] if sentinel else decoded
        if sentinel:
            slineno, sbytes, _segs = decoded[-1]
            if sbytes != [FIELD_SENTINEL]:
                raise ParseError(
                    f"{path}:{slineno}: {upstream} must end with a lone `db ${FIELD_SENTINEL:02X}` "
                    f"sentinel, found {['0x%02X' % b for b in sbytes]}"
                )
        if len(data) != expected_count:
            raise ParseError(
                f"{path}: {upstream} has {len(data)} data rows, expected {expected_count}"
            )
        for lineno, row, _segs in data:
            if len(row) != width:
                raise ParseError(
                    f"{path}:{lineno}: {upstream} row is {len(row)} bytes wide, expected {width}"
                )
        return data

    info: dict = {"upstream": upstream, "name": port_name, "cls": cls}

    if cls in ("C1", "C2", "C4"):
        if cls == "C1":
            width, count = EXPECTED_SCREEN_COLS, EXPECTED_SCREEN_ROWS
        elif cls == "C2":
            width, count = EXPECTED_SCREEN_COLS, banner_rows[upstream]
        else:
            width, count = window_cols, len(decoded)
        data = rows_of(width, count)
        info["grid_rows"] = [row for _l, row, _s in data]
        info["row_comments"] = [_row_comment(segs) for _l, _r, segs in data]
        info["flat"] = [b for row in info["grid_rows"] for b in row]
        info["chunks"] = list(zip(info["grid_rows"], info["row_comments"]))
        info["type"] = _grid_type(cls, count)

    elif cls == "C3":
        data = rows_of(PLAYING_FIELD_COLS, PLAYING_FIELD_ROWS, sentinel=True)
        info["grid_rows"] = [row for _l, row, _s in data]
        info["row_comments"] = [_row_comment(segs) for _l, _r, segs in data]
        grid_flat = [b for row in info["grid_rows"] for b in row]
        info["flat"] = grid_flat + [FIELD_SENTINEL]
        info["chunks"] = list(zip(info["grid_rows"], info["row_comments"])) + \
            [([FIELD_SENTINEL], "$FF sentinel (serialization terminator; not part of the grid)")]
        info["type"] = _grid_type(cls, PLAYING_FIELD_ROWS)

    elif cls == "C5":
        if len(decoded) != 1:
            raise ParseError(f"{path}: {upstream} must be a single db line, found {len(decoded)}")
        lineno, row, _segs = decoded[0]
        if len(row) != tower_rows:
            raise ParseError(
                f"{path}:{lineno}: {upstream} is {len(row)} bytes, expected {tower_rows}"
            )
        info["flat_1d"] = row
        info["flat"] = row
        info["chunks"] = [(row, "vertical column, top to bottom")]
        info["type"] = "std::array<std::uint8_t, kTowerTilemapRows>"

    elif cls == "C6":
        if len(decoded) != 1:
            raise ParseError(f"{path}: {upstream} must be a single db line, found {len(decoded)}")
        lineno, row, segs = decoded[0]
        info["flat_1d"] = row
        info["flat"] = row
        info["chunks"] = [(row, _row_comment(segs))]
        info["type"] = f"std::array<std::uint8_t, {len(row)}>"

    else:  # unreachable
        raise ParseError(f"{path}: {upstream} has unknown class {cls!r}")

    return info


def _grid_type(cls: str, rows: int) -> str:
    if cls == "C1":
        return "std::array<std::array<std::uint8_t, kTilemapScreenCols>, kTilemapScreenRows>"
    if cls == "C2":
        return f"std::array<std::array<std::uint8_t, kTilemapScreenCols>, {rows}>"
    if cls == "C3":
        return "std::array<std::array<std::uint8_t, kPlayingFieldCols>, kPlayingFieldRows>"
    if cls == "C4":
        return f"std::array<std::array<std::uint8_t, kTilemapWindowCols>, {rows}>"
    raise ValueError(cls)


# --- emit ---------------------------------------------------------------------------------------

def _fixture_name(port_name: str) -> str:
    return f"kExpected{port_name[1:]}Bytes"


def _byte_row(values: list[int]) -> str:
    return ", ".join(f"0x{b:02X}" for b in values)


def _emit_grid(info: dict) -> str:
    """A composed 2-D grid: double-braced rows, each with its source-string comment when it had one."""
    lines = [f"inline constexpr {info['type']}", f"    {info['name']} = {{{{"]
    for row, comment in zip(info["grid_rows"], info["row_comments"]):
        suffix = f"  // {comment}" if comment else ""
        lines.append(f"    {{{{ {_byte_row(row)} }}}},{suffix}")
    lines.append("}};")
    return "\n".join(lines)


def _emit_1d(info: dict) -> str:
    comment = info["chunks"][0][1]
    body = f"    {_byte_row(info['flat_1d'])},"
    if comment:
        body += f"  // {comment}"
    # The closing `}};` is a plain string element: inside an f-string `}}` would collapse to one `}`.
    return "\n".join([f"inline constexpr {info['type']} {info['name']} = {{{{", body, "}};"])


def emit_inc(result: dict, source_commit: str) -> str:
    window_cols = result["window_cols"]
    tower_rows = result["tower_rows"]
    parts = [
        common.banner("parse_tilemaps.py", source_commit).rstrip("\n"),
        "// Included at namespace scope in src/data/tilemaps.h (inside `namespace kirpich`), which",
        "// includes <array>, <cstdint>, and playing_field.h (for kPlayingFieldCols/Rows) before this.",
        "// Cells are raw tile indices; each screen is a row-major grid (the field overlays' $FF",
        "// terminator and the towers' write stride are serialization, not stored here).",
        "",
        "// Screen geometry, read from the loaders in the disassembly.",
        f"inline constexpr std::uint8_t kTilemapScreenCols = {EXPECTED_SCREEN_COLS};",
        f"inline constexpr std::uint8_t kTilemapScreenRows = {EXPECTED_SCREEN_ROWS};",
        f"inline constexpr std::uint8_t kTilemapWindowCols = {window_cols};",
        f"inline constexpr std::uint8_t kTowerTilemapRows = {tower_rows};",
        "",
    ]
    for info in result["labels"]:
        if info["cls"] in ("C5", "C6"):
            parts.append(_emit_1d(info))
        else:
            parts.append(_emit_grid(info))
        parts.append("")
    return "\n".join(parts).rstrip("\n") + "\n"


def _emit_fixture_array(info: dict) -> str:
    name = _fixture_name(info["name"])
    lines = [
        f"inline constexpr std::array<std::uint8_t, {len(info['flat'])}> {name} = {{{{"
    ]
    for chunk, comment in info["chunks"]:
        suffix = f"  // {comment}" if comment else ""
        lines.append(f"    {_byte_row(chunk)},{suffix}")
    lines.append("}};")
    return "\n".join(lines)


def emit_fixture(result: dict, source_commit: str) -> str:
    arrays = "\n\n".join(_emit_fixture_array(info) for info in result["labels"])
    return f"""#pragma once
{common.banner("parse_tilemaps.py", source_commit)}\
// Independent fixture for the full-corpus tilemap sweep: each screen's bytes in serialization order
// and form ($FF field-overlay sentinels kept), flat and independent of the composed grids in
// src/data/tilemaps.h, so a defect in the header cannot mask the sweep. tests/test_tilemaps.cpp
// asserts each composed grid equals these bytes cell for cell.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

{arrays}

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(
            f"{label}: emitted output contains a non-ASCII byte "
            f"(U+{ord(bad):04X}) - the emitter must escape or render every non-ASCII byte"
        )


# --- driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's tilemap grids + fixture.")
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
        print(f"parse_tilemaps: source file not found: {asm_path}", file=sys.stderr)
        return 2
    if not (source_root / "charmap.asm").is_file():
        print(f"parse_tilemaps: source file not found: {source_root / 'charmap.asm'}", file=sys.stderr)
        return 2

    table = build_charmap_table(source_root)
    asm_text = asm_path.read_bytes().decode("utf-8")
    result = parse_tilemaps(asm_text, table, asm_path)
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
        print(f"parse_tilemaps: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_tilemaps: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
