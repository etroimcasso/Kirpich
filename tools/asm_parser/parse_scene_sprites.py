#!/usr/bin/env python3
"""Parser for Kirpich's sprite scene lists - the OAM object-placement tables.

The renderer draws a scripted scene (a victory screen, the ending dance, a launch sequence, a menu)
from a small table of sprite objects. Two shapes exist, both in tetris.asm:

  Shape A - count-prefixed 6-byte objects (LoadSprites, tetris.asm:3611): the caller passes a count
            and the routine copies that many 6-byte objects into the $C200 object buffer. Eleven
            tables: the config/difficulty/height menu markers and the victory/defeat/dance/launch
            scenes.
  Shape B - single $FF-terminated 7-byte objects (CopyUntilFF, tetris.asm:6267): the active- and
            preview-piece templates copied to $C200/$C210. Seven data bytes then one $FF; byte 3 is
            a $00 placeholder the falling-piece logic overwrites at run time.

The 6-byte object the renderer reads (_RenderSprites, tetris.asm:6693-6850):

  byte 0  enable/visibility  - $00 draw, $80 starts hidden (renderer forces Y=$FF)
  byte 1  OAM Y base coordinate
  byte 2  OAM X base coordinate
  byte 3  sprite index into SpriteList  - a SpriteId (the composed-sprite identity space, 1.I)
  byte 4  OAM attribute, OR-merged  - only bit 7 (BG-over-OBJ priority) varies in the corpus
  byte 5  OAM attribute + flip control  - only bit 5 (X-flip) varies in the corpus
  byte 6  base OAM flags  - shape B only; $00 in both templates

Every corpus-invariant bit is asserted here; the port struct (src/data/scene_sprites.h) carries only
the bits that vary (hidden, behindBg, xflip). The byte-3 name comes from parse_sprites' SpriteId name
table - the single source of the enumerator strings, imported rather than re-typed.

Emission set = the data `.inc` + the test fixture (no enum - the byte-3 space is 1.I's SpriteId, and
the scenes are call-site-selected labels, not a runtime symbol space):

  src/data/generated/scene_sprites_data.inc   the 13 kXxx arrays / single objects (SceneSprite rows),
                                              included at namespace scope inside scene_sprites.h
  tests/fixtures/scene_sprites_expected.h     the raw ROM bytes as plain integers, so a defect in the
                                              typed surface cannot mask the sweep in test_scene_sprites.cpp

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
from parse_sprites import SPRITE_COUNT, SPRITE_NAMES

# --- Table registry (upstream label, port accessor/array name, record count) --------------------
#
# Order here is the fixture and accessor order; the C++ test walks the same order. Shape A holds
# count-prefixed 6-byte objects; shape B holds one $FF-terminated 7-byte object each.
SHAPE_A_TABLES: list[tuple[str, str, int]] = [
    ("Data_26CF", "configScreenSprites", 2),
    ("Data_26DB", "typeADifficultySprites", 1),
    ("Data_26E1", "typeBDifficultySprites", 2),
    ("Data_26ED", "twoPlayerHeightSprites", 2),
    ("MarioVictorySprites", "marioVictorySprites", 3),
    ("LuigiVictorySprites", "luigiVictorySprites", 3),
    ("MarioDefeatSprites", "marioDefeatSprites", 2),
    ("LuigiDefeatSprites", "luigiDefeatSprites", 2),
    ("DancerSprites", "dancerSprites", 10),
    ("BuranLaunchSprites", "buranLaunchSprites", 3),
    ("RocketLaunchSprites", "rocketLaunchSprites", 3),
]
SHAPE_B_TABLES: list[tuple[str, str]] = [
    ("ActivePieceSprite", "activePieceSprite"),
    ("PreviewPieceSprite", "previewPieceSprite"),
]

SHAPE_A_RECORD_BYTES = 6
SHAPE_B_RECORD_BYTES = 7          # data bytes; a $FF terminator follows
TABLE_COUNT = len(SHAPE_A_TABLES) + len(SHAPE_B_TABLES)   # 13
RECORD_COUNT = sum(n for _l, _a, n in SHAPE_A_TABLES) + len(SHAPE_B_TABLES)  # 35

# Byte-field masks / values (the corpus-invariant bits, asserted).
VIS_DRAW = 0x00
VIS_HIDDEN = 0x80
ATTR_PRIORITY = 0x80             # byte 4 bit 7: BG-over-OBJ priority
ATTR_XFLIP = 0x20                # byte 5 bit 5: OAM X-flip
TERMINATOR = 0xFF                # shape B list terminator
PLACEHOLDER_SPRITE = 0x00        # shape B byte 3 (runtime-patched)

_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_.]*)::')
_DB_RE = re.compile(r'^db\b(?P<operand>.*)$')
_HEX_BYTE_RE = re.compile(r'^\$([0-9A-Fa-f]{2})$')


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_scene_sprites"


# --- Parsed structures --------------------------------------------------------------------------

@dataclass(frozen=True)
class SceneSprite:
    """One placed sprite object, decoded from its raw bytes."""

    hidden: bool
    y: int
    x: int
    sprite_index: int
    behind_bg: bool
    xflip: bool

    @property
    def sprite_name(self) -> str:
        return SPRITE_NAMES[self.sprite_index]


@dataclass
class SceneTable:
    """One scene table: its accessor name, decoded objects, and raw bytes (terminator included)."""

    label: str
    accessor: str
    shape: str            # "A" | "B"
    objects: list[SceneSprite]
    raw_bytes: list[int]


# --- Operand + block helpers --------------------------------------------------------------------

def _split_operands(operand: str) -> list[str]:
    """Comma-separate a db operand, dropping any trailing comment and empty tokens."""
    operand = operand.split(";", 1)[0]
    return [tok.strip() for tok in operand.split(",") if tok.strip()]


def _parse_byte(token: str, path: Path, lineno: int) -> int:
    match = _HEX_BYTE_RE.match(token)
    if not match:
        raise ParseError(f"{path}:{lineno}: not a two-digit hex byte: {token!r}")
    return int(match.group(1), 16)


def _collect_block(lines: list[str], label: str, path: Path) -> list[int]:
    """The contiguous run of `db` bytes under `label::`, up to the next label or non-db line."""
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
            break                       # next label ends the block
        db_match = _DB_RE.match(line)
        if not db_match:
            break                       # first non-db meaningful line ends the block
        tokens = _split_operands(db_match.group("operand"))
        if not tokens:
            raise ParseError(f"{path}:{offset}: empty db operand under {label}::")
        for tok in tokens:
            out.append(_parse_byte(tok, path, offset))
        started = True
    if not started:
        raise ParseError(f"{path}: {label}:: has no db bytes")
    return out


# --- Object decode (the byte map + the invariant-bit asserts) -----------------------------------

def _decode_object(obj: list[int], ctx: str, path: Path) -> SceneSprite:
    b0, b1, b2, b3, b4, b5 = obj
    if b0 not in (VIS_DRAW, VIS_HIDDEN):
        raise ParseError(f"{path}: {ctx}: byte 0 (enable) = ${b0:02X}, expected $00 or $80")
    if b3 >= SPRITE_COUNT:
        raise ParseError(
            f"{path}: {ctx}: byte 3 (sprite) = ${b3:02X} is outside the SpriteId space $00-$5D")
    if b4 & ~ATTR_PRIORITY:
        raise ParseError(
            f"{path}: {ctx}: byte 4 = ${b4:02X} sets a bit outside the priority bit ($80)")
    if b5 & ~ATTR_XFLIP:
        raise ParseError(
            f"{path}: {ctx}: byte 5 = ${b5:02X} sets a bit outside the x-flip bit ($20)")
    return SceneSprite(
        hidden=(b0 == VIS_HIDDEN), y=b1, x=b2, sprite_index=b3,
        behind_bg=bool(b4 & ATTR_PRIORITY), xflip=bool(b5 & ATTR_XFLIP))


def _parse_shape_a(lines: list[str], label: str, accessor: str, count: int,
                   path: Path) -> SceneTable:
    raw = _collect_block(lines, label, path)
    if len(raw) != count * SHAPE_A_RECORD_BYTES:
        raise ParseError(
            f"{path}: {label} holds {len(raw)} bytes, expected {count} x {SHAPE_A_RECORD_BYTES} "
            f"= {count * SHAPE_A_RECORD_BYTES}")
    objects = [
        _decode_object(raw[i:i + SHAPE_A_RECORD_BYTES], f"{label}[{i // SHAPE_A_RECORD_BYTES}]", path)
        for i in range(0, len(raw), SHAPE_A_RECORD_BYTES)
    ]
    return SceneTable(label, accessor, "A", objects, raw)


def _parse_shape_b(lines: list[str], label: str, accessor: str, path: Path) -> SceneTable:
    raw = _collect_block(lines, label, path)
    if len(raw) != SHAPE_B_RECORD_BYTES + 1:
        raise ParseError(
            f"{path}: {label} holds {len(raw)} bytes, expected {SHAPE_B_RECORD_BYTES} data + one "
            f"${TERMINATOR:02X} terminator")
    if raw[-1] != TERMINATOR:
        raise ParseError(f"{path}: {label} must end in one ${TERMINATOR:02X}, found ${raw[-1]:02X}")
    if TERMINATOR in raw[:-1]:
        raise ParseError(f"{path}: {label} has a ${TERMINATOR:02X} before the end")
    if raw[6] != 0x00:
        raise ParseError(f"{path}: {label} byte 6 (base OAM flags) = ${raw[6]:02X}, expected $00")
    if raw[3] != PLACEHOLDER_SPRITE:
        raise ParseError(
            f"{path}: {label} byte 3 = ${raw[3]:02X}, expected the runtime placeholder $00")
    obj = _decode_object(raw[:6], label, path)
    return SceneTable(label, accessor, "B", [obj], raw)


# --- Parse + assemble ---------------------------------------------------------------------------

def parse_scene_sprites(tetris_text: str, tetris_path: Path) -> list[SceneTable]:
    if len(SPRITE_NAMES) != SPRITE_COUNT:
        raise ParseError(
            f"imported SpriteId name table holds {len(SPRITE_NAMES)} names, expected {SPRITE_COUNT}")
    lines = tetris_text.splitlines()
    tables: list[SceneTable] = []
    for label, accessor, count in SHAPE_A_TABLES:
        tables.append(_parse_shape_a(lines, label, accessor, count, tetris_path))
    for label, accessor in SHAPE_B_TABLES:
        tables.append(_parse_shape_b(lines, label, accessor, tetris_path))

    # Corpus totals (the moment-of-truth counts).
    if len(tables) != TABLE_COUNT:
        raise ParseError(f"{tetris_path}: parsed {len(tables)} tables, expected {TABLE_COUNT}")
    total_records = sum(len(t.objects) for t in tables)
    if total_records != RECORD_COUNT:
        raise ParseError(
            f"{tetris_path}: parsed {total_records} objects, expected {RECORD_COUNT}")
    return tables


# --- Emit: scene_sprites_data.inc ---------------------------------------------------------------

def _emit_row(obj: SceneSprite) -> str:
    return ("{ "
            f".hidden = {'true' if obj.hidden else 'false'}, "
            f".y = 0x{obj.y:02X}, .x = 0x{obj.x:02X}, "
            f".sprite = SpriteId::{obj.sprite_name}, "
            f".behindBg = {'true' if obj.behind_bg else 'false'}, "
            f".xflip = {'true' if obj.xflip else 'false'}"
            " }")


def _cap(accessor: str) -> str:
    """kConfigScreenSprites from configScreenSprites."""
    return "k" + accessor[0].upper() + accessor[1:]


def emit_inc(tables: list[SceneTable], source_commit: str) -> str:
    blocks: list[str] = []
    for table in tables:
        name = _cap(table.accessor)
        if table.shape == "A":
            rows = "\n".join(f"    {_emit_row(o)}," for o in table.objects)
            blocks.append(
                f"// {table.label}\n"
                f"inline constexpr std::array<SceneSprite, {len(table.objects)}> {name}{{{{\n"
                f"{rows}\n}}}};")
        else:  # single object (byte 3 is the runtime placeholder SpriteId::L_0)
            blocks.append(
                f"// {table.label} (byte 3 is a runtime placeholder; stored value is L_0)\n"
                f"inline constexpr SceneSprite {name}{_emit_row(table.objects[0])};")
    body = "\n\n".join(blocks)
    return f"""{common.banner("parse_scene_sprites.py", source_commit)}\
// Included at namespace scope inside src/data/scene_sprites.h (inside `namespace kirpich`), which
// defines SceneSprite first. Each row is one placed sprite object: its visibility, OAM base
// coordinates, the SpriteId it draws, and the two attribute bits (BG priority, x-flip) the corpus
// uses. Shape-A tables are count-prefixed lists; the two shape-B objects are the piece templates.
{body}
"""


# --- Emit: scene_sprites_expected.h (raw serialization fixture) ---------------------------------

def _byte_row(values: list[int]) -> str:
    return ", ".join(f"0x{b:02X}" for b in values)


def emit_fixture(tables: list[SceneTable], source_commit: str) -> str:
    flat: list[int] = []
    table_rows: list[str] = []
    for table in tables:
        bpr = SHAPE_A_RECORD_BYTES if table.shape == "A" else SHAPE_B_RECORD_BYTES + 1
        table_rows.append(
            f"    {{ .byte_offset = {len(flat)}, .record_count = {len(table.objects)}, "
            f".bytes_per_record = {bpr}, "
            f".terminated = {'true' if table.shape == 'B' else 'false'} }},  // {table.label}")
        flat.extend(table.raw_bytes)
    table_body = "\n".join(table_rows)
    flat_lines = "\n".join(
        "    " + _byte_row(flat[i:i + 16]) + "," for i in range(0, len(flat), 16))
    return f"""#pragma once
{common.banner("parse_scene_sprites.py", source_commit)}\
// Independent fixture for the full-corpus scene-sprite sweep: the ROM's raw object bytes as plain
// integers. Each row of kExpectedSceneTables slices kExpectedSceneSpriteBytes for one table (shape-A
// records are 6 bytes; the two shape-B templates are 7 data bytes + a $FF terminator). It holds no
// port type, so a defect in src/data/scene_sprites.h cannot mask the sweep. test_scene_sprites.cpp
// re-derives each SceneSprite from these bytes and compares to the generated arrays.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// One table's slice into kExpectedSceneSpriteBytes.
struct SceneTableRow {{
    std::uint16_t byte_offset;
    std::uint8_t  record_count;
    std::uint8_t  bytes_per_record;   // 6 (shape A) or 8 (shape B: 7 data + terminator)
    bool          terminated;         // true => the last byte per record is a $FF terminator
}};

// The {TABLE_COUNT} tables, in accessor order (matches src/data/scene_sprites.h).
inline constexpr std::array<SceneTableRow, {TABLE_COUNT}> kExpectedSceneTables{{{{
{table_body}
}}}};

// Every table's raw bytes concatenated in the order above ({len(flat)} bytes total).
inline constexpr std::array<std::uint8_t, {len(flat)}> kExpectedSceneSpriteBytes{{{{
{flat_lines}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's sprite scene lists + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    tetris_path: Path = args.source_root / "tetris.asm"
    if not tetris_path.is_file():
        print(f"parse_scene_sprites: source file not found: {tetris_path}", file=sys.stderr)
        return 2

    tetris_text = tetris_path.read_bytes().decode("utf-8")
    tables = parse_scene_sprites(tetris_text, tetris_path)
    commit = common.source_commit_of(args.source_root)

    print(f"parse_scene_sprites: {len(tables)} tables / {sum(len(t.objects) for t in tables)} "
          f"objects; invariant-bit asserts passed.")

    outputs = {
        args.inc_out: emit_inc(tables, commit),
        args.fixture_out: emit_fixture(tables, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        _assert_ascii(content, str(out_path))
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_scene_sprites: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_scene_sprites: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
