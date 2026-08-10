#!/usr/bin/env python3
"""Unit tests for parse_sfx.py.

Three layers per the parser test discipline:
  1. Helper units          - the byte/word readers, the SM83 instruction sizer, the blob reader, and
                             the address walk on crafted input.
  2. Synthetic edge cases  - malformed corpora that MUST raise (every structural assert has a raise
                             path): a checkpoint mismatch (wrong instruction size), blob overlap, a
                             blob-length mismatch, a wrong pointer-table target/size, a broken wave
                             dispatch, a referenced dead pattern, a tail that does not tile.
  3. End-to-end            - the real ../tetris/audio.asm: walked addresses, blob bytes, counts, the
                             pointer tables, the wave dispatch. Skips cleanly when the checkout is
                             absent.

Run from the project root:  python3 -m unittest tools.asm_parser.test_parse_sfx
Or on CI:                   python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'

Python 3 stdlib only.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parse_sfx as ps  # noqa: E402

P = Path("audio.asm")


# --- Layer 1: pure helpers ----------------------------------------------------------------------

class ParseByte(unittest.TestCase):
    def test_decimal(self):
        self.assertEqual(ps._parse_byte("36", P, 1), 36)

    def test_hex(self):
        self.assertEqual(ps._parse_byte("$C7", P, 1), 0xC7)

    def test_over_a_byte_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_byte("$100", P, 1)

    def test_garbage_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_byte("nope", P, 1)


class ParseWord(unittest.TestCase):
    def test_hex(self):
        self.assertEqual(ps._parse_word("$6E94", P, 1), 0x6E94)

    def test_non_dollar_raises(self):
        with self.assertRaises(SystemExit):
            ps._parse_word("6E94", P, 1)


class InstructionSize(unittest.TestCase):
    def size(self, body):
        return ps._instruction_size(body, P, 1)

    def test_data_directives(self):
        self.assertEqual(self.size("db $00, $B5, $D0, $40, $C7"), 5)
        self.assertEqual(self.size("dw $6F3F"), 2)
        self.assertEqual(self.size("dw StartTinkSFX, StartTetrisSFX"), 4)

    def test_one_byte_forms(self):
        for body in ("ret", "ret z", "xor a", "ld a, b", "ld l, e", "push af", "pop hl",
                     "jp hl", "inc e", "dec [hl]", "ldi a, [hl]", "ldh [c], a", "add b",
                     "add hl, bc", "and a", "cp [hl]", "ld [hl], a", "ld [de], a", "ld a, [de]",
                     "ld a, [hl]"):
            self.assertEqual(self.size(body), 1, body)

    def test_two_byte_forms(self):
        for body in ("ld a, 5", "ld a, $80", "ld c, LOW(rNR10)", "ld b, 4", "cp a, 1",
                     "cp a, $02", "and a, $1F", "or a, $80", "jr z, .label", "jr .label",
                     "ldh [rNR10], a", "ldh a, [rDIV]", "res 7, [hl]", "set 7, [hl]",
                     "bit 7, a", "sla a", "swap e", "srl a", "ld [hl], $01", "ld [hl], 0"):
            self.assertEqual(self.size(body), 2, body)

    def test_three_byte_forms(self):
        for body in ("ld hl, Data_659B", "ld hl, -1", "ld de, $DF80", "ld bc, $0410",
                     "ld a, [wPauseUnpauseSound]", "ld a, [$DF71]", "ld [wNewSquareSFXID], a",
                     "ld [$DFF5], a", "jp z, .label", "jp StartGameOverSFX", "call StartSFXCommon"):
            self.assertEqual(self.size(body), 3, body)

    def test_unsized_instruction_raises(self):
        with self.assertRaises(SystemExit):
            self.size("frobnicate a, b")


class MemAndImmediate(unittest.TestCase):
    def test_mem_addr(self):
        self.assertTrue(ps._is_mem_addr("[$DF71]"))
        self.assertTrue(ps._is_mem_addr("[wNewSquareSFXID]"))
        self.assertFalse(ps._is_mem_addr("[hl]"))
        self.assertFalse(ps._is_mem_addr("[c]"))
        self.assertFalse(ps._is_mem_addr("a"))

    def test_immediate(self):
        self.assertTrue(ps._is_immediate("$80"))
        self.assertTrue(ps._is_immediate("5"))
        self.assertTrue(ps._is_immediate("LOW(rNR10)"))
        self.assertTrue(ps._is_immediate("Data_659B"))
        self.assertFalse(ps._is_immediate("a"))
        self.assertFalse(ps._is_immediate("hl"))
        self.assertFalse(ps._is_immediate("[hl]"))


class ReadBlob(unittest.TestCase):
    def test_reads_db_run(self):
        lines = ["Data_659B::", "    db $00, $B5, $D0, $40, $C7", "StartTinkSFX::", "    ret"]
        self.assertEqual(ps._read_blob(lines, "Data_659B", P), [0x00, 0xB5, 0xD0, 0x40, 0xC7])

    def test_reads_dw_run_little_endian(self):
        lines = ["NotePitches::", "    dw $F00", "    dw $02C", "Data_6E94::", "    db $00"]
        self.assertEqual(ps._read_blob(lines, "NotePitches", P), [0x00, 0x0F, 0x2C, 0x00])

    def test_local_label(self):
        lines = ["_UpdateAudio::", "    ret", ".pauseTuneFirstNoteData",
                 "    db $B2, $E3, $83, $C7", "Next::"]
        self.assertEqual(ps._read_blob(lines, ".pauseTuneFirstNoteData", P),
                         [0xB2, 0xE3, 0x83, 0xC7])

    def test_missing_label_raises(self):
        with self.assertRaises(SystemExit):
            ps._read_blob(["Foo::", "    ret"], "Data_659B", P)


class WalkAddresses(unittest.TestCase):
    def setUp(self):
        # These exercise the SECTION/checkpoint/sizing mechanics in isolation; empty the blob
        # manifest so the "all blobs reachable" assert (checked on the real corpus) does not fire.
        orig = ps.BLOBS
        ps.BLOBS = []
        self.addCleanup(lambda: setattr(ps, "BLOBS", orig))

    def walk(self, lines):
        return ps.walk_addresses(lines, P)

    def test_checkpoint_passes(self):
        # ld a,5 (2 bytes) puts the next label at $6482, matching its encoded address.
        lines = ['SECTION "Audio", ROM0[$6480]', "Start::", "    ld a, 5", "Data_6482::", "    db $00"]
        self.walk(lines)  # no raise (Data_6482 has no blob entry, but the checkpoint still runs)

    def test_checkpoint_mismatch_raises(self):
        # The label claims $6483 but the walk reaches $6482 -> a size error would look exactly so.
        lines = ['SECTION "Audio", ROM0[$6480]', "Start::", "    ld a, 5", "Data_6483::", "    db $00"]
        with self.assertRaises(SystemExit):
            self.walk(lines)

    def test_section_reset(self):
        lines = ['SECTION "A", ROM0[$6480]', "    ret", 'SECTION "B", ROM0[$7FF0]', "Data_7FF0::",
                 "    db $00"]
        self.walk(lines)  # the second section resets the address; Data_7FF0 checkpoints

    def test_content_before_section_raises(self):
        with self.assertRaises(SystemExit):
            self.walk(["    ret"])


# --- Layer 2: synthetic corpora that MUST raise (and the valid baselines) -----------------------

def _pointer_tables(square_start=None, sizes=(8, 8, 4, 4)) -> list[str]:
    square_start = ps.SQUARE_START_TARGETS if square_start is None else square_start
    targets = [square_start, ps.SQUARE_CONTINUE_TARGETS, ps.NOISE_START_TARGETS,
               ps.NOISE_CONTINUE_TARGETS]
    names = ["SquareSFXStartPointers", "SquareSFXContinuePointers",
             "NoiseSFXStartPointers", "NoiseSFXContinuePointers"]
    out: list[str] = []
    for name, tgt, n in zip(names, targets, sizes):
        out.append(f"{name}::")
        out += [f"    dw {tgt[i] if i < len(tgt) else '$0000'}" for i in range(n)]
    return out


class PointerTables(unittest.TestCase):
    def test_valid(self):
        ps.assert_pointer_tables(_pointer_tables() + ["MusicPointers::"], P)

    def test_wrong_target_raises(self):
        bad = list(ps.SQUARE_START_TARGETS)
        bad[0] = "StartWrongSFX"
        with self.assertRaises(SystemExit):
            ps.assert_pointer_tables(_pointer_tables(square_start=bad) + ["MusicPointers::"], P)

    def test_wrong_size_raises(self):
        with self.assertRaises(SystemExit):
            ps.assert_pointer_tables(_pointer_tables(sizes=(7, 8, 4, 4)) + ["MusicPointers::"], P)


class WaveDispatch(unittest.TestCase):
    VALID = ["PlayWaveSFX::", "    ld a, [wNewWaveSFXID]", "    cp a, 1",
             "    jp z, StartTetrisSweepSFX", "    cp a, 2", "    jp z, StartGameOverSFX",
             "    ret", "NextRoutine::"]

    def test_valid(self):
        ps.assert_wave_dispatch(self.VALID, P)

    def test_wrong_target_raises(self):
        bad = [ln.replace("StartGameOverSFX", "StartSomethingSFX") for ln in self.VALID]
        with self.assertRaises(SystemExit):
            ps.assert_wave_dispatch(bad, P)


class DeadWavePattern(unittest.TestCase):
    def test_valid_when_only_defined(self):
        ps.assert_dead_wave_pattern(["WavePattern_6EB9::", "    db $01"], P)  # no raise

    def test_referenced_raises(self):
        with self.assertRaises(SystemExit):
            ps.assert_dead_wave_pattern(
                ["WavePattern_6EB9::", "    db $01", "    ld hl, WavePattern_6EB9"], P)


class CollectBlobs(unittest.TestCase):
    def _patch_blobs(self, manifest):
        orig = ps.BLOBS
        ps.BLOBS = manifest
        self.addCleanup(lambda: setattr(ps, "BLOBS", orig))

    def test_length_mismatch_raises(self):
        self._patch_blobs([("Data_A", 5)])  # expects 5, source has 4
        lines = ["Data_A::", "    db $00, $00, $00, $00"]
        with self.assertRaises(SystemExit):
            ps.collect_blobs(lines, {"Data_A": 0x6480}, P)

    def test_overlap_raises(self):
        self._patch_blobs([("Data_A", 4), ("Data_B", 4)])
        lines = ["Data_A::", "    db $00, $00, $00, $00", "Data_B::", "    db $00, $00, $00, $00"]
        with self.assertRaises(SystemExit):  # 0x6480 + 4 > 0x6481
            ps.collect_blobs(lines, {"Data_A": 0x6480, "Data_B": 0x6481}, P)

    def test_tail_not_tiling_raises(self):
        self._patch_blobs([("Data_A", 1)])  # ends at 0x6481, not the note-length region
        lines = ["Data_A::", "    db $00"]
        with self.assertRaises(SystemExit):
            ps.collect_blobs(lines, {"Data_A": 0x6480}, P)


# --- Layer 3: end-to-end against the real disassembly -------------------------------------------

def _find_tetris_root() -> Path | None:
    project_root = Path(__file__).resolve().parents[2]
    candidates = [
        Path(os.environ["TETRIS_SRC"]) if os.environ.get("TETRIS_SRC") else None,
        project_root.parent / "tetris",  # local dev sibling checkout
        project_root / "tetris",         # CI submodule path
    ]
    for candidate in candidates:
        if candidate and (candidate / "audio.asm").is_file():
            return candidate
    return None


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.root = _find_tetris_root()
        if self.root is None:
            self.skipTest("tetris disassembly checkout not found (unit-only run)")
        self.audio = self.root / "audio.asm"
        self.lines = self.audio.read_bytes().decode("utf-8").splitlines()

    def test_pointer_tables(self):
        ps.assert_pointer_tables(self.lines, self.audio)  # no raise

    def test_wave_dispatch(self):
        ps.assert_wave_dispatch(self.lines, self.audio)  # no raise

    def test_dead_wave_pattern(self):
        ps.assert_dead_wave_pattern(self.lines, self.audio)  # no raise

    def test_walk_addresses(self):
        addrs = ps.walk_addresses(self.lines, self.audio)
        # Self-encoding labels resolve to their names; code-embedded ones to the walked address.
        self.assertEqual(addrs["Data_659B"], 0x659B)
        self.assertEqual(addrs["Data_6E94"], 0x6E94)
        self.assertEqual(addrs["WavePattern_6EA9"], 0x6EA9)
        self.assertEqual(addrs[".pauseTuneFirstNoteData"], 0x657B)
        self.assertEqual(addrs["LevelUpNote1"], 0x6640)
        self.assertEqual(addrs["NotePitches"], 0x6E02)
        self.assertEqual(addrs["DefaultWavePattern"], 0x6EE9)

    def test_collect_blobs(self):
        addrs = ps.walk_addresses(self.lines, self.audio)
        blobs = ps.collect_blobs(self.lines, addrs, self.audio)
        self.assertEqual(len(blobs), len(ps.BLOBS))
        by_name = {name: (addr, data) for name, addr, data in blobs}
        self.assertEqual(by_name["Data_659B"], (0x659B, [0x00, 0xB5, 0xD0, 0x40, 0xC7]))
        self.assertEqual(by_name["LiftOffVolumeData"][1][0], 0x70)
        self.assertEqual(by_name["LiftOffVolumeData"][1][-1], 0x10)
        self.assertEqual(len(by_name["NotePitches"][1]), 146)
        self.assertEqual(by_name["NotePitches"][1][:4], [0x00, 0x0F, 0x2C, 0x00])
        # The tail tiles into the note-length region.
        last_addr, last_data = by_name["DefaultWavePattern"]
        self.assertEqual(last_addr + len(last_data), ps.NOTE_LENGTH_REGION_BASE)

    def test_emit_shapes(self):
        addrs = ps.walk_addresses(self.lines, self.audio)
        blobs = ps.collect_blobs(self.lines, addrs, self.audio)
        header = ps.emit_header("abc1234")
        self.assertIn("enum class SquareSfxId : std::uint8_t", header)
        self.assertIn("enum class NoiseSfxId : std::uint8_t", header)
        self.assertIn("enum class WaveSfxId : std::uint8_t", header)
        self.assertIn("kAudioSectionBase            = 0x6480", header)
        self.assertTrue(header.isascii())
        fixture = ps.emit_fixture(blobs, "abc1234")
        self.assertIn("struct SfxBlobExpected", fixture)
        self.assertIn('.name = "Data_659B"', fixture)
        self.assertTrue(fixture.isascii())


if __name__ == "__main__":
    unittest.main()
