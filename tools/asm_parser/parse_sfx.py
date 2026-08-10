#!/usr/bin/env python3
"""Parser for Kirpich's SFX + residual sound-driver data surface.

Every byte of this unit is a literal `db`/`dw` in the kaspermeerts/tetris `audio.asm`, and every
address is computable directly from the source: an SM83 instruction's encoded length is fixed by its
mnemonic and operand forms, so walking the section from its ROM origin ($6480) and summing those
lengths yields the address of every label - the same information a symbol table holds, computed from
the text. There is nothing to assemble. The walk is self-proving: the disassembly is dense with
address-encoding labels (Data_659B = $659B, WavePattern_6EA9 = $6EA9, .label_660E = $660E, ...), and
each one is a checkpoint whose walked address must equal the address its name encodes; a single wrong
instruction size fails loudly at the next label.

The SFX "sequences" are code routines that drive the APU from small register-image blobs interleaved
with that code; those routines ride the engine-hosted driver image. What ports here is therefore
(a) the three 1-based ID spaces the game writes to wNewSquareSFXID / wNewNoiseSFXID / wNewWaveSFXID
-> three enums, and (b) the mechanical-configuration data blobs + residual driver data (note-pitch
physics table, vibrato offsets, wave timbres, the noise-note table, pause-tune notes), transcribed to
a raw-byte verification fixture. All of it is mechanical config in the same class as the StereoData /
note-length tables - raw bytes, no hashes.

Emission set (single `--all` invocation):

  src/data/sfx.h                  the three SfxId enums (wire values) + table/count/span constants.
                                  Fully parser-emitted; every value is an exact transcribed wire byte
                                  or a walked address.
  tests/fixtures/sfx_expected.h   every data blob as raw bytes over one flat pool, with its walked
                                  {name, addr, length, poolOffset} in address order. Independent of
                                  the typed surface.

Structural asserts (any deviation is a hard error with an audio.asm citation, never silently
accepted): the instruction-size walk hits every address-encoding label exactly; the four pointer
tables have their fixed sizes and tile [$6480, $64B0) onto the music pointer table; each table's
targets match the routine names the enums are derived from; PlayWaveSFX direct-compares IDs 1 and 2
(there is no wave table); every blob's byte run has its expected length; the blobs are strictly
increasing and non-overlapping and the tail tiles into the note-length region at $6EF9;
WavePattern_6EB9 is referenced nowhere (dead data, a pinned quirk).

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

AUDIO_SECTION_ORIGIN = 0x6480          # SECTION "Audio", ROM0[$6480]
MUSIC_POINTERS_ADDR = 0x64B0           # the tables tile onto MusicPointers; asserted
NOTE_LENGTH_REGION_BASE = 0x6EF9       # the blob tail tiles into this (the note-length region)

SQUARE_SFX_START_ADDR = 0x6480
SQUARE_SFX_CONTINUE_ADDR = 0x6490
NOISE_SFX_START_ADDR = 0x64A0
NOISE_SFX_CONTINUE_ADDR = 0x64A8
SQUARE_SFX_COUNT = 8
NOISE_SFX_COUNT = 4
WAVE_SFX_COUNT = 2                     # PlayWaveSFX direct-compares IDs 1 and 2; no table

SQUARE_START_TARGETS = [
    "StartTinkSFX", "StartChangeScreenSFX", "StartRotatePieceSFX", "StartShiftPieceSFX",
    "StartGarbageAttackSFX", "StartLineClearSFX", "StartTetrisSFX", "StartLevelUpSFX",
]
SQUARE_CONTINUE_TARGETS = [
    "ContinueTinkSFX", "ContinueGenericSquareSFX", "ContinueRotatePieceSFX",
    "ContinueGenericSquareSFX", "ContinueGenericSquareSFX", "ContinueLineClearSFX",
    "ContinueTetrisSFX", "ContinueLevelUpSFX",
]
NOISE_START_TARGETS = [
    "StartStackFallSFX", "StartLockPieceSFX", "StartIgnitionSFX", "StartLiftoffSFX",
]
NOISE_CONTINUE_TARGETS = [
    "ContinueGenericNoiseSFX", "ContinueGenericNoiseSFX", "ContinueGenericNoiseSFX",
    "ContinueLiftoffSFX",
]

# The three ID enums (port-authored). Values are the 1-based bytes game code writes to the wire
# variables; NONE = 0 is the no-op. SquareSfxId/NoiseSfxId names follow the start-table order and its
# comments; WaveSfxId follows PlayWaveSFX's direct-compare dispatch (1 -> Tetris sweep, 2 -> game
# over). No STOP sentinel: SFX has no $FF semantics.
SQUARE_SFX_ENUM = [
    ("NONE", 0, "no-op"),
    ("TINK", 1, "menu cursor movement"),
    ("CHANGE_SCREEN", 2, "menu change screen"),
    ("ROTATE_PIECE", 3, "rotate piece"),
    ("SHIFT_PIECE", 4, "shift piece"),
    ("GARBAGE_ATTACK", 5, "garbage attack sweep"),
    ("LINE_CLEAR", 6, "line clear"),
    ("TETRIS", 7, "tetris"),
    ("LEVEL_UP", 8, "level up"),
]
NOISE_SFX_ENUM = [
    ("NONE", 0, "no-op"),
    ("STACK_FALL", 1, "stack settles after a line clear"),
    ("LOCK_PIECE", 2, "piece locks"),
    ("IGNITION", 3, "rocket ignition"),
    ("LIFTOFF", 4, "rocket flying"),
]
WAVE_SFX_ENUM = [
    ("NONE", 0, "no-op"),
    ("TETRIS_SWEEP", 1, "end-of-Tetris wave sweep"),
    ("GAME_OVER", 2, "game over"),
]

# WavePattern_6EB9 is dead data - referenced nowhere in the ROM (a pinned quirk).
DEAD_WAVE_PATTERN = "WavePattern_6EB9"

# Every blob in the unit, in address order, with its expected byte length. Global labels end in `::`;
# the two pause-tune notes are locals (`.name`) inside _UpdateAudio. A `dw` run contributes two bytes
# per word (little-endian, as in ROM).
BLOBS = [
    (".pauseTuneFirstNoteData", 4),
    (".pauseTuneSecondNoteData", 4),
    ("Data_659B", 5),
    ("Data_65A0", 5),
    ("Data_65A5", 5),
    ("Data_65E7", 5),
    ("Data_65EC", 5),
    ("Data_6623", 5),
    ("LevelUpNote1", 5),
    ("LevelUpNote2", 5),
    ("LevelUpNote3", 5),
    ("LevelUpNote4", 5),
    ("Data_6695", 5),
    ("Data_669A", 11),
    ("Data_66A5", 10),
    ("Data_66EC", 5),
    ("Data_66F1", 6),
    ("Data_66F7", 5),
    ("Data_6740", 5),
    ("Data_6745", 4),
    ("Data_6749", 4),
    ("Data_674D", 4),
    ("Data_6751", 4),
    ("LiftOffNoiseData", 36),
    ("LiftOffVolumeData", 36),
    ("Data_67FB", 5),
    ("Data_6857", 3),
    ("Data_685A", 2),
    ("Data_685C", 3),
    ("Data_685F", 2),
    ("Data_6861", 3),
    ("Data_6864", 2),
    ("Data_6866", 3),
    ("Data_6869", 2),
    ("VibratoOffsets", 55),
    ("NotePitches", 146),  # 73 dw: leading $F00 placeholder + chromatic C2..B7 (72 notes)
    ("Data_6E94", 21),
    ("WavePattern_6EA9", 16),
    (DEAD_WAVE_PATTERN, 16),
    ("WavePattern_6EC9", 16),
    ("GameOverWavePattern", 16),
    ("DefaultWavePattern", 16),
]

SECTION_RE = re.compile(r'^SECTION\s+"[^"]*",\s*ROM0\[\$([0-9A-Fa-f]+)\]')
GLOBAL_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::?$")   # Name: or Name::
LOCAL_LABEL_RE = re.compile(r"^(\.[A-Za-z_][A-Za-z0-9_]*):?$")   # .name or .name:
POINTER_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
ADDR_LABEL_RE = re.compile(r"_([0-9A-Fa-f]{4})$")                # ..._659B, .label_660E


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_sfx"


# --- Small helpers ------------------------------------------------------------------------------

def _strip_comment(line: str) -> str:
    return line.partition(";")[0].strip()


def _parse_byte(token: str, path: Path, lineno: int) -> int:
    token = token.strip()
    try:
        value = int(token[1:], 16) if token.startswith("$") else int(token, 10)
    except ValueError as exc:
        raise ParseError(f"{path}:{lineno}: not a byte value: {token!r}") from exc
    if not 0 <= value <= 0xFF:
        raise ParseError(f"{path}:{lineno}: value {value} out of byte range: {token!r}")
    return value


def _parse_word(token: str, path: Path, lineno: int) -> int:
    token = token.strip()
    if not token.startswith("$"):
        raise ParseError(f"{path}:{lineno}: dw operand is not a word literal: {token!r}")
    try:
        value = int(token[1:], 16)
    except ValueError as exc:
        raise ParseError(f"{path}:{lineno}: not a word literal: {token!r}") from exc
    if not 0 <= value <= 0xFFFF:
        raise ParseError(f"{path}:{lineno}: word {value} out of range: {token!r}")
    return value


def _find_label(lines: list[str], label: str, path: Path) -> int:
    if label.startswith("."):
        hits = [i for i, ln in enumerate(lines)
                if LOCAL_LABEL_RE.match(ln.strip()) and ln.strip().rstrip(":") == label]
    else:
        hits = [i for i, ln in enumerate(lines) if ln.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label} not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label} defined more than once (lines {found})")
    return hits[0]


def _read_blob(lines: list[str], label: str, path: Path) -> list[int]:
    """The contiguous db/dw run under `label`, expanded to bytes (dw little-endian). Stops at the
    first line that is not db/dw (the next label, or the code that follows the data)."""
    start = _find_label(lines, label, path)
    out: list[int] = []
    for offset, raw in enumerate(lines[start + 1:], start=start + 2):
        body = _strip_comment(raw)
        if not body:
            continue
        if body.startswith("db "):
            out += [_parse_byte(tok, path, offset) for tok in body[3:].split(",")]
        elif body.startswith("dw "):
            for tok in body[3:].split(","):
                word = _parse_word(tok, path, offset)
                out += [word & 0xFF, (word >> 8) & 0xFF]
        else:
            break
    return out


# --- SM83 instruction sizing --------------------------------------------------------------------
#
# An instruction's encoded length is fixed by its mnemonic and operand forms - the classic 1/2/3-byte
# SM83 rules. Nothing here assembles opcodes; it only sizes them, which is all the address walk needs.

REG8 = {"a", "b", "c", "d", "e", "h", "l"}
REG16 = {"af", "bc", "de", "hl", "sp"}
SIMPLE_MEM = {"[hl]", "[hli]", "[hld]", "[hl+]", "[hl-]", "[bc]", "[de]", "[c]"}
CB_MNEMONICS = {"rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl", "bit", "res", "set"}
ONE_BYTE_NO_OPERAND = {
    "nop", "halt", "di", "ei", "daa", "cpl", "scf", "ccf",
    "rlca", "rrca", "rla", "rra", "ret", "reti",
}
ALU_MNEMONICS = {"add", "adc", "sub", "sbc", "and", "or", "xor", "cp"}


def _operands(body: str) -> tuple[str, list[str]]:
    parts = body.split(None, 1)
    mnem = parts[0].lower()
    ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
    return mnem, ops


def _is_mem_addr(op: str) -> bool:
    """A bracketed operand that is a full 16-bit address (ld [nn],a / ld a,[nn]), not [hl]/[c]/etc."""
    return op.startswith("[") and op.lower() not in SIMPLE_MEM


def _is_immediate(op: str) -> bool:
    """An operand carrying an encoded immediate: a number, $literal, or a constant/label expression -
    anything that is not a register name or a memory operand."""
    o = op.lower()
    if o in REG8 or o in REG16 or op.startswith("["):
        return False
    return True


def _instruction_size(body: str, path: Path, lineno: int) -> int:
    mnem, ops = _operands(body)
    if mnem == "db":
        return len(ops)
    if mnem == "dw":
        return 2 * len(ops)
    if mnem == "ds":
        return int(ops[0], 0)
    if mnem in CB_MNEMONICS:
        return 2
    if mnem in ONE_BYTE_NO_OPERAND:
        return 1
    if mnem == "stop":
        return 2
    if mnem in {"push", "pop", "rst"}:
        return 1
    if mnem == "jr":
        return 2
    if mnem == "call":
        return 3
    if mnem == "jp":
        return 1 if ops and ops[-1] in {"hl", "[hl]"} else 3
    if mnem in {"inc", "dec"}:
        return 1
    if mnem in ALU_MNEMONICS:
        if mnem == "add" and len(ops) == 2 and ops[0] == "hl":
            return 1  # add hl, rr
        if mnem == "add" and len(ops) == 2 and ops[0] == "sp":
            return 2  # add sp, e8
        return 2 if _is_immediate(ops[-1]) else 1
    if mnem in {"ldi", "ldd"}:
        return 1
    if mnem == "ldh":
        return 1 if any(o == "[c]" for o in ops) else 2
    if mnem == "ld":
        op1, op2 = ops[0], ops[1]
        if _is_mem_addr(op1) or _is_mem_addr(op2):
            return 3  # ld [nn], a / ld a, [nn]
        if op1 in REG16 and op2 not in REG16 and op2 not in REG8 and not op2.startswith("["):
            return 3  # ld rr, n16
        if (op1 in REG8 or op1 == "[hl]") and _is_immediate(op2):
            return 2  # ld r, n8 / ld [hl], n8
        return 1
    raise ParseError(f"{path}:{lineno}: cannot size instruction: {body!r}")


def walk_addresses(lines: list[str], path: Path) -> dict[str, int]:
    """Walk every SECTION from its ROM origin, summing instruction sizes, and return the address of
    each blob label. Every address-encoding label is a checkpoint proving the walk."""
    addr: int | None = None
    blob_names = {label for label, _ in BLOBS}
    result: dict[str, int] = {}
    for lineno, raw in enumerate(lines, start=1):
        body = _strip_comment(raw)
        if not body or body.startswith("INCLUDE"):
            continue
        m = SECTION_RE.match(body)
        if m:
            addr = int(m.group(1), 16)
            continue
        if addr is None:
            raise ParseError(f"{path}:{lineno}: content before any SECTION: {body!r}")

        gm = GLOBAL_LABEL_RE.match(body)
        lm = LOCAL_LABEL_RE.match(body)
        if gm:
            name = gm.group(1)
        elif lm:
            name = lm.group(1)
        else:
            addr += _instruction_size(body, path, lineno)
            continue

        em = ADDR_LABEL_RE.search(name)
        if em:
            want = int(em.group(1), 16)
            if want != addr:
                raise ParseError(
                    f"{path}:{lineno}: {name} walked to ${addr:04X} but its name encodes "
                    f"${want:04X} (an instruction size is wrong before here)"
                )
        if name in blob_names:
            result[name] = addr
    missing = blob_names - result.keys()
    if missing:
        raise ParseError(f"{path}: blob labels never reached by the walk: {sorted(missing)}")
    return result


# --- audio.asm structure asserts ----------------------------------------------------------------

def _read_pointer_targets(lines: list[str], label: str, count: int, path: Path) -> list[str]:
    start = _find_label(lines, label, path)
    targets: list[str] = []
    for offset, raw in enumerate(lines[start + 1:], start=start + 2):
        body = _strip_comment(raw)
        if not body:
            continue
        if POINTER_LABEL_RE.match(body):
            break
        if not body.startswith("dw "):
            raise ParseError(f"{path}:{offset}: expected dw inside {label}: {raw.strip()!r}")
        for tok in body[3:].split(","):
            targets.append(tok.strip())
    if len(targets) != count:
        raise ParseError(f"{path}: {label} has {len(targets)} entries, expected {count}")
    return targets


def assert_pointer_tables(lines: list[str], path: Path) -> None:
    """The four tables carry exactly their expected targets and tile [$6480, $64B0) onto the
    music pointer table."""
    tables = (
        ("SquareSFXStartPointers", SQUARE_START_TARGETS),
        ("SquareSFXContinuePointers", SQUARE_CONTINUE_TARGETS),
        ("NoiseSFXStartPointers", NOISE_START_TARGETS),
        ("NoiseSFXContinuePointers", NOISE_CONTINUE_TARGETS),
    )
    addr = AUDIO_SECTION_ORIGIN
    for label, expected in tables:
        targets = _read_pointer_targets(lines, label, len(expected), path)
        if targets != expected:
            raise ParseError(f"{path}: {label} targets {targets} != expected {expected}")
        addr += len(expected) * 2
    if addr != MUSIC_POINTERS_ADDR:
        raise ParseError(
            f"{path}: SFX tables tile to ${addr:04X}, expected MusicPointers at "
            f"${MUSIC_POINTERS_ADDR:04X}"
        )


def assert_wave_dispatch(lines: list[str], path: Path) -> None:
    """PlayWaveSFX direct-compares IDs 1 -> StartTetrisSweepSFX and 2 -> StartGameOverSFX. There is
    no wave pointer table; this dispatch is what WaveSfxId's two values mean."""
    start = _find_label(lines, "PlayWaveSFX", path)
    seen: dict[int, str] = {}
    pending: int | None = None
    for raw in lines[start + 1:]:
        body = _strip_comment(raw)
        if not body:
            continue
        if POINTER_LABEL_RE.match(body) and not body.startswith("PlayWaveSFX"):
            break
        m = re.match(r"^cp\s+a,\s*(\d+)$", body)
        if m:
            pending = int(m.group(1))
            continue
        m = re.match(r"^jp\s+z,\s*([A-Za-z_][A-Za-z0-9_.]*)$", body)
        if m and pending is not None:
            seen.setdefault(pending, m.group(1))
            pending = None
    if seen.get(1) != "StartTetrisSweepSFX" or seen.get(2) != "StartGameOverSFX":
        raise ParseError(
            f"{path}: PlayWaveSFX dispatch {seen} != {{1: StartTetrisSweepSFX, 2: StartGameOverSFX}}"
        )


def assert_dead_wave_pattern(lines: list[str], path: Path) -> None:
    """WavePattern_6EB9 is dead: it is defined once and referenced nowhere else."""
    refs = [i + 1 for i, ln in enumerate(lines)
            if DEAD_WAVE_PATTERN in ln and not ln.strip().startswith(f"{DEAD_WAVE_PATTERN}::")]
    if refs:
        raise ParseError(f"{path}: {DEAD_WAVE_PATTERN} expected dead but referenced at lines {refs}")


def collect_blobs(
    lines: list[str], addrs: dict[str, int], path: Path
) -> list[tuple[str, int, list[int]]]:
    """Read every blob's bytes, assert each run's length, and check the blobs are strictly increasing,
    non-overlapping, and tile at their tail into the note-length region at $6EF9."""
    out: list[tuple[str, int, list[int]]] = []
    for label, expected_len in BLOBS:
        data = _read_blob(lines, label, path)
        if len(data) != expected_len:
            raise ParseError(f"{path}: {label} has {len(data)} bytes, expected {expected_len}")
        out.append((label, addrs[label], data))

    for (n1, a1, d1), (n2, a2, _d2) in zip(out, out[1:]):
        if a1 + len(d1) > a2:
            raise ParseError(
                f"{path}: {n1} (${a1:04X} + {len(d1)}) overlaps {n2} (${a2:04X})"
            )
    last_name, last_addr, last_data = out[-1]
    end = last_addr + len(last_data)
    if end != NOTE_LENGTH_REGION_BASE:
        raise ParseError(
            f"{path}: last blob {last_name} ends at ${end:04X}, expected the note-length region "
            f"at ${NOTE_LENGTH_REGION_BASE:04X}"
        )
    return out


# --- Emit: src/data/sfx.h -----------------------------------------------------------------------

def _enum(name: str, rows: list[tuple[str, int, str]]) -> str:
    width = max(len(n) for n, _, _ in rows)
    body = "\n".join(f"    {n:<{width}} = 0x{v:02X},  // {d}" for n, v, d in rows)
    return f"enum class {name} : std::uint8_t {{\n{body}\n}};"


def emit_header(source_commit: str) -> str:
    return f"""#pragma once
{common.banner("parse_sfx.py", source_commit)}\
// The three SFX ID spaces + the SFX/driver data address map. Like the music data, the SFX
// sequences are driver code that rides the engine-hosted ROM image; the port carries
// only the identifiers the game writes to wNewSquareSFXID / wNewNoiseSFXID / wNewWaveSFXID and the
// constants that locate the tables. The data blobs themselves are verified in
// tests/fixtures/sfx_expected.h.

#include <cstdint>

namespace kirpich {{

// wNewSquareSFXID: 1-based index into SquareSFXStartPointers; 0 is the no-op.
{_enum("SquareSfxId", SQUARE_SFX_ENUM)}

// wNewNoiseSFXID: 1-based index into NoiseSFXStartPointers; 0 is the no-op.
{_enum("NoiseSfxId", NOISE_SFX_ENUM)}

// wNewWaveSFXID: PlayWaveSFX direct-compares these (no pointer table); 0 is the no-op.
{_enum("WaveSfxId", WAVE_SFX_ENUM)}

inline constexpr std::uint16_t kAudioSectionBase            = 0x{AUDIO_SECTION_ORIGIN:04X};
inline constexpr std::uint16_t kSquareSfxStartPointersAddr  = 0x{SQUARE_SFX_START_ADDR:04X};
inline constexpr std::uint16_t kSquareSfxContinuePointersAddr = 0x{SQUARE_SFX_CONTINUE_ADDR:04X};
inline constexpr std::uint16_t kNoiseSfxStartPointersAddr   = 0x{NOISE_SFX_START_ADDR:04X};
inline constexpr std::uint16_t kNoiseSfxContinuePointersAddr = 0x{NOISE_SFX_CONTINUE_ADDR:04X};
inline constexpr std::uint8_t  kSquareSfxCount              = {SQUARE_SFX_COUNT};
inline constexpr std::uint8_t  kNoiseSfxCount               = {NOISE_SFX_COUNT};
inline constexpr std::uint8_t  kWaveSfxCount                = {WAVE_SFX_COUNT};

}}  // namespace kirpich
"""


# --- Emit: tests/fixtures/sfx_expected.h --------------------------------------------------------

def _blob_rows(blobs: list[tuple[str, int, list[int]]]) -> tuple[str, str, int]:
    pool: list[int] = []
    rows: list[str] = []
    name_w = max(len(n) for n, _, _ in blobs) + 2  # + surrounding quotes
    for name, addr, data in blobs:
        offset = len(pool)
        pool += data
        quoted = f'"{name}"'
        rows.append(
            f'    {{ .name = {quoted:<{name_w}}, .addr = 0x{addr:04X}, '
            f'.length = {len(data):3d}, .poolOffset = {offset:4d} }},'
        )
    pool_text = "\n".join(
        "    " + ", ".join(f"0x{b:02X}" for b in pool[i:i + 12]) + ","
        for i in range(0, len(pool), 12)
    )
    return "\n".join(rows), pool_text, len(pool)


def emit_fixture(blobs: list[tuple[str, int, list[int]]], source_commit: str) -> str:
    rows, pool_text, pool_len = _blob_rows(blobs)
    return f"""#pragma once
{common.banner("parse_sfx.py", source_commit)}\
// Independent raw-byte fixture for the full-corpus SFX sweep. Every SFX register-image blob and the
// residual driver data (note-pitch table, vibrato offsets, noise-note table, wave patterns,
// pause-tune notes) as {{name, addr, length, poolOffset}} rows over one flat pool, in address order.
// Addresses are walked from the audio.asm section origin; all bytes are mechanical configuration
// (StereoData / note-length class), no hashes. Deliberately independent of src/data/sfx.h.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

struct SfxBlobExpected {{
    std::string_view name;       // the audio.asm label
    std::uint16_t    addr;       // walked ROM address
    std::uint16_t    length;
    std::uint16_t    poolOffset; // into kSfxBlobPool
    bool operator==(const SfxBlobExpected&) const = default;
}};

inline constexpr std::array<SfxBlobExpected, {len(blobs)}> kExpectedSfxBlobs{{{{
{rows}
}}}};

inline constexpr std::array<std::uint8_t, {pool_len}> kSfxBlobPool{{{{
{pool_text}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's SFX data surface + fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--header-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    audio_path = args.source_root / "audio.asm"
    if not audio_path.is_file():
        print(f"parse_sfx: source file not found: {audio_path}", file=sys.stderr)
        return 2
    lines = audio_path.read_bytes().decode("utf-8").splitlines()

    assert_pointer_tables(lines, audio_path)
    assert_wave_dispatch(lines, audio_path)
    assert_dead_wave_pattern(lines, audio_path)
    addrs = walk_addresses(lines, audio_path)
    blobs = collect_blobs(lines, addrs, audio_path)

    commit = common.source_commit_of(args.source_root)
    outputs = {
        args.header_out: emit_header(commit),
        args.fixture_out: emit_fixture(blobs, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_sfx: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_sfx: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
