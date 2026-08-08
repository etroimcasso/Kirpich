#!/usr/bin/env python3
"""Unit tests for parse_scoring.py.

Three layers per the parser test discipline:
  1. Helper units          - the BCD decoder (validity + decode) and the comment splitter.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a
                             raise path: missing/duplicate labels, tri-site disagreement, broken
                             kind-order anchors, invalid BCD, a malformed bonus ladder, missing
                             single-site constants), plus the shape of both emitted artifacts.
  3. End-to-end            - the real ../tetris/tetris.asm, asserting the decoded scores,
                             thresholds, sprites, and constants. Skips cleanly when the
                             disassembly checkout is absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_scoring
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_scoring as ps  # noqa: E402


# --- Valid synthetic corpus (the baseline the edge-case tests perturb) --------------------------
#
# One section per parsed routine, mirroring the real tetris.asm shapes - including the third
# ladder rung's `cp a , $10` space-before-comma quirk, which the real source contains.

ADD_BCD = """\
AddBCD::    ; TODO Name?
    ld a, e
    add [hl]
    daa
    ldi [hl], a
    ld a, 0
    adc [hl]
    daa
    ld [hl], a
    ret nc
    ld a, $99           ; Saturate number at 999 999
    ldd [hl], a
    ldd [hl], a
    ld [hl], a
    ret
"""

DISPLAY = """\
GameState_05::
    ld hl, $C802
    ld de, ScoreboardTilemap
    call LoadPlayingFieldTilemap
    ldh a, [hTypeBLevel]
    and a
    jr z, .nextState
    ld de, $0040
    ld hl, $C827
    call PrintLineClearScores
    ld de, $0100
    ld hl, $C887
    call PrintLineClearScores
    ld de, $0300
    ld hl, $C8E7
    call PrintLineClearScores
    ld de, $1200
    ld hl, $C947
    call PrintLineClearScores
.nextState
    ld a, 128           ; A little over 2 seconds
    ldh [hTimer1], a
    ld a, $25           ; BCD encoded
    ldh [hLines], a
    ld a, $0B
    ldh [hGameState], a
    ret
"""

TALLY = """\
tallySoftDropPoints::
    xor a
    ld [$C0C6], a
    ld de, wSoftDropPoints
    ld a, [de]
    or l
    jp z, Call_25D9._nextState         ; What? Bug
    dec hl
    ld de, 1            ;  And add one to the BCD encoded number of soft drops
    ld hl, wSoftDropPointsBCD
    push de
    call AddBCD
    pop de
    ld hl, wScore
    call AddBCD
    ret
"""

SCOREBOARD = """\
UpdateScoreboard::
    ld a, [$C0C6]
    and a
    ret z
    ld a, [wScoreboardState]
    cp a, 4
    jr z, tallySoftDropPoints
    ld de, $0040
    ld bc, $9823
    ld hl, wSinglesCount
    and a
    jr z, .addScore
    ld de, $0100
    ld bc, $9883
    ld hl, wDoublesCount
    cp a, 1
    jr z, .addScore
    ld de, $0300
    ld bc, $98E3
    ld hl, wTriplesCount
    cp a, 2
    jr z, .addScore
    ld de, $1200
    ld bc, $9943
    ld hl, wTetrisCount
.addScore
    call Call_25D9
    ret
"""

BONUS = """\
GameState_0D::
    ldh a, [hTimer1]
    and a
    ret nz
    ldh a, [hGameType]
    cp a, $37           ; Type A
    jr nz, .noBonusEnding
    ld hl, wScore + 2   ; Upper 2 digits of the 6 digit score
    ld a, [hl]
    ld b, $58           ; Rocket sprite
    cp a, $20           ; At least 200k points
    jr nc, .bonusEnding
    inc b
    cp a, $15           ; At least 150k
    jr nc, .bonusEnding
    inc b
    cp a , $10          ; At least 100k
    jr nc, .bonusEnding
.noBonusEnding
    ld a, $04
.nextState
    ldh [hGameState], a
    ret

.bonusEnding
    ld a, b
    ret
"""

AWARD = """\
AddLineClearScore::
    ldh a, [hGameType]
    cp a, $37
    ret nz
    ld hl, wLineClearStats
    ld bc, 5            ; 5 bytes per type
    ld a, [hl]
    ld de, $0040        ; 40 points for a Single
    and a
    jr nz, .awardScore
    add hl, bc
    ld a, [hl]
    ld de, $0100        ; 100 points for a Double
    and a
    jr nz, .awardScore
    add hl, bc
    ld a, [hl]
    ld de, $0300        ; 300 points for a Triple
    and a
    jr nz, .awardScore
    add hl, bc
    ld de, $1200        ; 1200 points for a Tetris
    ld a, [hl]
    and a
    ret z
.awardScore
    ld [hl], $00
    ldh a, [hLevel]
    ld b, a
    inc b               ; Multiply score with level+1
    ret
"""

LEVEL_UP = """\
Call_244B::
    ldh a, [hGameState]
    and a
    ret nz
    ld hl, hLevel
    ld a, [hl]
    cp a, $14
    ret z
    call Call_249D
    ret

TrailingRoutine::
    ret
"""

SECTION_ORDER = ("add_bcd", "display", "tally", "scoreboard", "bonus", "award", "level_up")
DEFAULT_SECTIONS = {
    "add_bcd": ADD_BCD,
    "display": DISPLAY,
    "tally": TALLY,
    "scoreboard": SCOREBOARD,
    "bonus": BONUS,
    "award": AWARD,
    "level_up": LEVEL_UP,
}


def _corpus(**overrides: str) -> str:
    sections = dict(DEFAULT_SECTIONS, **overrides)
    return "\n".join(sections[name] for name in SECTION_ORDER)


P = Path("tetris.asm")


# --- Layer 1: helpers + valid parse -------------------------------------------------------------

class BcdDecode(unittest.TestCase):
    def test_decodes_sixteen_bit_immediate(self):
        self.assertEqual(ps._bcd_decode(0x1200, 4, P, 1), 1200)

    def test_decodes_single_byte(self):
        self.assertEqual(ps._bcd_decode(0x25, 2, P, 1), 25)

    def test_invalid_nibble_raises(self):
        with self.assertRaises(SystemExit):
            ps._bcd_decode(0x0A40, 4, P, 1)

    def test_bcd_int_matches_decode(self):
        self.assertEqual(ps._bcd_int(0x0300, 4), 300)


class SplitComment(unittest.TestCase):
    def test_splits_and_collapses_whitespace(self):
        self.assertEqual(ps._split_comment("cp a , $10          ; At least 100k"),
                         ("cp a , $10", "At least 100k"))

    def test_no_comment_yields_empty_text(self):
        self.assertEqual(ps._split_comment("    inc b"), ("inc b", ""))


class ParseValid(unittest.TestCase):
    def test_full_corpus_parses(self):
        data = ps.parse_scoring(_corpus(), P)
        self.assertEqual(data.score_bcd, (0x0040, 0x0100, 0x0300, 0x1200))
        self.assertEqual(data.threshold_bcd, (0x20, 0x15, 0x10))
        self.assertEqual(data.rocket_sprites, (0x58, 0x59, 0x5A))
        self.assertEqual(data.level_cap, 0x14)
        self.assertEqual(data.type_b_goal_bcd, 0x25)
        self.assertEqual(data.soft_drop_operand, 1)
        self.assertEqual(data.saturation_byte, 0x99)

    def test_space_before_comma_rung_is_accepted(self):
        """The real source spells the third rung `cp a , $10`; the default corpus carries it."""
        self.assertIn("cp a , $10", _corpus())
        self.assertEqual(ps.parse_scoring(_corpus(), P).threshold_bcd[2], 0x10)


# --- Layer 2: synthetic edge cases that MUST raise ----------------------------------------------

class LabelsMustRaise(unittest.TestCase):
    def test_missing_award_label_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(award=AWARD.replace("AddLineClearScore::", "Renamed::")), P)

    def test_duplicate_award_label_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus() + "\n" + AWARD, P)

    def test_missing_scoreboard_label_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(scoreboard=SCOREBOARD.replace("UpdateScoreboard::", "X::")), P)

    def test_unterminated_final_routine_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(level_up=LEVEL_UP.replace("TrailingRoutine::\n    ret\n", "")), P)


class TriSiteAgreementMustRaise(unittest.TestCase):
    def test_display_site_disagreement_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(display=DISPLAY.replace("ld de, $0300", "ld de, $0500")), P)

    def test_scoreboard_site_disagreement_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(scoreboard=SCOREBOARD.replace("ld de, $1200", "ld de, $1000")), P)


class AwardSiteMustRaise(unittest.TestCase):
    def test_wrong_anchor_comment_kind_raises(self):
        broken = AWARD.replace("; 300 points for a Triple", "; 300 points for a Double")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(award=broken), P)

    def test_anchor_comment_value_mismatch_raises(self):
        """The comment pins the immediate: changing the value without the comment must trip."""
        broken = AWARD.replace("ld de, $0100        ; 100 points", "ld de, $0200        ; 100 points")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(award=broken), P)

    def test_missing_stride_comment_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(award=AWARD.replace("; 5 bytes per type", "")), P)

    def test_invalid_bcd_immediate_raises(self):
        broken = AWARD.replace("ld de, $0040        ; 40 points for a Single",
                               "ld de, $0A40        ; 40 points for a Single")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(award=broken), P)


class ScoreboardOrderMustRaise(unittest.TestCase):
    def test_swapped_stats_labels_raise(self):
        broken = (SCOREBOARD.replace("wDoublesCount", "SWAP")
                            .replace("wTriplesCount", "wDoublesCount")
                            .replace("SWAP", "wTriplesCount"))
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(scoreboard=broken), P)

    def test_wrong_discriminator_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(scoreboard=SCOREBOARD.replace("cp a, 2", "cp a, 3")), P)

    def test_missing_soft_drop_dispatch_raises(self):
        broken = SCOREBOARD.replace("    cp a, 4\n    jr z, tallySoftDropPoints\n", "")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(scoreboard=broken), P)


class DisplaySiteMustRaise(unittest.TestCase):
    def test_extra_display_pair_raises(self):
        extra = "    ld de, $2000\n    ld hl, $C900\n    call PrintLineClearScores\n"
        broken = DISPLAY.replace("\n.nextState\n", "\n" + extra + ".nextState\n")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(display=broken), P)

    def test_unpaired_score_load_raises(self):
        broken = DISPLAY.replace("    ld hl, $C887\n    call PrintLineClearScores", "    nop")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(display=broken), P)

    def test_missing_line_goal_raises(self):
        broken = DISPLAY.replace("    ld a, $25           ; BCD encoded\n    ldh [hLines], a\n", "")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(display=broken), P)


class BonusLadderMustRaise(unittest.TestCase):
    def test_missing_inc_b_raises(self):
        """Dropping a rung's `inc b` truncates the ladder below three tiers."""
        broken = BONUS.replace("    jr nc, .bonusEnding\n    inc b\n    cp a, $15",
                               "    jr nc, .bonusEnding\n    cp a, $15", 1)
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(bonus=broken), P)

    def test_missing_top_byte_read_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(bonus=BONUS.replace("ld hl, wScore + 2", "ld hl, wScore")), P)

    def test_tier_comment_disagreement_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(bonus=BONUS.replace("; At least 150k", "; At least 250k")), P)

    def test_non_descending_thresholds_raise(self):
        broken = BONUS.replace("cp a, $15", "cp a, $25")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(bonus=broken), P)

    def test_rocket_sprite_not_a_spriteid_raises(self):
        """The rocket base must resolve to ROCKET_* SpriteIds; a base past the id space trips it."""
        broken = BONUS.replace("ld b, $58", "ld b, $70")  # $70..$72 are outside the SpriteId table
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(bonus=broken), P)


class ConstantSitesMustRaise(unittest.TestCase):
    def test_level_cap_without_ret_z_raises(self):
        broken = LEVEL_UP.replace("    cp a, $14\n    ret z\n", "    cp a, $14\n    ret nz\n")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(level_up=broken), P)

    def test_missing_soft_drop_operand_raises(self):
        broken = TALLY.replace("    ld de, 1            ;  And add one to the BCD encoded number of soft drops\n", "")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(tally=broken), P)

    def test_missing_saturation_tail_raises(self):
        broken = ADD_BCD.replace("    ldd [hl], a\n    ldd [hl], a\n    ld [hl], a\n    ret\n",
                                 "    ldd [hl], a\n    ld [hl], a\n    ret\n")
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(add_bcd=broken), P)

    def test_invalid_bcd_line_goal_raises(self):
        with self.assertRaises(SystemExit):
            ps.parse_scoring(_corpus(display=DISPLAY.replace("ld a, $25", "ld a, $2A")), P)


# --- Layer 2b: emit shape -----------------------------------------------------------------------

class EmitShape(unittest.TestCase):
    def setUp(self):
        self.data = ps.parse_scoring(_corpus(), P)

    def test_inc_holds_both_tables_and_the_constants(self):
        inc = ps.emit_inc(self.data, "abc1234")
        self.assertIn("{ .kind = LineClearKind::SINGLE, .points =   40 },", inc)
        self.assertIn("{ .kind = LineClearKind::TETRIS, .points = 1200 },", inc)
        self.assertIn("{ .min_score = 200000, .rocket_sprite = SpriteId::ROCKET_L },", inc)
        self.assertIn("{ .min_score = 100000, .rocket_sprite = SpriteId::ROCKET_S },", inc)
        self.assertIn("kLevelCap = 20;", inc)
        self.assertIn("kTypeBLineGoal = 25;", inc)
        self.assertIn("kSoftDropPointsPerRow = 1;", inc)
        self.assertIn("kScoreSaturation = 999999;", inc)
        self.assertNotIn("namespace kirpich {", inc)  # included mid-header; opens nothing
        self.assertTrue(inc.isascii())

    def test_inc_is_fully_decimal_and_typed(self):
        """The typed surface is decimal and enum-typed: no BCD wire value and no raw sprite byte
        leaks into the .inc. The rockets are SpriteId enumerators, so no hex remains at all."""
        inc = ps.emit_inc(self.data, "abc1234")
        self.assertNotIn("0x0040", inc)
        self.assertNotIn("0x1200", inc)
        self.assertIn(".rocket_sprite = SpriteId::ROCKET_L", inc)
        self.assertNotIn("0x", inc)

    def test_fixture_is_raw_bytes_with_no_port_type(self):
        fixture = ps.emit_fixture(self.data, "abc1234")
        self.assertIn("#pragma once", fixture)
        self.assertIn("kExpectedLineClearScoreBcd", fixture)
        self.assertIn("0x0040, 0x0100, 0x0300, 0x1200", fixture)
        self.assertIn("0x20, 0x15, 0x10", fixture)
        self.assertIn("0x58, 0x59, 0x5A", fixture)
        self.assertIn("kExpectedLevelCapByte = 0x14", fixture)
        self.assertIn("kExpectedTypeBLineGoalBcd = 0x25", fixture)
        self.assertIn("kExpectedSoftDropOperand = 0x0001", fixture)
        self.assertIn("kExpectedSaturationByte = 0x99", fixture)
        self.assertIn("namespace kirpich::fixtures", fixture)
        # Independent of the surface it guards: no include of it, no use of its types.
        self.assertNotIn('#include "data/scoring.h"', fixture)
        self.assertNotIn("LineClearKind", fixture)
        self.assertNotIn("LineClearScoreEntry", fixture)
        self.assertNotIn("BonusEndingEntry", fixture)
        self.assertTrue(fixture.isascii())


# --- Layer 3: end-to-end against the real disassembly -------------------------------------------

def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "tetris.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")

    def test_real_asm(self):
        text = (self.root / "tetris.asm").read_bytes().decode("utf-8")
        data = ps.parse_scoring(text, self.root / "tetris.asm")

        # Hand-traced against tetris.asm - the award site (:5005-5019), the scoreboard site
        # (:4887-4902), the display site (:4627-4636), the ladder (:4946-4956), the level cap
        # (:5834), the Type-B goal (:4656), the tally operand (:4861), the saturation (:190).
        self.assertEqual(data.score_bcd, (0x0040, 0x0100, 0x0300, 0x1200))
        self.assertEqual([ps._bcd_int(v, 4) for v in data.score_bcd], [40, 100, 300, 1200])
        self.assertEqual(data.threshold_bcd, (0x20, 0x15, 0x10))
        self.assertEqual(data.rocket_sprites, (0x58, 0x59, 0x5A))
        self.assertEqual(data.level_cap, 20)       # $14, plain binary
        self.assertEqual(data.type_b_goal_bcd, 0x25)
        self.assertEqual(data.soft_drop_operand, 1)
        self.assertEqual(data.saturation_byte, 0x99)


if __name__ == "__main__":
    unittest.main()
