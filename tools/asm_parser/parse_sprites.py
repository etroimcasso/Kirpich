#!/usr/bin/env python3
"""Parser for Kirpich's composed OAM sprites - every multi-tile sprite the game draws.

The disassembly builds each on-screen sprite from three levels that the renderer walks together:

  SpriteList (tetris.asm)     94 word entries, the identity space the game passes in a descriptor
                              and the score screen passes as a rocket tier. Four entries duplicate
                              an earlier target, so 94 identities map onto 90 distinct records.
  Sprite_* records (sprites.asm)   90 rows of {dw SpriteTiles pointer, db y-offset, db x-offset};
                              the offsets are signed, applied by the renderer at draw time.
  SpriteTiles_* tile lists (sprites.asm)   90 rows of {dw Matrix pointer, tile stream}; mapping to
                              records is exactly 1:1 (each list referenced once, none shared).
  Matrix_* grids (tetris.asm)   5 shared (y, x) pixel-offset grids; the renderer consumes one pair
                              per drawn tile to place each OAM entry.

The tile stream is a little escape machine (renderer at tetris.asm:6750-6773): $FF terminates the
list; $FE consumes one grid pair and emits nothing (a gap); $FD toggles the OAM x-flip bit for the
one real tile byte that follows it and consumes no grid pair of its own. Every other byte is a real
tile that consumes one grid pair. No real tile byte in the corpus is >= $FD, so the encoding is
unambiguous. This parser resolves record + tile list + grid into one composed record per identity -
a list of (y, x, xflip, tile) parts plus the record's two offsets - duplicates resolved by value so
every identity is self-contained.

Provenance: SpriteList has no constants file, so the 94 identity names are minted here (the port's
naming table, locked below). The name table is the single source of the SpriteId enumerator strings;
parse_scoring imports it to type the rocket-tier bytes.

Emission set = the enum header + the data `.inc` + the test fixture:

  include/kirpich/sprite_id.h            enum class SpriteId : std::uint8_t, 94 enumerators
  src/data/generated/sprites_data.inc    kSprites (std::array<Sprite, 94>), designated rows,
                                         included at namespace scope inside src/data/sprites.h
  tests/fixtures/sprites_expected.h       the raw serialization - SpriteList targets, record rows,
                                         the concatenated tile streams, and the grid pairs - as
                                         plain integers, so a defect in the composed surface cannot
                                         mask the sweep in tests/test_sprites.cpp

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Any deviation from
the expected structure is a hard error with a source citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import common

# --- Identity name table (port-design; SpriteList has no upstream constants file) ---------------
#
# One group per logical span of the sprite list, mirroring the disassembly's own SpriteList
# comments. Animation-frame pairs get _1/_2; the three uncommented alias entries that duplicate an
# earlier target get content-derived _ALT names. Flattened, this is the SpriteId enumerator order:
# index == value == the SpriteList index the game passes in a descriptor.
SPRITE_GROUPS: list[tuple[str, list[str]]] = [
    ("Falling-piece rotation sprites - four per tetromino, in PieceKind order (L, J, I, O, S, Z, T).",
     ["L_0", "L_1", "L_2", "L_3", "J_0", "J_1", "J_2", "J_3",
      "I_0", "I_1", "I_2", "I_3", "O_0", "O_1", "O_2", "O_3",
      "S_0", "S_1", "S_2", "S_3", "Z_0", "Z_1", "Z_2", "Z_3",
      "T_0", "T_1", "T_2", "T_3"]),
    ("Game-type labels and the music-off label.",
     ["A_TYPE", "B_TYPE", "C_TYPE", "OFF"]),
    ("Score digits 0-9.",
     ["DIGIT_0", "DIGIT_1", "DIGIT_2", "DIGIT_3", "DIGIT_4",
      "DIGIT_5", "DIGIT_6", "DIGIT_7", "DIGIT_8", "DIGIT_9"]),
    ("Victory-and-defeat characters, the Buran shuttle, and their smoke and exhaust.",
     ["JUMPING_LARGE_MARIO_1", "JUMPING_LARGE_MARIO_2", "BURAN", "JUMPING_LARGE_MARIO_2_ALT",
      "CRYING_LARGE_MARIO_1", "CRYING_LARGE_MARIO_2",
      "JUMPING_SMALL_MARIO_1", "JUMPING_SMALL_MARIO_2",
      "CRYING_SMALL_MARIO_1", "CRYING_SMALL_MARIO_2",
      "ROCKET_SMOKE_1", "ROCKET_SMOKE_2",
      "JUMPING_LARGE_LUIGI_1", "JUMPING_LARGE_LUIGI_2",
      "ALTERNATIVE_EXHAUST_1", "ALTERNATIVE_EXHAUST_2",
      "CRYING_LARGE_LUIGI_1", "CRYING_LARGE_LUIGI_2",
      "JUMPING_SMALL_LUIGI_1", "JUMPING_SMALL_LUIGI_2",
      "CRYING_SMALL_LUIGI_1", "CRYING_SMALL_LUIGI_2",
      "BURAN_EXHAUST_1", "BURAN_EXHAUST_2",
      "CRYING_SMALL_LUIGI_2_ALT", "VIOLINIST_1_ALT"]),
    ("Ending-dance musicians.",
     ["VIOLINIST_1", "VIOLINIST_2", "CELLIST_1", "CELLIST_2",
      "BIG_DRUM_1", "BIG_DRUM_2", "GUITARIST_1", "GUITARIST_2",
      "FLUTIST_PAIR_1", "FLUTIST_PAIR_2", "BAYAN_1", "BAYAN_2",
      "JUMPING_COSSACK_1", "JUMPING_COSSACK_2", "DANCER_1", "DANCER_2",
      "KOKOSHNIK_WOMAN_1", "KOKOSHNIK_WOMAN_2"]),
    ("Victory-crash smoke and the score-ranked rockets with exhaust.",
     ["VICTORY_CRASH_SMOKE_SMALL", "VICTORY_CRASH_SMOKE_LARGE",
      "ROCKET_L", "ROCKET_M", "ROCKET_S", "ROCKET_S_ALT",
      "ROCKET_EXHAUST_1", "ROCKET_EXHAUST_2"]),
]

# The flat name table: SPRITE_NAMES[index] is the SpriteId enumerator for that SpriteList index.
SPRITE_NAMES: list[str] = [name for _comment, names in SPRITE_GROUPS for name in names]

SPRITE_COUNT = 94           # SpriteList indices 0x00..0x5D
RECORD_COUNT = 90           # distinct Sprite_* / SpriteTiles_* records (four SpriteList dups)
GRID_COUNT = 5
MAX_PARTS = 28              # the Buran (SpriteTiles_30CB); capacity of the composed part vector

# The four SpriteList entries that re-target an earlier record, and the addresses they duplicate.
EXPECTED_DUP_INDICES = {0x2D: 0x2CCC, 0x42: 0x2D04, 0x43: 0x2D08, 0x5B: 0x3112}

# Corpus totals over the 90 distinct tile lists (the moment-of-truth counts).
CORPUS_STREAM_BYTES = 877
CORPUS_REAL_TILES = 462
CORPUS_FD = 52
CORPUS_FE = 273
CORPUS_FF = 90              # exactly one $FF terminator per list

# Escape bytes.
TILE_TERMINATOR = 0xFF     # ends the tile list
TILE_SKIP = 0xFE           # consumes one grid pair, emits nothing
TILE_XFLIP = 0xFD          # x-flips the next real tile; consumes no grid pair itself
FIRST_ESCAPE = 0xFD        # every real tile byte is strictly below this

# sprites.asm section: one contiguous SECTION holding every record and tile list.
SPRITE_SECTION_ADDR = 0x2C20
_SPRITE_SECTION_RE = re.compile(r'^SECTION\s+"Sprite data",\s*ROM0\[\$2C20\]$')

# SpriteList lives in tetris.asm; the Matrix grids follow it in their own section.
SPRITELIST_LABEL = "SpriteList"

# The five grids, in section order: (label, ROM address, pair count). Carried from the retired
# sprite-grids grammar; the last count is fixed (its terminal boundary GameplayTiles has no address
# suffix), the first four are cross-checked against the address deltas.
GRID_TABLES = [
    ("Matrix_31A9", 0x31A9, 16),
    ("Matrix_31C9", 0x31C9, 8),
    ("Matrix_31D9", 0x31D9, 14),
    ("Matrix_31F5", 0x31F5, 28),
    ("Matrix_322D", 0x322D, 9),
]
GRID_SECTION_TEXT = 'SECTION "TODO Name", ROM0[$31A9]'
GRID_TERMINAL_LABEL = "GameplayTiles"
GRID_LAST_TABLE_BYTES = 18   # $323F - $322D
GRID_BYTE_STEP = 8           # every offset is one 8x8 tile
GRID_BYTE_MAX = 0x38         # largest offset the grids use

_LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)::')
_DW_RE = re.compile(r'^dw\s+(?P<operand>.+)$')
_DB_RE = re.compile(r'^db\b(?P<operand>.*)$')
_GRID_SECTION_RE = re.compile(r'^SECTION "TODO Name", ROM0\[\$31A9\]$')
_HEX_BYTE_RE = re.compile(r'^\$([0-9A-Fa-f]{2})$')
_SIGNED_BYTE_RE = re.compile(r'^(-?)\$([0-9A-Fa-f]{1,2})$')
_WORD_RE = re.compile(r'^\$([0-9A-Fa-f]{1,4})$')
_SPRITE_LABEL_RE = re.compile(r'^Sprite_([0-9A-Fa-f]{4})$')
_TILES_LABEL_RE = re.compile(r'^SpriteTiles_([0-9A-Fa-f]{4})$')
_MATRIX_LABEL_RE = re.compile(r'^Matrix_([0-9A-Fa-f]{4})$')


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_sprites"


# --- Parsed structures --------------------------------------------------------------------------

@dataclass
class Record:
    """One Sprite_* row: the tile list it points at and its two signed render offsets."""

    label: str
    address: int
    tiles_label: str
    offset_y_byte: int   # raw two's-complement byte as stored
    offset_x_byte: int


@dataclass
class TileList:
    """One SpriteTiles_* row: the grid it walks and its raw tile stream (escapes + $FF included)."""

    label: str
    address: int
    grid_label: str
    stream: list[int]


@dataclass(frozen=True)
class Part:
    """One composed OAM part: grid (y, x), the $FD x-flip toggle, and the raw tile byte."""

    y: int
    x: int
    xflip: bool
    tile: int


@dataclass
class ComposedSprite:
    """One identity: its name/value, the record's signed offsets, and the composed part list."""

    index: int
    name: str
    target_address: int
    record: Record
    tile_list: TileList
    offset_y: int        # signed
    offset_x: int
    parts: list[Part]
    stream_offset: int = 0  # into the concatenated fixture stream, filled at fixture time


@dataclass
class SpritesData:
    """Everything the emitters consume."""

    sprites: list[ComposedSprite]              # 94, SpriteList index order
    records: list[Record]                      # 90, sprites.asm file order
    tile_lists: dict[str, TileList]            # by label
    grids: list[tuple[str, int, list[tuple[int, int]]]]  # (label, address, pairs), section order
    grid_by_label: dict[str, list[tuple[int, int]]] = field(default_factory=dict)


# --- Operand helpers ----------------------------------------------------------------------------

def _split_operands(operand: str) -> list[str]:
    """Comma-separate a db/dw operand, dropping the trailing empty token RGBDS allows (`db $x, `)."""
    operand = operand.split(";", 1)[0]
    tokens = [tok.strip() for tok in operand.split(",")]
    return [tok for tok in tokens if tok]


def _parse_stream_byte(token: str, path: Path, lineno: int) -> int:
    match = _HEX_BYTE_RE.match(token)
    if not match:
        raise ParseError(f"{path}:{lineno}: not a two-digit hex tile byte: {token!r}")
    return int(match.group(1), 16)


def _parse_signed_byte(token: str, path: Path, lineno: int) -> int:
    """A record offset byte: `$00` or `-$NN`, returned as its raw two's-complement byte (0..255)."""
    match = _SIGNED_BYTE_RE.match(token)
    if not match:
        raise ParseError(f"{path}:{lineno}: not a signed hex byte: {token!r}")
    value = int(match.group(2), 16)
    if match.group(1) == "-":
        value = (-value) & 0xFF
    return value & 0xFF


def _signed(byte: int) -> int:
    return byte - 256 if byte >= 128 else byte


# --- sprites.asm: records + tile lists (with the address walk) ----------------------------------

def _parse_sprite_section(text: str, path: Path) -> tuple[list[Record], list[TileList]]:
    """Walk the one contiguous sprite-data section, computing each label's address and asserting it
    equals the label's hex suffix. Records and tile lists may interleave; each is keyed by prefix."""
    lines = text.splitlines()
    section_idx = None
    for i, raw in enumerate(lines):
        if _SPRITE_SECTION_RE.match(raw.strip()):
            section_idx = i
            break
    if section_idx is None:
        raise ParseError(f'{path}: section anchor `SECTION "Sprite data", ROM0[$2C20]` not found')

    addr = SPRITE_SECTION_ADDR
    records: list[Record] = []
    tile_lists: list[TileList] = []

    # State for the label currently being filled.
    cur_label: str | None = None
    cur_kind: str | None = None       # "record" | "tiles"
    cur_addr = 0
    cur_dw: list[str] = []
    cur_db: list[int] = []

    def flush(lineno: int) -> None:
        nonlocal cur_label, cur_kind, cur_dw, cur_db
        if cur_label is None:
            return
        if cur_kind == "record":
            if len(cur_dw) != 1 or not _TILES_LABEL_RE.match(cur_dw[0]):
                raise ParseError(
                    f"{path}:{lineno}: {cur_label} must hold exactly one `dw SpriteTiles_*`, "
                    f"found {cur_dw}")
            if len(cur_db) != 2:
                raise ParseError(
                    f"{path}:{lineno}: {cur_label} must hold exactly two offset bytes, "
                    f"found {len(cur_db)}")
            oy, ox = _signed(cur_db[0]), _signed(cur_db[1])
            if not (-0x28 <= oy <= 0):
                raise ParseError(f"{path}: {cur_label} y-offset {oy} outside [-40, 0]")
            if not (-0x18 <= ox <= 0):
                raise ParseError(f"{path}: {cur_label} x-offset {ox} outside [-24, 0]")
            records.append(Record(cur_label, cur_addr, cur_dw[0], cur_db[0], cur_db[1]))
        else:  # tiles
            if len(cur_dw) != 1 or not _MATRIX_LABEL_RE.match(cur_dw[0]):
                raise ParseError(
                    f"{path}:{lineno}: {cur_label} must hold exactly one `dw Matrix_*`, "
                    f"found {cur_dw}")
            if not cur_db:
                raise ParseError(f"{path}:{lineno}: {cur_label} has an empty tile stream")
            if cur_db[-1] != TILE_TERMINATOR:
                raise ParseError(
                    f"{path}:{lineno}: {cur_label} stream must end in one ${TILE_TERMINATOR:02X}")
            if TILE_TERMINATOR in cur_db[:-1]:
                raise ParseError(
                    f"{path}:{lineno}: {cur_label} has a ${TILE_TERMINATOR:02X} before the end")
            for j, b in enumerate(cur_db[:-1]):
                if b == TILE_XFLIP and (j + 1 >= len(cur_db) or cur_db[j + 1] >= FIRST_ESCAPE):
                    raise ParseError(
                        f"{path}:{lineno}: {cur_label} ${TILE_XFLIP:02X} must be followed by a "
                        f"real tile byte (< ${FIRST_ESCAPE:02X})")
            tile_lists.append(TileList(cur_label, cur_addr, cur_dw[0], list(cur_db)))
        cur_label = cur_kind = None
        cur_dw = []
        cur_db = []

    for offset, raw in enumerate(lines[section_idx + 1:], start=section_idx + 2):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("SECTION"):
            break  # only one sprite-data section; another section ends it

        label_match = _LABEL_RE.match(line)
        if label_match:
            flush(offset)
            label = label_match.group(1)
            sprite_m = _SPRITE_LABEL_RE.match(label)
            tiles_m = _TILES_LABEL_RE.match(label)
            if sprite_m:
                kind, suffix = "record", sprite_m.group(1)
            elif tiles_m:
                kind, suffix = "tiles", tiles_m.group(1)
            else:
                raise ParseError(
                    f"{path}:{offset}: unexpected label {label}:: in the sprite-data section")
            if int(suffix, 16) != addr:
                raise ParseError(
                    f"{path}:{offset}: {label}:: address suffix ${suffix} != computed "
                    f"section address ${addr:04X}")
            cur_label, cur_kind, cur_addr = label, kind, addr
            continue

        dw_match = _DW_RE.match(line)
        if dw_match:
            if cur_label is None:
                raise ParseError(f"{path}:{offset}: dw before any label")
            ops = _split_operands(dw_match.group("operand"))
            if not ops:
                raise ParseError(f"{path}:{offset}: empty dw operand")
            cur_dw.extend(ops)
            addr += 2 * len(ops)
            continue

        db_match = _DB_RE.match(line)
        if db_match:
            if cur_label is None:
                raise ParseError(f"{path}:{offset}: db before any label")
            tokens = _split_operands(db_match.group("operand"))
            if not tokens:
                raise ParseError(f"{path}:{offset}: empty db operand")
            for tok in tokens:
                if cur_kind == "record":
                    cur_db.append(_parse_signed_byte(tok, path, offset))
                else:
                    cur_db.append(_parse_stream_byte(tok, path, offset))
            addr += len(tokens)
            continue

        raise ParseError(
            f"{path}:{offset}: unexpected line in the sprite-data section: {raw!r}")

    flush(len(lines) + 1)
    return records, tile_lists


# --- tetris.asm: SpriteList identity table ------------------------------------------------------

def _parse_spritelist(text: str, path: Path) -> list[int]:
    """The 94 `dw $XXXX` target addresses following `SpriteList::`, in index order."""
    lines = text.splitlines()
    hits = [i for i, raw in enumerate(lines) if raw.strip().startswith(f"{SPRITELIST_LABEL}::")]
    if len(hits) != 1:
        raise ParseError(
            f"{path}: label {SPRITELIST_LABEL}:: must appear exactly once, found {len(hits)}")

    targets: list[int] = []
    started = False
    for offset, raw in enumerate(lines[hits[0] + 1:], start=hits[0] + 2):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        dw_match = _DW_RE.match(line)
        if not dw_match:
            break  # first non-dw meaningful line ends the table
        started = True
        for tok in _split_operands(dw_match.group("operand")):
            word = _WORD_RE.match(tok)
            if not word:
                raise ParseError(f"{path}:{offset}: not a hex word in SpriteList: {tok!r}")
            targets.append(int(word.group(1), 16))
    if not started:
        raise ParseError(f"{path}: {SPRITELIST_LABEL}:: has no dw entries")
    return targets


# --- tetris.asm: the five Matrix grids (grammar carried from the retired sprite-grids parser) ---

def _parse_grids(text: str, path: Path) -> list[tuple[str, int, list[tuple[int, int]]]]:
    lines = text.splitlines()
    section_idx = None
    for i, raw in enumerate(lines):
        if _GRID_SECTION_RE.match(raw.strip()):
            section_idx = i
            break
    if section_idx is None:
        raise ParseError(f"{path}: grid section anchor `{GRID_SECTION_TEXT}` not found")

    collected: dict[str, list[int]] = {}
    expect = 0
    current: str | None = None
    saw_terminal = False
    for offset, raw in enumerate(lines[section_idx + 1:], start=section_idx + 2):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        label_match = _LABEL_RE.match(line)
        if label_match:
            label = label_match.group(1)
            if label == GRID_TERMINAL_LABEL:
                if expect != len(GRID_TABLES):
                    raise ParseError(
                        f"{path}:{offset}: reached {GRID_TERMINAL_LABEL}:: after only {expect} "
                        f"of {len(GRID_TABLES)} grids")
                saw_terminal = True
                break
            if expect >= len(GRID_TABLES) or label != GRID_TABLES[expect][0]:
                want = GRID_TABLES[expect][0] if expect < len(GRID_TABLES) else GRID_TERMINAL_LABEL
                raise ParseError(f"{path}:{offset}: expected grid {want}::, found {label}::")
            current = label
            collected[label] = []
            expect += 1
            continue
        db_match = _DB_RE.match(line)
        if db_match:
            if current is None:
                raise ParseError(f"{path}:{offset}: grid db before any Matrix_* label")
            for tok in _split_operands(db_match.group("operand")):
                collected[current].append(_parse_stream_byte(tok, path, offset))
            continue
        raise ParseError(f"{path}:{offset}: unexpected line between grid tables: {raw!r}")

    if not saw_terminal:
        raise ParseError(f"{path}: grids ran to EOF without the {GRID_TERMINAL_LABEL}:: boundary")

    out: list[tuple[str, int, list[tuple[int, int]]]] = []
    for idx, (label, addr, n) in enumerate(GRID_TABLES):
        byte_list = collected[label]
        expected_bytes = (GRID_TABLES[idx + 1][1] - addr) if idx + 1 < len(GRID_TABLES) \
            else GRID_LAST_TABLE_BYTES
        if expected_bytes != 2 * n:
            raise ParseError(
                f"{path}: {label} address delta implies {expected_bytes} bytes but N={n}")
        if len(byte_list) != expected_bytes:
            raise ParseError(
                f"{path}: {label} collected {len(byte_list)} bytes, expected {expected_bytes}")
        for j, byte in enumerate(byte_list):
            if byte % GRID_BYTE_STEP != 0:
                raise ParseError(f"{path}: {label} byte {j} = ${byte:02X} is not 8-aligned")
            if byte > GRID_BYTE_MAX:
                raise ParseError(
                    f"{path}: {label} byte {j} = ${byte:02X} exceeds ${GRID_BYTE_MAX:02X}")
        pairs = [(byte_list[k], byte_list[k + 1]) for k in range(0, len(byte_list), 2)]
        out.append((label, addr, pairs))
    total = sum(len(pairs) for _l, _a, pairs in out)
    if total != 75:
        raise ParseError(f"{path}: grids hold {total} pairs, expected 75")
    return out


# --- Composition (the escape state machine) -----------------------------------------------------

def _compose(stream: list[int], grid: list[tuple[int, int]], ctx: str,
             path: Path) -> tuple[list[Part], int, int, int]:
    """Walk one tile stream against its grid, returning (parts, real_tiles, fd, fe). Grid
    over-consumption or a malformed escape is a hard error."""
    parts: list[Part] = []
    fd = fe = 0
    gi = 0
    i = 0
    n = len(stream)
    while i < n:
        b = stream[i]
        if b == TILE_TERMINATOR:
            break
        if b == TILE_XFLIP:
            fd += 1
            if i + 1 >= n or stream[i + 1] >= FIRST_ESCAPE:
                raise ParseError(f"{path}: {ctx}: ${TILE_XFLIP:02X} not followed by a real tile")
            if gi >= len(grid):
                raise ParseError(f"{path}: {ctx}: consumes more grid pairs than the grid holds")
            y, x = grid[gi]
            gi += 1
            parts.append(Part(y, x, True, stream[i + 1]))
            i += 2
            continue
        if b == TILE_SKIP:
            fe += 1
            if gi >= len(grid):
                raise ParseError(f"{path}: {ctx}: consumes more grid pairs than the grid holds")
            gi += 1
            i += 1
            continue
        if gi >= len(grid):
            raise ParseError(f"{path}: {ctx}: consumes more grid pairs than the grid holds")
        y, x = grid[gi]
        gi += 1
        parts.append(Part(y, x, False, b))
        i += 1
    return parts, len(parts), fd, fe


# --- Parse + assemble ---------------------------------------------------------------------------

def parse_sprites(tetris_text: str, sprites_text: str,
                  tetris_path: Path, sprites_path: Path) -> SpritesData:
    if len(SPRITE_NAMES) != SPRITE_COUNT:
        raise ParseError(
            f"name table holds {len(SPRITE_NAMES)} names, expected {SPRITE_COUNT}")
    if len(set(SPRITE_NAMES)) != SPRITE_COUNT:
        raise ParseError("name table has duplicate enumerators")

    records, tile_lists = _parse_sprite_section(sprites_text, sprites_path)
    targets = _parse_spritelist(tetris_text, tetris_path)
    grids = _parse_grids(tetris_text, tetris_path)
    grid_by_label = {label: pairs for label, _addr, pairs in grids}

    # Corpus scale (the sub-parsers grammar-check; the orchestrator pins the counts).
    if len(records) != RECORD_COUNT:
        raise ParseError(
            f"{sprites_path}: found {len(records)} Sprite_* records, expected {RECORD_COUNT}")
    if len(tile_lists) != RECORD_COUNT:
        raise ParseError(
            f"{sprites_path}: found {len(tile_lists)} SpriteTiles_* lists, expected {RECORD_COUNT}")
    if len(targets) != SPRITE_COUNT:
        raise ParseError(
            f"{tetris_path}: SpriteList has {len(targets)} entries, expected {SPRITE_COUNT}")

    records_by_addr = {r.address: r for r in records}
    tiles_by_label = {t.label: t for t in tile_lists}

    # 1:1 topology: every record points at a distinct tile list, covering all of them.
    referenced = [r.tiles_label for r in records]
    if len(set(referenced)) != len(referenced):
        raise ParseError(f"{sprites_path}: two records share a SpriteTiles_* list")
    if set(referenced) != set(tiles_by_label):
        orphaned = sorted(set(tiles_by_label) - set(referenced))
        raise ParseError(
            f"{sprites_path}: tile lists not referenced 1:1 by records: {orphaned}")

    # SpriteList duplicates. Every target must resolve to a record. Exactly four addresses are
    # shared, each by two indices; the alias index of each pair (the _ALT enumerator) is a naming
    # decision the PLAN fixes, not a positional one - $2D08 aliases at index 0x43 even though 0x43
    # precedes the canonical 0x44 - so the check pins the four designated alias indices and asserts
    # the shared-address set independently rather than picking "the later occurrence".
    counts: dict[int, int] = {}
    for index, target in enumerate(targets):
        if target not in records_by_addr:
            raise ParseError(
                f"{tetris_path}: SpriteList[{index:#04x}] target ${target:04X} has no record")
        counts[target] = counts.get(target, 0) + 1
    shared = {addr: c for addr, c in counts.items() if c > 1}
    if shared != {addr: 2 for addr in EXPECTED_DUP_INDICES.values()}:
        raise ParseError(
            f"{tetris_path}: shared SpriteList targets {{{', '.join(f'${a:04X}:{c}' for a, c in sorted(shared.items()))}}} "
            f"!= expected the four addresses {sorted(f'${a:04X}' for a in EXPECTED_DUP_INDICES.values())} twice each")
    for alias_index, addr in EXPECTED_DUP_INDICES.items():
        if targets[alias_index] != addr:
            raise ParseError(
                f"{tetris_path}: SpriteList[{alias_index:#04x}] is ${targets[alias_index]:04X}, "
                f"expected the alias target ${addr:04X}")

    # Compose every identity; accumulate the corpus totals over the 90 distinct tile lists.
    sprites: list[ComposedSprite] = []
    for index, target in enumerate(targets):
        record = records_by_addr[target]
        tile_list = tiles_by_label[record.tiles_label]
        grid = grid_by_label.get(tile_list.grid_label)
        if grid is None:
            raise ParseError(
                f"{sprites_path}: {tile_list.label} points at unknown grid {tile_list.grid_label}")
        parts, _real, _fd, _fe = _compose(tile_list.stream, grid, tile_list.label, sprites_path)
        sprites.append(ComposedSprite(
            index=index, name=SPRITE_NAMES[index], target_address=target,
            record=record, tile_list=tile_list,
            offset_y=_signed(record.offset_y_byte), offset_x=_signed(record.offset_x_byte),
            parts=parts))

    _assert_corpus(tile_lists, grid_by_label, sprites_path)

    return SpritesData(sprites=sprites, records=records, tile_lists=tiles_by_label,
                       grids=grids, grid_by_label=grid_by_label)


def _assert_corpus(tile_lists: list[TileList], grid_by_label: dict[str, list[tuple[int, int]]],
                   path: Path) -> None:
    total_bytes = total_real = total_fd = total_fe = total_ff = 0
    max_parts = 0
    for t in tile_lists:
        parts, real, fd, fe = _compose(t.stream, grid_by_label[t.grid_label], t.label, path)
        consumed = len(parts) + fe
        if consumed > len(grid_by_label[t.grid_label]):
            raise ParseError(f"{path}: {t.label} consumes {consumed} grid pairs, grid holds "
                             f"{len(grid_by_label[t.grid_label])}")
        total_bytes += len(t.stream)
        total_real += real
        total_fd += fd
        total_fe += fe
        total_ff += 1
        max_parts = max(max_parts, real)
    checks = {
        "stream bytes": (total_bytes, CORPUS_STREAM_BYTES),
        "real tiles": (total_real, CORPUS_REAL_TILES),
        "$FD escapes": (total_fd, CORPUS_FD),
        "$FE skips": (total_fe, CORPUS_FE),
        "$FF terminators": (total_ff, CORPUS_FF),
        "max parts": (max_parts, MAX_PARTS),
    }
    for name, (got, want) in checks.items():
        if got != want:
            raise ParseError(f"{path}: corpus {name} = {got}, expected {want}")


# --- Emit: sprite_id.h --------------------------------------------------------------------------

def emit_enum(source_commit: str) -> str:
    body_lines: list[str] = []
    value = 0
    for i, (comment, names) in enumerate(SPRITE_GROUPS):
        if i:
            body_lines.append("")
        body_lines.append(f"    // {comment}")
        for name in names:
            body_lines.append(f"    {name} = 0x{value:02X},")
            value += 1
    body = "\n".join(body_lines)
    return f"""#pragma once
{common.banner("parse_sprites.py", source_commit)}\
// The 94 composed OAM sprites the game can draw, in SpriteList index order. The value is the index
// the game passes in a sprite descriptor (and the score screen passes to pick a rocket ending); it
// is a dense identity space, 0x00..0x5D, with no "none" value - visibility is a separate descriptor
// byte. Four identities alias an earlier target's layout (the _ALT enumerators); they are distinct
// ids with equal composed geometry. The composition each id resolves to lives in src/data/sprites.h;
// the layout and the renderer's walk are specified in docs/contracts/sprites.md.

#include <cstdint>

namespace kirpich {{

enum class SpriteId : std::uint8_t {{
{body}
}};

}}  // namespace kirpich
"""


# --- Emit: sprites_data.inc ---------------------------------------------------------------------

def _emit_part(part: Part) -> str:
    # Pixel positions (y, x) emit as decimal integers. The tile stays a raw OBJ tile-sheet index in
    # hex - a nameless graphics-index space, stored verbatim.
    return (f"{{ .y = {part.y}, .x = {part.x}, "
            f".xflip = {'true' if part.xflip else 'false'}, .tile = 0x{part.tile:02X} }}")


def _emit_sprite_row(sprite: ComposedSprite) -> str:
    head = (f"    {{ .id = SpriteId::{sprite.name}, "
            f".offset_y = {sprite.offset_y}, "
            f".offset_x = {sprite.offset_x}, .parts = {{")
    if not sprite.parts:
        return head + "} },"
    lines = [head]
    for part in sprite.parts:
        lines.append(f"        {_emit_part(part)},")
    lines.append("    } },")
    return "\n".join(lines)


def emit_inc(data: SpritesData, source_commit: str) -> str:
    rows = "\n".join(_emit_sprite_row(s) for s in data.sprites)
    return f"""{common.banner("parse_sprites.py", source_commit)}\
// Included at namespace scope inside src/data/sprites.h (inside `namespace kirpich`), which defines
// SpritePart, Sprite, and BoundedVec first. Each row is one identity's composed OAM layout: the
// record's two signed render offsets and the resolved part list (grid position, x-flip toggle, and
// raw tile byte per part). Row order is the SpriteList index; the four aliased ids carry a full copy
// of the layout they share, so every id is self-contained.
inline constexpr std::array<Sprite, {SPRITE_COUNT}> kSprites{{{{
{rows}
}}}};
"""


# --- Emit: sprites_expected.h (raw serialization fixture) ---------------------------------------

def _byte_row(values: list[int]) -> str:
    return ", ".join(f"0x{b:02X}" for b in values)


def emit_fixture(data: SpritesData, source_commit: str) -> str:
    grid_addr_by_label = {label: addr for label, addr, _pairs in data.grids}

    # Targets.
    targets = [s.target_address for s in data.sprites]
    target_lines = "\n".join(
        "    " + ", ".join(f"0x{t:04X}" for t in targets[i:i + 8]) + ","
        for i in range(0, len(targets), 8))

    # Record rows, in sprites.asm file order, with each list's slice into the flat stream.
    stream_flat: list[int] = []
    offsets: dict[str, int] = {}
    for record in data.records:
        tl = data.tile_lists[record.tiles_label]
        offsets[record.label] = len(stream_flat)
        stream_flat.extend(tl.stream)
    record_lines = []
    for record in data.records:
        tl = data.tile_lists[record.tiles_label]
        record_lines.append(
            f"    {{ .record_address = 0x{record.address:04X}, "
            f".tiles_address = 0x{tl.address:04X}, "
            f".grid_address = 0x{grid_addr_by_label[tl.grid_label]:04X}, "
            f".offset_y_byte = 0x{record.offset_y_byte:02X}, "
            f".offset_x_byte = 0x{record.offset_x_byte:02X}, "
            f".stream_offset = {offsets[record.label]}, .stream_length = {len(tl.stream)} }},")
    record_body = "\n".join(record_lines)

    stream_lines = "\n".join(
        "    " + _byte_row(stream_flat[i:i + 16]) + ","
        for i in range(0, len(stream_flat), 16))

    # Grid rows + flat grid pair bytes.
    grid_pair_flat: list[int] = []
    grid_rows = []
    for label, addr, pairs in data.grids:
        grid_rows.append(
            f"    {{ .grid_address = 0x{addr:04X}, "
            f".pair_offset = {len(grid_pair_flat) // 2}, .pair_count = {len(pairs)} }},")
        for y, x in pairs:
            grid_pair_flat.extend((y, x))
    grid_row_body = "\n".join(grid_rows)
    grid_pair_lines = "\n".join(
        "    " + _byte_row(grid_pair_flat[i:i + 16]) + ","
        for i in range(0, len(grid_pair_flat), 16))

    return f"""#pragma once
{common.banner("parse_sprites.py", source_commit)}\
// Independent fixture for the full-corpus sprite sweep: the ROM's raw serialization as plain
// integers - the SpriteList targets, the 90 record rows (each pointing at its tile-stream slice and
// grid), the concatenated tile streams (escapes and terminators kept), and the five grids' pairs.
// It holds no port type, so a defect in the composed surface in src/data/sprites.h cannot mask the
// sweep. tests/test_sprites.cpp re-runs the escape state machine over these bytes and compares the
// result to kSprites.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// One decoded record: where it lives, the tile list and grid it resolves through, its two raw
// offset bytes (two's complement as stored), and its slice into kExpectedSpriteStreamBytes.
struct SpriteRecordRow {{
    std::uint16_t record_address;
    std::uint16_t tiles_address;
    std::uint16_t grid_address;
    std::uint8_t  offset_y_byte;
    std::uint8_t  offset_x_byte;
    std::uint16_t stream_offset;
    std::uint16_t stream_length;
}};

// One grid: its address and its slice into kExpectedGridPairBytes (a run of pair_count (y, x) pairs).
struct SpriteGridRow {{
    std::uint16_t grid_address;
    std::uint16_t pair_offset;
    std::uint16_t pair_count;
}};

// The 94 SpriteList targets, in index order (four of them repeat an earlier address).
inline constexpr std::array<std::uint16_t, {SPRITE_COUNT}> kExpectedSpriteListTargets{{{{
{target_lines}
}}}};

// The 90 distinct records, in sprites.asm file order.
inline constexpr std::array<SpriteRecordRow, {RECORD_COUNT}> kExpectedSpriteRecordRows{{{{
{record_body}
}}}};

// Every tile stream concatenated in record-row order ({len(stream_flat)} bytes total).
inline constexpr std::array<std::uint8_t, {len(stream_flat)}> kExpectedSpriteStreamBytes{{{{
{stream_lines}
}}}};

// The five grids and their flat (y, x) pair bytes ({len(grid_pair_flat)} bytes = 75 pairs).
inline constexpr std::array<SpriteGridRow, {GRID_COUNT}> kExpectedGridRows{{{{
{grid_row_body}
}}}};
inline constexpr std::array<std::uint8_t, {len(grid_pair_flat)}> kExpectedGridPairBytes{{{{
{grid_pair_lines}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(
            f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's composed sprites + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--enum-out", type=Path)
    parser.add_argument("--inc-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    tetris_path = source_root / "tetris.asm"
    sprites_path = source_root / "sprites.asm"
    for p in (tetris_path, sprites_path):
        if not p.is_file():
            print(f"parse_sprites: source file not found: {p}", file=sys.stderr)
            return 2

    tetris_text = tetris_path.read_bytes().decode("utf-8")
    sprites_text = sprites_path.read_bytes().decode("utf-8")
    data = parse_sprites(tetris_text, sprites_text, tetris_path, sprites_path)
    commit = common.source_commit_of(source_root)

    print(f"parse_sprites: {len(data.sprites)} identities over {len(data.records)} records, "
          f"{CORPUS_REAL_TILES} tiles / {CORPUS_FD} flips / {CORPUS_FE} gaps in "
          f"{CORPUS_STREAM_BYTES} stream bytes; max {MAX_PARTS} parts; corpus asserts passed.")

    outputs = {
        args.enum_out: emit_enum(commit),
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
        print(f"parse_sprites: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_sprites: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
