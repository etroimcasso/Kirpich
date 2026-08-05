#!/usr/bin/env python3
"""Parser for Kirpich's core enums.

The kaspermeerts/tetris disassembly is flat: constants.asm is five lines, and there is no
constants/*_constants.asm symbol table. The core-enum surfaces therefore split into three
provenance classes, and this parser covers exactly the two that the source carries a transcribable
symbol set for:

  Class A - named EQU in constants.asm -> parser-emitted C++ header (names AND values):
    include/kirpich/serial_role.h        (MASTER, SLAVE)
    include/kirpich/serial_clock_mode.h  (external / internal transfer clock)

  Class B - label/dispatch-encoded in tetris.asm -> parser-emitted value fixture that guards the
            hand-authored headers against value drift:
    tests/fixtures/core_enums_expected.h (GameState: 54 values $00-$35; SerialState: dispatch 0..3)

Class C (GameType, MusicType, Piece) has no source symbol to transcribe and is hand-authored
port-design; it is not emitted here.

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Every artifact is
transcribed from source; any deviation from the expected structure is a hard error with a source
citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# --- Expected structure (the source contract this parser asserts) -------------------------------

# Class A: the four named constants that must exist verbatim in constants.asm.
EXPECTED_SERIAL_ROLE = {"MASTER": 0x29, "SLAVE": 0x55}
EXPECTED_CLOCK_MODE = {
    "SERIAL_TRANSFER_EXTERNAL_CLOCK": 0x80,
    "SERIAL_TRANSFER_INTERNAL_CLOCK": 0x81,
}

# Class B: the serial dispatch table, in order. Index 0 is the handshake; 1..3 are the three
# serial states; index 4 is a vestigial slot (a bare ret) the running game never dispatches.
EXPECTED_SERIAL_DISPATCH = [
    "Handshake",
    "SerialState_01",
    "SerialState_02",
    "SerialState_03",
    "LoadTilesFromHL.ret",
]
SERIAL_STATE_COUNT = 4  # reachable states: indices 0..3

GAME_STATE_COUNT = 54  # contiguous $00..$35
GAME_STATE_VESTIGIAL = 0x36  # `dw $27EA ; 0x36` - a raw address, not a labelled state


class ParseError(SystemExit):
    """A structural assertion failed. Carries a source citation; halts the emit run (T1)."""

    def __init__(self, message: str) -> None:
        super().__init__(f"parse_core_enums: STRUCTURAL ASSERT FAILED\n  {message}")


# --- Class A: EQU symbol table ------------------------------------------------------------------

_EQU_RE = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+EQU\s+\$([0-9A-Fa-f]+)\s*(?:;.*)?$")


def parse_equ(constants_text: str, path: Path) -> dict[str, int]:
    """Parse `NAME EQU $HH` lines into {name: value}."""
    table: dict[str, int] = {}
    for lineno, line in enumerate(constants_text.splitlines(), start=1):
        if not line.strip() or line.lstrip().startswith(";"):
            continue
        match = _EQU_RE.match(line)
        if not match:
            raise ParseError(f"{path}:{lineno}: unrecognised line in constants.asm: {line!r}")
        table[match.group(1)] = int(match.group(2), 16)
    return table


def assert_equ_values(table: dict[str, int], expected: dict[str, int], path: Path) -> None:
    for name, value in expected.items():
        if name not in table:
            raise ParseError(f"{path}: expected symbol {name} not found in constants.asm")
        if table[name] != value:
            raise ParseError(
                f"{path}: {name} = ${table[name]:02X}, expected ${value:02X} "
                f"(upstream drifted - adjudicate before re-emitting)"
            )


# --- Class B: GameState dispatch + labels -------------------------------------------------------

_DW_GAMESTATE_RE = re.compile(
    r"^\s*dw\s+GameState_([0-9A-Fa-f]{2})\s*(?:;\s*(.*?))?\s*$"
)
_GAMESTATE_LABEL_RE = re.compile(r"^GameState_([0-9A-Fa-f]{2})::")
_DW_VESTIGIAL_RE = re.compile(r"^\s*dw\s+\$27EA\b")


def scan_game_states(tetris_text: str, path: Path) -> list[tuple[int, str, str]]:
    """Return [(value, upstream_label, comment)] for the 54 GameState dispatch entries."""
    lines = tetris_text.splitlines()

    rows: list[tuple[int, str, str]] = []
    saw_vestigial = False
    for line in lines:
        match = _DW_GAMESTATE_RE.match(line)
        if match:
            value = int(match.group(1), 16)
            comment = (match.group(2) or "").strip()
            rows.append((value, f"GameState_{match.group(1).upper()}", comment))
        elif rows and not saw_vestigial and _DW_VESTIGIAL_RE.match(line):
            # The slot immediately past $35: a raw address, documented vestigial over-read.
            saw_vestigial = True

    if len(rows) != GAME_STATE_COUNT:
        raise ParseError(
            f"{path}: found {len(rows)} `dw GameState_XX` dispatch entries, expected "
            f"{GAME_STATE_COUNT}"
        )

    values = [v for v, _, _ in rows]
    expected_values = list(range(0x00, GAME_STATE_COUNT))  # 0x00..0x35
    if values != expected_values:
        raise ParseError(
            f"{path}: GameState dispatch values are not the contiguous set $00..$35 in order; "
            f"got {[f'${v:02X}' for v in values]}"
        )

    if not saw_vestigial:
        raise ParseError(
            f"{path}: expected the vestigial `dw $27EA` slot (0x36) after the 54 GameState "
            f"entries; not found (dispatch table shape changed - adjudicate)"
        )

    # Every dispatched state must have a matching handler label, and vice versa.
    labels = {int(m.group(1), 16) for line in lines if (m := _GAMESTATE_LABEL_RE.match(line))}
    dispatched = set(values)
    if labels != dispatched:
        missing = sorted(dispatched - labels)
        extra = sorted(labels - dispatched)
        raise ParseError(
            f"{path}: GameState labels and dispatch disagree; "
            f"dispatched-without-label={[f'${v:02X}' for v in missing]}, "
            f"label-without-dispatch={[f'${v:02X}' for v in extra]}"
        )

    return rows


# --- Class B: SerialState dispatch --------------------------------------------------------------

_DW_ANY_RE = re.compile(r"^\s*dw\s+([A-Za-z_][\w.]*)\s*(?:;.*)?$")


def scan_serial_states(tetris_text: str, path: Path) -> list[tuple[int, str]]:
    """Return [(index, label)] for the reachable serial states (indices 0..3)."""
    lines = tetris_text.splitlines()

    # Find the serial dispatch: the `rst $28` reached right after loading hSerialState, then the
    # run of `dw` entries that follows.
    anchor = None
    for i, line in enumerate(lines):
        if "[hSerialState]" in line:
            for j in range(i, min(i + 4, len(lines))):
                if "rst $28" in lines[j]:
                    anchor = j
                    break
            if anchor is not None:
                break
    if anchor is None:
        raise ParseError(f"{path}: could not locate the serial dispatch (`[hSerialState]` + rst $28)")

    dispatch: list[str] = []
    for line in lines[anchor + 1:]:
        match = _DW_ANY_RE.match(line)
        if match:
            dispatch.append(match.group(1))
        elif line.strip() == "":
            continue
        else:
            break

    if dispatch != EXPECTED_SERIAL_DISPATCH:
        raise ParseError(
            f"{path}: serial dispatch is {dispatch}, expected {EXPECTED_SERIAL_DISPATCH}"
        )

    # Reachable states only: indices 0..3 (index 4 is the vestigial `LoadTilesFromHL.ret`).
    return [(i, label) for i, label in enumerate(dispatch[:SERIAL_STATE_COUNT])]


# --- Emitters -----------------------------------------------------------------------------------

def _banner(source_commit: str) -> str:
    return (
        "// GENERATED by tools/asm_parser/parse_core_enums.py - do not hand-edit.\n"
        f"// Transcribed from the kaspermeerts/tetris disassembly (upstream @ {source_commit}).\n"
        "// Regenerate with `--all` after any upstream repin; edits here are overwritten.\n"
    )


def _cpp_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def emit_serial_role(table: dict[str, int], source_commit: str) -> str:
    return f"""#pragma once
{_banner(source_commit)}
#include <cstdint>

namespace kirpich {{

// Which side of the two-player link cable this Game Boy is. Elected during the serial handshake:
// a Game Boy that reads the SLAVE code back from the wire becomes the MASTER, and vice versa.
enum class SerialRole : uint8_t {{
    MASTER = 0x{table['MASTER']:02X},
    SLAVE  = 0x{table['SLAVE']:02X},
}};

}}  // namespace kirpich
"""


def emit_serial_clock_mode(table: dict[str, int], source_commit: str) -> str:
    ext = table["SERIAL_TRANSFER_EXTERNAL_CLOCK"]
    internal = table["SERIAL_TRANSFER_INTERNAL_CLOCK"]
    return f"""#pragma once
{_banner(source_commit)}
#include <cstdint>

namespace kirpich {{

// The serial-control clock source written to rSC when starting a transfer. EXTERNAL waits for the
// other Game Boy to drive the clock (this side is the slave); INTERNAL drives it (this side is the
// master).
enum class SerialClockMode : uint8_t {{
    EXTERNAL = 0x{ext:02X},
    INTERNAL = 0x{internal:02X},
}};

}}  // namespace kirpich
"""


def emit_fixture(
    game_states: list[tuple[int, str, str]],
    serial_states: list[tuple[int, str]],
    source_commit: str,
) -> str:
    gs_rows = "\n".join(
        f'    {{0x{value:02X}, "{label}", "{_cpp_escape(comment)}"}},'
        for value, label, comment in game_states
    )
    ss_rows = "\n".join(
        f'    {{0x{index:02X}, "{_cpp_escape(label)}"}},' for index, label in serial_states
    )
    return f"""#pragma once
{_banner(source_commit)}
// The authoritative value sets for the two label/dispatch-encoded core enums, scanned from source.
// The hand-authored headers (include/kirpich/game_state.h, serial_state.h) carry port-design
// *names*; this fixture guards their *values*. tests/test_core_enums.cpp drift-checks the two.

#include <array>
#include <cstdint>
#include <string_view>

namespace kirpich::fixtures {{

struct GameStateRow {{
    std::uint8_t     value;
    std::string_view label;    // upstream GameState_XX label (structural anchor)
    std::string_view comment;  // upstream jump-table comment (informational)
}};

// 54 states, contiguous 0x00..0x35. The 0x36 slot upstream is a raw `dw $27EA` over-read and is
// deliberately absent here - it is not a state.
inline constexpr std::array<GameStateRow, {len(game_states)}> kGameStateExpected{{{{
{gs_rows}
}}}};

struct SerialStateRow {{
    std::uint8_t     index;
    std::string_view label;
}};

// Reachable serial states only: dispatch indices 0..3 (index 4 upstream is a vestigial bare ret).
inline constexpr std::array<SerialStateRow, {len(serial_states)}> kSerialStateExpected{{{{
{ss_rows}
}}}};

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def source_commit_of(source_root: Path) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(source_root), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return out.stdout.strip() or "unknown"
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return "unknown"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's parser-covered core enums.")
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Path to the kaspermeerts/tetris disassembly checkout.")
    parser.add_argument("--all", action="store_true",
                        help="Emit every artifact (the only shipped mode).")
    parser.add_argument("--serial-role-out", type=Path)
    parser.add_argument("--serial-clock-mode-out", type=Path)
    parser.add_argument("--fixture-out", type=Path)
    args = parser.parse_args(argv)

    source_root: Path = args.source_root
    constants_path = source_root / "constants.asm"
    tetris_path = source_root / "tetris.asm"
    for required in (constants_path, tetris_path):
        if not required.is_file():
            print(f"parse_core_enums: source file not found: {required}", file=sys.stderr)
            return 2

    commit = source_commit_of(source_root)

    equ = parse_equ(constants_path.read_text(), constants_path)
    assert_equ_values(equ, EXPECTED_SERIAL_ROLE, constants_path)
    assert_equ_values(equ, EXPECTED_CLOCK_MODE, constants_path)

    tetris_text = tetris_path.read_text()
    game_states = scan_game_states(tetris_text, tetris_path)
    serial_states = scan_serial_states(tetris_text, tetris_path)

    outputs = {
        args.serial_role_out: emit_serial_role(equ, commit),
        args.serial_clock_mode_out: emit_serial_clock_mode(equ, commit),
        args.fixture_out: emit_fixture(game_states, serial_states, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content)
        print(f"parse_core_enums: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_core_enums: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
