#!/usr/bin/env python3
"""Parser for Kirpich's scoring data - line-clear awards, soft-drop points, bonus-ending
thresholds, and the level-progression constants.

Every score in the ROM is a BCD-encoded number: the hex digits ARE the decimal digits, so $1200
is 1200 points. The four line-clear base scores (40/100/300/1200) appear as 16-bit immediates at
three independent sites - the Type-A award routine (`AddLineClearScore::`), the Type-B end-of-round
scoreboard (`UpdateScoreboard::`), and the Type-B scoreboard display state (`GameState_05::`) - and
this parser extracts all three and asserts they agree value-for-value in the same kind order. The
bonus-ending ladder (`GameState_0D::`) compares the score's top BCD byte against three descending
thresholds, picking a rocket sprite per tier; the remaining constants each live at a single
anchored site (the `cp a, $14` level cap, the `ld a, $25` Type-B line goal, the `ld de, 1`
soft-drop tally operand, and `AddBCD::`'s `$99` saturation tail).

Emission set = the data `.inc` (both tables plus the transcribed constants, decoded to decimal)
+ the test fixture (the same data as raw wire bytes, independent of the typed surface):

  src/data/generated/scoring_data.inc     kLineClearScores + kBonusEndings designated-initializer
                                          rows and the four constants, included at namespace scope
                                          inside src/data/scoring.h
  tests/fixtures/scoring_expected.h       the BCD immediates, threshold/sprite/constant bytes as
                                          plain integers, so a defect in the engine header cannot
                                          mask the full-corpus sweeps

Python 3 stdlib only. Port-time tooling - never a build-time or CI dependency. Any deviation from
the expected structure is a hard error with a source citation, never silently accepted.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import common

# --- Expected structure (the source contract this parser asserts) -------------------------------

KIND_NAMES = ("Single", "Double", "Triple", "Tetris")
KIND_ENUMERATORS = ("SINGLE", "DOUBLE", "TRIPLE", "TETRIS")
STATS_LABELS = ("wSinglesCount", "wDoublesCount", "wTriplesCount", "wTetrisCount")

AWARD_LABEL = "AddLineClearScore"
SCOREBOARD_LABEL = "UpdateScoreboard"
DISPLAY_LABEL = "GameState_05"
BONUS_LABEL = "GameState_0D"
LEVEL_UP_LABEL = "Call_244B"
TALLY_LABEL = "tallySoftDropPoints"
ADD_BCD_LABEL = "AddBCD"

STRIDE_COMMENT = "5 bytes per type"
BONUS_TIER_COUNT = 3

# C++ emission.
CPP_SCORES_TABLE = "kLineClearScores"
CPP_BONUS_TABLE = "kBonusEndings"

_GLOBAL_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)::")
_LD_DE_HEX_RE = re.compile(r"^ld\s+de\s*,\s*\$([0-9A-Fa-f]{1,4})$")
_LD_DE_DEC_RE = re.compile(r"^ld\s+de\s*,\s*(\d+)$")
_LD_BC_HEX_RE = re.compile(r"^ld\s+bc\s*,\s*\$([0-9A-Fa-f]{1,4})$")
_LD_HL_SYM_RE = re.compile(r"^ld\s+hl\s*,\s*([A-Za-z_][A-Za-z0-9_]*)$")
_LD_HL_HEX_RE = re.compile(r"^ld\s+hl\s*,\s*\$([0-9A-Fa-f]{1,4})$")
_LD_B_HEX_RE = re.compile(r"^ld\s+b\s*,\s*\$([0-9A-Fa-f]{1,2})$")
_LD_A_HEX_RE = re.compile(r"^ld\s+a\s*,\s*\$([0-9A-Fa-f]{1,2})$")
_CP_A_HEX_RE = re.compile(r"^cp\s+a\s*,\s*\$([0-9A-Fa-f]{1,2})$")
_SCORE_COMMENT_RE = re.compile(r"^(\d+) points for a (Single|Double|Triple|Tetris)$")
_TIER_COMMENT_RE = re.compile(r"^At least (\d+)k(?: points)?$")


class ParseError(common.ParseError):
    """A structural assertion failed. Carries a source citation; halts the emit run."""

    script = "parse_scoring"


@dataclass(frozen=True)
class Instr:
    """One non-blank source line inside a routine: its body (whitespace-collapsed) and comment."""

    lineno: int
    body: str
    comment: str


@dataclass(frozen=True)
class ScoringData:
    """Everything the emitters consume, straight off the parse."""

    score_bcd: tuple[int, ...]       # four 16-bit BCD immediates, kind order
    threshold_bcd: tuple[int, ...]   # three threshold bytes, ROM check order (descending)
    rocket_sprites: tuple[int, ...]  # one sprite byte per tier, same order
    level_cap: int                   # plain binary byte - $14 IS decimal 20
    type_b_goal_bcd: int             # BCD byte - $25 is 25 lines
    soft_drop_operand: int           # the AddBCD operand per tallied soft-drop row
    saturation_byte: int             # the byte AddBCD stores three times to saturate


# --- Instruction-stream helpers -----------------------------------------------------------------

def _split_comment(line: str) -> tuple[str, str]:
    """Split a source line into its directive and its trailing comment text (without the `;`)."""
    body, sep, comment = line.partition(";")
    return " ".join(body.split()), comment.strip() if sep else ""


def _region(lines: list[str], label: str, path: Path) -> list[Instr]:
    """The instruction stream of one global routine: from its `Label::` to the next global label.

    Blank and comment-only lines are dropped; local labels (`.addScore`) stay in the stream as
    plain bodies. The label must exist exactly once.
    """
    hits = [i for i, line in enumerate(lines) if line.strip().startswith(f"{label}::")]
    if not hits:
        raise ParseError(f"{path}: label {label}:: not found")
    if len(hits) > 1:
        found = ", ".join(str(i + 1) for i in hits)
        raise ParseError(f"{path}: label {label}:: defined more than once (lines {found})")

    instrs: list[Instr] = []
    for offset, raw_line in enumerate(lines[hits[0] + 1:], start=hits[0] + 2):
        stripped = raw_line.strip()
        if _GLOBAL_LABEL_RE.match(stripped):
            return instrs
        body, comment = _split_comment(stripped)
        if not body:
            continue
        instrs.append(Instr(offset, body, comment))
    raise ParseError(f"{path}: reached end of file without a label terminating {label}::")


def _bcd_decode(value: int, nibbles: int, path: Path, lineno: int) -> int:
    """Read a BCD immediate as the decimal number its hex digits spell. Any nibble > 9 is a
    transcription hazard, never silently accepted."""
    decoded = 0
    for shift in range(nibbles - 1, -1, -1):
        digit = (value >> (shift * 4)) & 0xF
        if digit > 9:
            raise ParseError(
                f"{path}:{lineno}: ${value:0{nibbles}X} is not valid BCD (nibble {digit:X} > 9)"
            )
        decoded = decoded * 10 + digit
    return decoded


# --- Per-site extraction ------------------------------------------------------------------------

def _parse_award_site(lines: list[str], path: Path) -> list[int]:
    """`AddLineClearScore::` - the four BCD immediates, each pinned by upstream's own
    '; N points for a <Kind>' comment, plus the 5-byte stats stride."""
    instrs = _region(lines, AWARD_LABEL, path)

    stride = [i for i in instrs if i.body == "ld bc, 5"]
    if len(stride) != 1 or stride[0].comment != STRIDE_COMMENT:
        raise ParseError(
            f"{path}: {AWARD_LABEL}:: must hold exactly one `ld bc, 5` with the "
            f"'; {STRIDE_COMMENT}' stride comment"
        )

    values: list[int] = []
    for instr in instrs:
        hex_match = _LD_DE_HEX_RE.match(instr.body)
        if not hex_match:
            continue
        row = len(values)
        if row >= len(KIND_NAMES):
            raise ParseError(
                f"{path}:{instr.lineno}: more than {len(KIND_NAMES)} score immediates in "
                f"{AWARD_LABEL}::"
            )
        value = int(hex_match.group(1), 16)
        decoded = _bcd_decode(value, 4, path, instr.lineno)
        comment_match = _SCORE_COMMENT_RE.match(instr.comment)
        if not comment_match:
            raise ParseError(
                f"{path}:{instr.lineno}: expected a '; N points for a <Kind>' anchor comment, "
                f"found {instr.comment!r}"
            )
        if int(comment_match.group(1)) != decoded or comment_match.group(2) != KIND_NAMES[row]:
            raise ParseError(
                f"{path}:{instr.lineno}: anchor comment {instr.comment!r} disagrees with "
                f"${value:04X} at row {row} (expected {decoded} points for a {KIND_NAMES[row]})"
            )
        values.append(value)

    if len(values) != len(KIND_NAMES):
        raise ParseError(
            f"{path}: {AWARD_LABEL}:: yielded {len(values)} score immediates, "
            f"expected {len(KIND_NAMES)}"
        )
    return values


def _parse_scoreboard_site(lines: list[str], path: Path) -> list[int]:
    """`UpdateScoreboard::` - the same four immediates behind the kind-discriminator chain.

    Each group is `ld de, $BCD / ld bc, $addr / ld hl, w<Kind>Count` followed by its
    discriminator (`and a` for 0, `cp a, 1`, `cp a, 2`, fall-through for 3). The stats labels
    and discriminator values anchor the kind ORDER; the soft-drop dispatch (`cp a, 4` to the
    tally routine) must also be present."""
    instrs = _region(lines, SCOREBOARD_LABEL, path)
    bodies = [i.body for i in instrs]

    try:
        dispatch = bodies.index("cp a, 4")
    except ValueError:
        raise ParseError(f"{path}: {SCOREBOARD_LABEL}:: is missing the `cp a, 4` soft-drop dispatch")
    if dispatch + 1 >= len(instrs) or bodies[dispatch + 1] != f"jr z, {TALLY_LABEL}":
        raise ParseError(
            f"{path}: {SCOREBOARD_LABEL}:: `cp a, 4` must dispatch to {TALLY_LABEL} via `jr z`"
        )

    values: list[int] = []
    pos = dispatch + 2
    for row, stats_label in enumerate(STATS_LABELS):
        if pos >= len(instrs):
            raise ParseError(f"{path}: {SCOREBOARD_LABEL}:: ended before kind group {row}")
        instr = instrs[pos]
        hex_match = _LD_DE_HEX_RE.match(instr.body)
        if not hex_match:
            raise ParseError(
                f"{path}:{instr.lineno}: expected `ld de, $<score>` opening kind group {row}, "
                f"found {instr.body!r}"
            )
        values.append(int(hex_match.group(1), 16))
        _bcd_decode(values[-1], 4, path, instr.lineno)

        if pos + 2 >= len(instrs) or not _LD_BC_HEX_RE.match(bodies[pos + 1]):
            raise ParseError(
                f"{path}:{instr.lineno}: kind group {row} must load its print address via "
                f"`ld bc, $<addr>`"
            )
        hl_instr = instrs[pos + 2]
        hl_match = _LD_HL_SYM_RE.match(hl_instr.body)
        if not hl_match or hl_match.group(1) != stats_label:
            raise ParseError(
                f"{path}:{hl_instr.lineno}: kind group {row} must point at {stats_label}, "
                f"found {hl_instr.body!r}"
            )
        pos += 3

        if row < 3:
            expected_cp = "and a" if row == 0 else f"cp a, {row}"
            if (pos + 1 >= len(instrs) or bodies[pos] != expected_cp
                    or bodies[pos + 1] != "jr z, .addScore"):
                raise ParseError(
                    f"{path}:{instrs[pos].lineno}: kind group {row} must close with "
                    f"`{expected_cp}` / `jr z, .addScore`"
                )
            pos += 2
        else:
            if pos >= len(instrs) or bodies[pos] != ".addScore":
                raise ParseError(
                    f"{path}:{instrs[pos - 1].lineno}: kind group 3 must fall through into "
                    f".addScore"
                )
    return values


def _parse_display_site(lines: list[str], path: Path) -> tuple[list[int], int]:
    """`GameState_05::` - the four `ld de, $<score>` / `call PrintLineClearScores` display pairs,
    plus the Type-B line goal (`ld a, $25` stored straight into hLines)."""
    instrs = _region(lines, DISPLAY_LABEL, path)

    values: list[int] = []
    for pos, instr in enumerate(instrs):
        hex_match = _LD_DE_HEX_RE.match(instr.body)
        if not hex_match:
            continue
        if (pos + 2 >= len(instrs) or not _LD_HL_HEX_RE.match(instrs[pos + 1].body)
                or instrs[pos + 2].body != "call PrintLineClearScores"):
            raise ParseError(
                f"{path}:{instr.lineno}: display pair must be `ld de, $<score>` / "
                f"`ld hl, $<addr>` / `call PrintLineClearScores`"
            )
        if len(values) >= len(KIND_NAMES):
            raise ParseError(
                f"{path}:{instr.lineno}: more than {len(KIND_NAMES)} display pairs in "
                f"{DISPLAY_LABEL}::"
            )
        values.append(int(hex_match.group(1), 16))
        _bcd_decode(values[-1], 4, path, instr.lineno)
    if len(values) != len(KIND_NAMES):
        raise ParseError(
            f"{path}: {DISPLAY_LABEL}:: yielded {len(values)} display pairs, "
            f"expected {len(KIND_NAMES)}"
        )

    goals = [
        instr for pos, instr in enumerate(instrs)
        if _LD_A_HEX_RE.match(instr.body)
        and pos + 1 < len(instrs) and instrs[pos + 1].body == "ldh [hLines], a"
    ]
    if len(goals) != 1:
        raise ParseError(
            f"{path}: {DISPLAY_LABEL}:: must store exactly one immediate into hLines "
            f"(the Type-B line goal), found {len(goals)}"
        )
    goal_byte = int(_LD_A_HEX_RE.match(goals[0].body).group(1), 16)
    _bcd_decode(goal_byte, 2, path, goals[0].lineno)
    return values, goal_byte


def _parse_bonus_ladder(lines: list[str], path: Path) -> tuple[list[int], list[int]]:
    """`GameState_0D::` - the rocket ladder: `ld b, $<base>` then a `cp a, $<threshold>` /
    `jr nc, .bonusEnding` rung per tier with `inc b` between rungs. Sprite bytes derive from the
    base plus the rung's position; each rung's '; At least NNNk' comment must agree with its
    threshold."""
    instrs = _region(lines, BONUS_LABEL, path)
    bodies = [i.body for i in instrs]

    if "ld hl, wScore + 2" not in bodies:
        raise ParseError(
            f"{path}: {BONUS_LABEL}:: must read the score's top byte via `ld hl, wScore + 2`"
        )
    base_hits = [pos for pos, body in enumerate(bodies) if _LD_B_HEX_RE.match(body)]
    if len(base_hits) != 1:
        raise ParseError(
            f"{path}: {BONUS_LABEL}:: must load exactly one rocket sprite base via `ld b, $<n>`, "
            f"found {len(base_hits)}"
        )
    base_pos = base_hits[0]
    base_sprite = int(_LD_B_HEX_RE.match(bodies[base_pos]).group(1), 16)

    thresholds: list[int] = []
    sprites: list[int] = []
    pos = base_pos + 1
    while True:
        if pos + 1 >= len(instrs):
            raise ParseError(f"{path}: {BONUS_LABEL}:: ladder ran off the end of the routine")
        instr = instrs[pos]
        cp_match = _CP_A_HEX_RE.match(instr.body)
        if not cp_match or bodies[pos + 1] != "jr nc, .bonusEnding":
            break
        threshold = int(cp_match.group(1), 16)
        decoded = _bcd_decode(threshold, 2, path, instr.lineno)
        comment_match = _TIER_COMMENT_RE.match(instr.comment)
        if not comment_match:
            raise ParseError(
                f"{path}:{instr.lineno}: expected an '; At least NNNk' anchor comment on the "
                f"ladder rung, found {instr.comment!r}"
            )
        if int(comment_match.group(1)) * 1000 != decoded * 10000:
            raise ParseError(
                f"{path}:{instr.lineno}: anchor comment {instr.comment!r} disagrees with "
                f"threshold ${threshold:02X} (top-byte {decoded} = {decoded * 10000} points)"
            )
        thresholds.append(threshold)
        sprites.append(base_sprite + len(sprites))
        pos += 2
        if pos < len(instrs) and bodies[pos] == "inc b":
            pos += 1
        else:
            break

    if len(thresholds) != BONUS_TIER_COUNT:
        raise ParseError(
            f"{path}: {BONUS_LABEL}:: ladder yielded {len(thresholds)} tiers, "
            f"expected {BONUS_TIER_COUNT}"
        )
    for row in range(1, len(thresholds)):
        if thresholds[row] >= thresholds[row - 1]:
            raise ParseError(
                f"{path}: {BONUS_LABEL}:: ladder thresholds must strictly descend "
                f"(tier {row}: ${thresholds[row]:02X} >= ${thresholds[row - 1]:02X})"
            )
    return thresholds, sprites


def _parse_level_cap(lines: list[str], path: Path) -> int:
    """`Call_244B::` - the level cap: the sole `cp a, $<cap>` immediately gated by `ret z`.
    hLevel is plain binary, so $14 IS decimal 20 - no BCD decode here."""
    instrs = _region(lines, LEVEL_UP_LABEL, path)
    caps = [
        instr for pos, instr in enumerate(instrs)
        if _CP_A_HEX_RE.match(instr.body)
        and pos + 1 < len(instrs) and instrs[pos + 1].body == "ret z"
    ]
    if len(caps) != 1:
        raise ParseError(
            f"{path}: {LEVEL_UP_LABEL}:: must gate the level cap with exactly one "
            f"`cp a, $<cap>` / `ret z`, found {len(caps)}"
        )
    return int(_CP_A_HEX_RE.match(caps[0].body).group(1), 16)


def _parse_soft_drop_operand(lines: list[str], path: Path) -> int:
    """`tallySoftDropPoints::` - the AddBCD operand added per tallied row: the `ld de, <n>`
    ahead of the routine's first `call AddBCD`."""
    instrs = _region(lines, TALLY_LABEL, path)
    operand: int | None = None
    for instr in instrs:
        dec_match = _LD_DE_DEC_RE.match(instr.body)
        if dec_match:
            operand = int(dec_match.group(1), 10)
        elif instr.body == f"call {ADD_BCD_LABEL}":
            if operand is None:
                raise ParseError(
                    f"{path}:{instr.lineno}: {TALLY_LABEL}:: calls {ADD_BCD_LABEL} without a "
                    f"decimal `ld de, <n>` operand ahead of it"
                )
            return operand
    raise ParseError(f"{path}: {TALLY_LABEL}:: never calls {ADD_BCD_LABEL}")


def _parse_saturation(lines: list[str], path: Path) -> int:
    """`AddBCD::` - the saturation tail: `ld a, $99` stored into all three score bytes
    (`ldd [hl], a` twice, then `ld [hl], a`)."""
    instrs = _region(lines, ADD_BCD_LABEL, path)
    tails = [
        (pos, instr) for pos, instr in enumerate(instrs)
        if _LD_A_HEX_RE.match(instr.body)
        and [i.body for i in instrs[pos + 1:pos + 4]] == ["ldd [hl], a", "ldd [hl], a", "ld [hl], a"]
    ]
    if len(tails) != 1:
        raise ParseError(
            f"{path}: {ADD_BCD_LABEL}:: must saturate with exactly one `ld a, $<byte>` stored "
            f"into all three score bytes, found {len(tails)}"
        )
    pos, instr = tails[0]
    byte = int(_LD_A_HEX_RE.match(instr.body).group(1), 16)
    _bcd_decode(byte, 2, path, instr.lineno)
    return byte


# --- Parse + cross-site agreement ---------------------------------------------------------------

def parse_scoring(text: str, path: Path) -> ScoringData:
    """Extract the whole scoring corpus and assert every site agrees."""
    lines = text.splitlines()

    award = _parse_award_site(lines, path)
    scoreboard = _parse_scoreboard_site(lines, path)
    display, goal_byte = _parse_display_site(lines, path)

    if not (award == scoreboard == display):
        raise ParseError(
            f"{path}: the three base-score sites disagree - "
            f"{AWARD_LABEL} {[f'${v:04X}' for v in award]}, "
            f"{SCOREBOARD_LABEL} {[f'${v:04X}' for v in scoreboard]}, "
            f"{DISPLAY_LABEL} {[f'${v:04X}' for v in display]}"
        )

    thresholds, sprites = _parse_bonus_ladder(lines, path)

    return ScoringData(
        score_bcd=tuple(award),
        threshold_bcd=tuple(thresholds),
        rocket_sprites=tuple(sprites),
        level_cap=_parse_level_cap(lines, path),
        type_b_goal_bcd=goal_byte,
        soft_drop_operand=_parse_soft_drop_operand(lines, path),
        saturation_byte=_parse_saturation(lines, path),
    )


def _bcd_int(value: int, nibbles: int) -> int:
    """Decode an already-validated BCD immediate (parse_scoring validated every nibble)."""
    return int(f"{value:0{nibbles}X}")


# --- Emit ---------------------------------------------------------------------------------------

def emit_inc(data: ScoringData, source_commit: str) -> str:
    score_rows = "\n".join(
        f"    {{ .kind = LineClearKind::{name}, .points = {_bcd_int(bcd, 4):4d} }},"
        for name, bcd in zip(KIND_ENUMERATORS, data.score_bcd)
    )
    bonus_rows = "\n".join(
        f"    {{ .min_score = {_bcd_int(bcd, 2) * 10000}, .rocket_sprite = 0x{sprite:02X} }},"
        for bcd, sprite in zip(data.threshold_bcd, data.rocket_sprites)
    )
    saturation = _bcd_int(data.saturation_byte, 2) * 10101  # $99 in all three bytes: 999999
    return f"""{common.banner("parse_scoring.py", source_commit)}\
// Included at namespace scope inside src/data/scoring.h (inside `namespace kirpich`). The two
// scoring tables and the transcribed constants, decoded to decimal - BCD is the ROM's wire
// format and never enters the typed surface.

// Base award per line-clear kind; the award site multiplies by (level + 1).
inline constexpr std::array<LineClearScoreEntry, {len(data.score_bcd)}> {CPP_SCORES_TABLE}{{{{
{score_rows}
}}}};

// Bonus-ending tiers in the ROM's check order - descending threshold, first match wins.
inline constexpr std::array<BonusEndingEntry, {len(data.threshold_bcd)}> {CPP_BONUS_TABLE}{{{{
{bonus_rows}
}}}};

// Levelling up stops at this level; the gravity table ends here for the same reason.
inline constexpr std::uint8_t kLevelCap = {data.level_cap};

// A Type-B game counts down from this many lines.
inline constexpr std::uint8_t kTypeBLineGoal = {_bcd_int(data.type_b_goal_bcd, 2)};

// Points per soft-dropped row the Type-B tally adds per tick.
inline constexpr std::uint8_t kSoftDropPointsPerRow = {data.soft_drop_operand};

// The score saturates here instead of rolling over - every accumulation clamps to it.
inline constexpr std::uint32_t kScoreSaturation = {saturation};
"""


def emit_fixture(data: ScoringData, source_commit: str) -> str:
    scores = ", ".join(f"0x{v:04X}" for v in data.score_bcd)
    thresholds = ", ".join(f"0x{v:02X}" for v in data.threshold_bcd)
    sprites = ", ".join(f"0x{v:02X}" for v in data.rocket_sprites)
    return f"""#pragma once
{common.banner("parse_scoring.py", source_commit)}\
// Independent fixture for the full-corpus scoring sweeps: the ROM's wire bytes as plain
// integers - the BCD score immediates, the threshold/sprite ladder, and the single-site
// constant bytes. Deliberately holds no port type, so a defect in src/data/scoring.h cannot
// mask the sweeps in tests/test_scoring.cpp.

#include <array>
#include <cstdint>

namespace kirpich::fixtures {{

// The four 16-bit BCD score immediates, in kind order (Single, Double, Triple, Tetris).
inline constexpr std::array<std::uint16_t, {len(data.score_bcd)}> kExpectedLineClearScoreBcd{{{{
    {scores},
}}}};

// The bonus-ending ladder in ROM check order: the top-score-byte thresholds and the rocket
// sprite byte each tier selects.
inline constexpr std::array<std::uint8_t, {len(data.threshold_bcd)}> kExpectedBonusThresholdBcd{{{{
    {thresholds},
}}}};
inline constexpr std::array<std::uint8_t, {len(data.rocket_sprites)}> kExpectedRocketSprites{{{{
    {sprites},
}}}};

// Single-site constant bytes, as the ROM spells them.
inline constexpr std::uint8_t  kExpectedLevelCapByte = 0x{data.level_cap:02X};      // plain binary
inline constexpr std::uint8_t  kExpectedTypeBLineGoalBcd = 0x{data.type_b_goal_bcd:02X};  // BCD
inline constexpr std::uint16_t kExpectedSoftDropOperand = 0x{data.soft_drop_operand:04X};
inline constexpr std::uint8_t  kExpectedSaturationByte = 0x{data.saturation_byte:02X};      // BCD, stored thrice

}}  // namespace kirpich::fixtures
"""


# --- Driver -------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Emit Kirpich's scoring tables + fixture.")
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
        print(f"parse_scoring: source file not found: {asm_path}", file=sys.stderr)
        return 2

    text = asm_path.read_bytes().decode("utf-8")
    data = parse_scoring(text, asm_path)
    commit = common.source_commit_of(source_root)

    outputs = {
        args.inc_out: emit_inc(data, commit),
        args.fixture_out: emit_fixture(data, commit),
    }
    wrote = 0
    for out_path, content in outputs.items():
        if out_path is None:
            continue
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(content, encoding="ascii")
        print(f"parse_scoring: wrote {out_path}")
        wrote += 1

    if wrote == 0:
        print("parse_scoring: no --*-out paths given; nothing written "
              "(structural asserts still ran and passed).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
