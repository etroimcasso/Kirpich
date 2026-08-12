#!/usr/bin/env python3
"""Parser for Kirpich's demo data - the two attract-mode joypad recordings and the piece sequence
they share.

The game plays two attract-mode demos by running the normal game loop with recorded input. Two things
drive that replay, stored as three INCBIN binaries at the tail of the main ROM section:

  typeademodata.bin  256 B  128 x 2-byte RLE joypad records for the Type A demo
  typebdemodata.bin  160 B   80 x 2-byte RLE joypad records for the Type B demo
  demopiecelist.bin   48 B   48 piece-spec bytes, shared by both demos

Each joypad record is (heldButtons, framesUntilNext): a Game Boy joypad byte and how many frames it
holds before the next record loads (DemoSimulateJoypad, tetris.asm:769-816). The demos replay gameplay,
so a held byte is the set of game *actions* held that step - the port surfaces each record's held state
as an engine action set (kirpich::Action), not a raw byte, resolving the Game Boy button bits to the
actions the gameplay input handler binds them to (RotateAndShiftPiece, tetris.asm:5910-6028, and the
soft-drop path: A -> rotate clockwise, B -> rotate counter-clockwise, LEFT/RIGHT -> shift, DOWN -> soft
drop). Each piece byte is a $C200 piece-spec value (kind * 4 + rotation) fed to
NextPiece.deterministicChoice in place of the RNG roll; every one is a multiple of four below 28
(kind 0-6, rotation always 0). These are uncopyrightable algorithmic data - input timelines and piece
picks, no expressive content - so unlike the graphics and audio payloads they compile into the binary
rather than living in assets/.

Emission set (a single `--all` invocation writes both):

  src/data/demo.h               the DemoInputRecord struct (an engine ActionSet + a frame count) + the
                                two composed input arrays + the Piece-typed piece list + the counts
  tests/fixtures/demo_expected.h  the three blobs as flat raw-byte arrays, independent of the composed
                                surfaces so a defect (or a wrong button->action mapping) in the header
                                cannot mask the sweep - the test bridges the raw byte to the action set

Inputs are the three .bin files read directly off disk and tetris.asm only to anchor the three INCBIN
directives under their labels (the grammar check that the blobs are still wired where the audit found
them). Every structural expectation is asserted as it is read - including that every pressed button bit
maps to a known game action; a held byte pressing an unmapped button is a hard error, never silently
dropped. No address is derived - nothing in this unit needs one.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

# The three blobs: filename -> expected byte length.
TYPE_A_BIN = "typeademodata.bin"
TYPE_B_BIN = "typebdemodata.bin"
PIECE_BIN = "demopiecelist.bin"

TYPE_A_SIZE = 256               # 128 records x 2 bytes
TYPE_B_SIZE = 160               #  80 records x 2 bytes
PIECE_SIZE = 48                 #  48 piece-spec bytes

RECORD_SIZE = 2                 # (heldButtons, framesUntilNext)

# Piece-spec domain: kind * 4 + rotation, seven kinds (0-6) x four rotations, valid raw 0..27. The demo
# list only ever stores spawn orientation (rotation 0), so every byte is a multiple of four below 28.
PIECE_KIND_COUNT = 7
PIECE_ROTATION_STRIDE = 4
PIECE_MAX_EXCLUSIVE = PIECE_KIND_COUNT * PIECE_ROTATION_STRIDE   # 28

# A Game Boy joypad bit -> the game action it drives during gameplay. Reverse-derived from the gameplay
# input handler (RotateAndShiftPiece, tetris.asm:5910-6028) and the soft-drop path. The demos replay
# gameplay, so a recorded held byte is a set of these actions. B, SELECT, START, and UP never appear in
# either recording and have no piece-control action, so a held byte that presses one is a hard error.
GB_BIT_TO_ACTION = {
    0x01: "RotateClockwise",         # A
    0x02: "RotateCounterClockwise",  # B
    0x10: "MoveRight",               # RIGHT
    0x20: "MoveLeft",                # LEFT
    0x80: "SoftDrop",                # DOWN
}
# The Action enum's declaration order, so a set literal is emitted deterministically.
ACTION_ENUM_ORDER = ["MoveLeft", "MoveRight", "SoftDrop", "RotateClockwise", "RotateCounterClockwise"]
_ALL_BITS = (0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80)

# The INCBIN anchors in tetris.asm: three `Label::` / `INCBIN "file"` pairs, contiguous and in order.
INCBIN_ANCHORS = [
    ("TypeADemoData", TYPE_A_BIN),
    ("TypeBDemoData", TYPE_B_BIN),
    ("DemoPieceList", PIECE_BIN),
]

# C++ emission names.
CPP_RECORD = "DemoInputRecord"
CPP_TYPE_A = "kTypeADemoInputs"
CPP_TYPE_B = "kTypeBDemoInputs"
CPP_PIECES = "kDemoPieceList"
CPP_FIX_A = "kExpectedTypeADemoBytes"
CPP_FIX_B = "kExpectedTypeBDemoBytes"
CPP_FIX_PIECES = "kExpectedDemoPieceListBytes"

_INCBIN_RE = re.compile(r'^INCBIN\s+"([^"]+)"\s*$')
_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::\s*$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_demo"


# --- The INCBIN anchors -------------------------------------------------------------------------

def assert_incbin_anchors(asm_text: str, path: Path) -> None:
    """The three demo INCBINs appear as contiguous `Label::` / `INCBIN "file"` pairs, in the expected
    order with the expected filenames. Confirms the blobs are still wired where the audit found them;
    extracts nothing (the bytes come from the .bin files directly)."""
    # Meaningful lines only (drop blanks and comment-only lines), keeping 1-based line numbers.
    meaningful: list[tuple[int, str]] = []
    for lineno, raw in enumerate(asm_text.splitlines(), start=1):
        body = raw.split(";", 1)[0].strip()
        if body:
            meaningful.append((lineno, body))

    first_label = INCBIN_ANCHORS[0][0]
    starts = [i for i, (_l, b) in enumerate(meaningful) if b == f"{first_label}::"]
    if len(starts) != 1:
        raise ParseError(
            f"{path}: expected exactly one `{first_label}::` label, found {len(starts)}"
        )
    idx = starts[0]

    expected_pairs = len(INCBIN_ANCHORS) * 2
    window = meaningful[idx:idx + expected_pairs]
    if len(window) < expected_pairs:
        raise ParseError(
            f"{path}: the demo INCBIN block is truncated after `{first_label}::`"
        )
    for anchor_i, (label, filename) in enumerate(INCBIN_ANCHORS):
        label_lineno, label_body = window[anchor_i * 2]
        incbin_lineno, incbin_body = window[anchor_i * 2 + 1]
        label_match = _LABEL_RE.match(label_body)
        if not label_match or label_match.group(1) != label:
            raise ParseError(
                f"{path}:{label_lineno}: expected label `{label}::`, found {label_body!r}"
            )
        incbin_match = _INCBIN_RE.match(incbin_body)
        if not incbin_match:
            raise ParseError(
                f"{path}:{incbin_lineno}: expected `INCBIN \"{filename}\"`, found {incbin_body!r}"
            )
        if incbin_match.group(1) != filename:
            raise ParseError(
                f"{path}:{incbin_lineno}: {label} INCBINs {incbin_match.group(1)!r}, "
                f"expected {filename!r}"
            )


# --- The blobs ----------------------------------------------------------------------------------

def read_blob(source_root: Path, filename: str, expected_size: int) -> list[int]:
    """Read a demo .bin file off disk, asserting its exact byte length."""
    path = source_root / filename
    if not path.is_file():
        raise ParseError(f"{path}: demo binary not found")
    data = list(path.read_bytes())
    if len(data) != expected_size:
        raise ParseError(f"{path}: {filename} is {len(data)} bytes, expected {expected_size}")
    return data


def to_records(data: list[int], filename: str, path: Path) -> list[tuple[int, int]]:
    """Pair a joypad blob's bytes into (held, frames) records, asserting an even byte count. The held
    byte is kept raw here; the button->action resolution happens in held_actions."""
    if len(data) % RECORD_SIZE != 0:
        raise ParseError(f"{path}: {filename} has an odd byte count {len(data)}; records are 2 bytes")
    return [(data[i], data[i + 1]) for i in range(0, len(data), RECORD_SIZE)]


def held_actions(held: int, path: Path) -> list[str]:
    """The game actions a held byte represents, in Action enum order. A pressed bit with no mapped
    action (B / SELECT / START / UP - none appear in either recording) is a hard error."""
    names: list[str] = []
    for bit in _ALL_BITS:
        if held & bit:
            action = GB_BIT_TO_ACTION.get(bit)
            if action is None:
                raise ParseError(
                    f"{path}: demo held byte ${held:02X} presses button bit ${bit:02X}, which maps "
                    f"to no game action"
                )
            names.append(action)
    names.sort(key=ACTION_ENUM_ORDER.index)
    return names


def assert_held_mappable(records: list[tuple[int, int]], path: Path) -> None:
    """Every held byte in the stream resolves to game actions (no unmapped button bit)."""
    for held, _frames in records:
        held_actions(held, path)


def assert_piece_domain(data: list[int], path: Path) -> None:
    """Every piece byte is a valid $C200 piece spec in spawn orientation: a multiple of four (rotation
    0) below 28 (kind 0-6)."""
    for i, byte in enumerate(data):
        if byte % PIECE_ROTATION_STRIDE != 0 or byte >= PIECE_MAX_EXCLUSIVE:
            raise ParseError(
                f"{path}: demo piece {i} = ${byte:02X} is not a spawn-orientation piece spec "
                f"(a multiple of {PIECE_ROTATION_STRIDE} below {PIECE_MAX_EXCLUSIVE})"
            )


def parse_demo(source_root: Path) -> dict:
    """Read and assert the three blobs plus the INCBIN anchors. Returns
    {"type_a": [(held, frames)], "type_b": [...], "type_a_bytes": [int], "type_b_bytes": [int],
     "pieces": [int]}."""
    asm_path = source_root / "tetris.asm"
    if not asm_path.is_file():
        raise ParseError(f"{asm_path}: tetris.asm not found")

    assert_incbin_anchors(asm_path.read_bytes().decode("utf-8"), asm_path)

    type_a = read_blob(source_root, TYPE_A_BIN, TYPE_A_SIZE)
    type_b = read_blob(source_root, TYPE_B_BIN, TYPE_B_SIZE)
    pieces = read_blob(source_root, PIECE_BIN, PIECE_SIZE)
    assert_piece_domain(pieces, source_root / PIECE_BIN)

    type_a_records = to_records(type_a, TYPE_A_BIN, source_root / TYPE_A_BIN)
    type_b_records = to_records(type_b, TYPE_B_BIN, source_root / TYPE_B_BIN)
    assert_held_mappable(type_a_records, source_root / TYPE_A_BIN)
    assert_held_mappable(type_b_records, source_root / TYPE_B_BIN)

    return {
        "type_a": type_a_records,
        "type_b": type_b_records,
        "type_a_bytes": type_a,
        "type_b_bytes": type_b,
        "pieces": pieces,
    }


# --- Emit: src/data/demo.h ----------------------------------------------------------------------

def _record_rows(records: list[tuple[int, int]], path: Path) -> str:
    rows: list[str] = []
    for held, frames in records:
        inner = ", ".join(f"Action::{n}" for n in held_actions(held, path))
        rows.append(f"    {{ .held = heldActions({{{inner}}}), .frames = 0x{frames:02X} }},")
    return "\n".join(rows)


def _piece_rows(pieces: list[int]) -> str:
    # Twelve pieces per line, matching the byte-pool wrapping the other parsers use.
    return "\n".join(
        "    " + " ".join(f"Piece{{0x{b:02X}}}," for b in pieces[i:i + 12])
        for i in range(0, len(pieces), 12)
    )


def emit_demo(result: dict, source_commit: str) -> str:
    type_a = result["type_a"]
    type_b = result["type_b"]
    pieces = result["pieces"]
    dummy = Path("<emit>")   # held_actions already validated in parse_demo; the path is only for errors
    return f"""#pragma once
{common.banner("parse_demo.py", source_commit)}\
// Demo data: the two attract-mode joypad recordings and the piece sequence they share.
//
// The game plays two attract-mode demos by running the normal game loop with recorded input in place
// of the player's. Each demo is a run-length-encoded timeline - a list of DemoInputRecords, one per
// input change - and both demos draw their pieces from a single fixed list rather than the RNG.
//
//   - kTypeADemoInputs / kTypeBDemoInputs: the Type A and Type B timelines. Each record is the set of
//     game actions held (held, a retropp::ActionSet over kirpich::Action) and the number of frames it
//     persists before the next record loads (frames). The recordings capture Game Boy joypad state; the
//     port resolves those button bits to the actions the gameplay input handler binds them to, so a
//     record holds actions, not a hardware byte, ready for the demo-replay system to feed into the
//     engine input path. Deriving the pressed edge from the held set is runtime behavior and ports with
//     the input system. The streams do not self-terminate - the demo ends by piece count, so trailing
//     records are simply never reached.
//   - kDemoPieceList: the deterministic piece sequence both demos replay. Each entry is a $C200
//     piece-spec value (kind * 4 + rotation, rotation always 0 = spawn orientation), the same Piece
//     wrapper the piece logic uses. The Type A demo consumes indices 0-15 and the Type B demo 17-29;
//     index 16 is never read (see docs/contracts/demo.md).
//
// This unit is the data only. Feeding the records into the input path, selecting between the two demos,
// and ending a demo by piece count are the demo state machine's job and port with the gameplay logic;
// docs/contracts/demo.md specifies that behavior with source line anchors.
//
// Generated from the disassembly by tools/asm_parser/parse_demo.py; edit the source and regenerate,
// not here.

#include <array>
#include <cstdint>
#include <initializer_list>

#include <retropp/input.h>

#include <kirpich/action.h>
#include <kirpich/piece.h>

namespace kirpich {{

// The engine action set held during one demo step, built from named actions.
[[nodiscard]] constexpr retropp::ActionSet heldActions(std::initializer_list<Action> actions) {{
    retropp::ActionSet set;
    for (const Action action : actions) {{
        set.set(retropp::actionId(action), true);
    }}
    return set;
}}

// One run-length-encoded step of a demo: the actions held, and how many frames they hold.
struct {CPP_RECORD} {{
    retropp::ActionSet held;    // the game actions held this step (empty = no input)
    std::uint8_t       frames;  // frames the state persists before the next record loads

    friend constexpr bool operator==(const {CPP_RECORD}&, const {CPP_RECORD}&) = default;
}};

// Number of records in each timeline, and the length of the shared piece list.
inline constexpr std::uint16_t kTypeADemoInputCount = {len(type_a)};
inline constexpr std::uint16_t kTypeBDemoInputCount = {len(type_b)};
inline constexpr std::uint16_t kDemoPieceCount      = {len(pieces)};

// The Type A demo's action timeline ({len(type_a)} records).
inline constexpr std::array<{CPP_RECORD}, kTypeADemoInputCount> {CPP_TYPE_A} = {{{{
{_record_rows(type_a, dummy)}
}}}};

// The Type B demo's action timeline ({len(type_b)} records).
inline constexpr std::array<{CPP_RECORD}, kTypeBDemoInputCount> {CPP_TYPE_B} = {{{{
{_record_rows(type_b, dummy)}
}}}};

// The deterministic piece sequence both demos replay ({len(pieces)} pieces, spawn orientation).
inline constexpr std::array<Piece, kDemoPieceCount> {CPP_PIECES} = {{{{
{_piece_rows(pieces)}
}}}};

}}  // namespace kirpich
"""


# --- Emit: tests/fixtures/demo_expected.h -------------------------------------------------------

def _flat_bytes(data: list[int]) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 12]) + ","
        for i in range(0, len(data), 12)
    )


def emit_fixture(result: dict, source_commit: str) -> str:
    type_a = result["type_a_bytes"]
    type_b = result["type_b_bytes"]
    pieces = result["pieces"]
    return f"""#pragma once
{common.banner("parse_demo.py", source_commit)}\
// Independent raw-byte fixtures for the full-corpus demo sweep: the three demo blobs exactly as the
// ROM stores them - the two joypad recordings in (held, frames) serialization order and the piece
// list - flat and independent of the composed surfaces in src/data/demo.h. tests/test_demo.cpp bridges
// each raw held byte to the action set the composed record carries, so a defect in the header OR a
// wrong button->action mapping fails the sweep.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// typeademodata.bin - {len(type_a)} bytes ({len(type_a) // RECORD_SIZE} records x 2).
inline constexpr std::array<std::uint8_t, {len(type_a)}> {CPP_FIX_A} = {{{{
{_flat_bytes(type_a)}
}}}};

// typebdemodata.bin - {len(type_b)} bytes ({len(type_b) // RECORD_SIZE} records x 2).
inline constexpr std::array<std::uint8_t, {len(type_b)}> {CPP_FIX_B} = {{{{
{_flat_bytes(type_b)}
}}}};

// demopiecelist.bin - {len(pieces)} piece-spec bytes.
inline constexpr std::array<std::uint8_t, {len(pieces)}> {CPP_FIX_PIECES} = {{{{
{_flat_bytes(pieces)}
}}}};

}}  // namespace kirpich::fixtures
"""


def _assert_ascii(content: str, label: str) -> None:
    if not content.isascii():
        bad = next(ch for ch in content if ord(ch) > 0x7F)
        raise ParseError(f"{label}: emitted output contains a non-ASCII byte (U+{ord(bad):04X})")


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Emit Kirpich's demo data surface (action-set records, piece list) and fixture.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--demo-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    result = parse_demo(source_root)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.demo_out: emit_demo(result, commit),
        args.fixture_out: emit_fixture(result, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        _assert_ascii(content, str(out_path))
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_demo: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_demo: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
