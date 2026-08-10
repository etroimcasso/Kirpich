// SFX + residual sound-driver data: the three SFX ID spaces the game writes to its audio-state wire
// variables, and the mechanical-configuration data blobs (register images, envelope/frequency ramps,
// the note-pitch table, vibrato offsets, wave timbres, the noise-note table, pause-tune notes).
//
// The SFX sequences are driver code that rides the engine-hosted ROM image; the port carries the
// three enums plus a raw-byte fixture (tests/fixtures/sfx_expected.h) whose blob addresses are walked
// from the disassembly and whose bytes are transcribed from it. Tests 3-5 read the real ROM and
// confirm the walked address + transcribed bytes of every blob against the cartridge. Expectations
// come from docs/contracts/sfx.md.
//
// The ROM is resolved from the CI provisioning path first, then the dev sibling; a machine with
// neither FAILS loudly - a missing ROM is a provisioning failure, never a skip. Same idiom as
// tests/test_music.cpp.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "data/music.h"  // kMusicPointersAddr - the SFX tables tile onto it
#include "data/sfx.h"
#include "fixtures/sfx_expected.h"

namespace {

namespace fs = std::filesystem;

using kirpich::kAudioSectionBase;
using kirpich::kNoiseSfxCount;
using kirpich::kNoiseSfxStartPointersAddr;
using kirpich::kNoiseSfxContinuePointersAddr;
using kirpich::kMusicPointersAddr;
using kirpich::kSquareSfxCount;
using kirpich::kSquareSfxStartPointersAddr;
using kirpich::kSquareSfxContinuePointersAddr;
using kirpich::kWaveSfxCount;
using kirpich::NoiseSfxId;
using kirpich::SquareSfxId;
using kirpich::WaveSfxId;
using kirpich::fixtures::kExpectedSfxBlobs;
using kirpich::fixtures::kSfxBlobPool;
using kirpich::fixtures::SfxBlobExpected;

constexpr std::uint16_t kNoteLengthRegionBase = 0x6EF9;  // where the blob tail tiles (music.h span)

template <typename E>
constexpr auto id(E value) { return static_cast<std::uint8_t>(value); }

// Find a blob row by its audio.asm label.
const SfxBlobExpected& blob(std::string_view name) {
    const auto it = std::find_if(kExpectedSfxBlobs.begin(), kExpectedSfxBlobs.end(),
                                 [name](const auto& b) { return b.name == name; });
    EXPECT_NE(it, kExpectedSfxBlobs.end()) << "no blob named " << name;
    return *it;
}

// The blob's bytes as a view into the flat pool.
std::span<const std::uint8_t> bytesOf(const SfxBlobExpected& b) {
    return {kSfxBlobPool.data() + b.poolOffset, b.length};
}

fs::path requireRomPath() {
#ifdef _WIN32
    const fs::path ciPath{"C:\\ci-assets\\kirpich\\tetris.gb"};
#else
    const char*    home = std::getenv("HOME");
    const fs::path ciPath = (home != nullptr)
                                ? fs::path{home} / "ci-assets" / "kirpich" / "tetris.gb"
                                : fs::path{};
#endif
    if (!ciPath.empty() && fs::exists(ciPath)) {
        return ciPath;
    }
    const fs::path devPath =
        fs::path{KIRPICH_PROJECT_ROOT}.parent_path() / "rom" / "Tetris (World) (Rev 1).gb";
    if (fs::exists(devPath)) {
        return devPath;
    }
    ADD_FAILURE() << "The Tetris ROM is required and was not found. Provision it at\n  " <<
        (ciPath.empty() ? "(CI path unavailable: no HOME)" : ciPath.string()) << "\nor\n  " <<
        devPath.string();
    return {};
}

std::vector<std::uint8_t> readRom(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::uint16_t word(const std::vector<std::uint8_t>& rom, std::size_t at) {
    return static_cast<std::uint16_t>(rom[at] | (rom[at + 1] << 8));
}

// 1. The three ID spaces and the table/count constants are the wire values and origins the driver
//    dispatch defines.
TEST(Sfx, ConstantsAndEnums) {
    EXPECT_EQ(kAudioSectionBase, 0x6480);
    EXPECT_EQ(kSquareSfxStartPointersAddr, 0x6480);
    EXPECT_EQ(kSquareSfxContinuePointersAddr, 0x6490);
    EXPECT_EQ(kNoiseSfxStartPointersAddr, 0x64A0);
    EXPECT_EQ(kNoiseSfxContinuePointersAddr, 0x64A8);
    EXPECT_EQ(kSquareSfxCount, 8);
    EXPECT_EQ(kNoiseSfxCount, 4);
    EXPECT_EQ(kWaveSfxCount, 2);

    EXPECT_EQ(id(SquareSfxId::NONE), 0);
    EXPECT_EQ(id(SquareSfxId::TINK), 1);
    EXPECT_EQ(id(SquareSfxId::CHANGE_SCREEN), 2);
    EXPECT_EQ(id(SquareSfxId::ROTATE_PIECE), 3);
    EXPECT_EQ(id(SquareSfxId::SHIFT_PIECE), 4);
    EXPECT_EQ(id(SquareSfxId::GARBAGE_ATTACK), 5);
    EXPECT_EQ(id(SquareSfxId::LINE_CLEAR), 6);
    EXPECT_EQ(id(SquareSfxId::TETRIS), 7);
    EXPECT_EQ(id(SquareSfxId::LEVEL_UP), 8);

    EXPECT_EQ(id(NoiseSfxId::NONE), 0);
    EXPECT_EQ(id(NoiseSfxId::STACK_FALL), 1);
    EXPECT_EQ(id(NoiseSfxId::LOCK_PIECE), 2);
    EXPECT_EQ(id(NoiseSfxId::IGNITION), 3);
    EXPECT_EQ(id(NoiseSfxId::LIFTOFF), 4);

    EXPECT_EQ(id(WaveSfxId::NONE), 0);
    EXPECT_EQ(id(WaveSfxId::TETRIS_SWEEP), 1);
    EXPECT_EQ(id(WaveSfxId::GAME_OVER), 2);
}

// 2. Device-free fixture integrity: blobs are address-sorted, non-overlapping, inside the audio
//    section, their pool windows are consistent, and the tail tiles into the note-length region.
TEST(Sfx, FixtureIntegrity) {
    std::size_t poolSpan = 0;
    for (std::size_t i = 0; i < kExpectedSfxBlobs.size(); ++i) {
        const auto& b = kExpectedSfxBlobs[i];
        EXPECT_FALSE(b.name.empty()) << "blob " << i;
        EXPECT_GE(b.addr, kAudioSectionBase) << b.name;
        EXPECT_LE(b.addr + b.length, kNoteLengthRegionBase) << b.name;
        EXPECT_EQ(b.poolOffset, poolSpan) << b.name << " pool offset";
        poolSpan += b.length;
        if (i + 1 < kExpectedSfxBlobs.size()) {
            const auto& n = kExpectedSfxBlobs[i + 1];
            EXPECT_LT(b.addr, n.addr) << "blobs unsorted at " << b.name;
            EXPECT_LE(b.addr + b.length, n.addr) << "blobs overlap at " << b.name;
        }
    }
    EXPECT_EQ(poolSpan, kSfxBlobPool.size());

    const auto& last = kExpectedSfxBlobs.back();
    EXPECT_EQ(last.addr + last.length, kNoteLengthRegionBase) << "tail does not tile to note region";

    // The four SFX pointer tables precede MusicPointers: base + 0x30 == $64B0.
    EXPECT_EQ(kAudioSectionBase + 0x30, kMusicPointersAddr);
}

// 3. Pointer-table sweep against the ROM: the 24 words at $6480 point inside the audio section, and
//    the aliased continue-table slots are equal by value (same routine).
TEST(Sfx, PointerTablesMatchRom) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);

    std::array<std::uint16_t, 24> targets{};
    for (std::size_t i = 0; i < targets.size(); ++i) {
        targets[i] = word(rom, kAudioSectionBase + i * 2);
        EXPECT_GT(targets[i], kMusicPointersAddr) << "pointer " << i << " below the routines";
        EXPECT_LT(targets[i], kNoteLengthRegionBase) << "pointer " << i << " past the section";
    }

    // Square continue slots 2/4/5 (0-based 1/3/4) are all ContinueGenericSquareSFX.
    EXPECT_EQ(targets[8 + 1], targets[8 + 3]);
    EXPECT_EQ(targets[8 + 3], targets[8 + 4]);
    // Noise continue slots 1-3 (0-based 0/1/2) are all ContinueGenericNoiseSFX.
    EXPECT_EQ(targets[20 + 0], targets[20 + 1]);
    EXPECT_EQ(targets[20 + 1], targets[20 + 2]);
}

// 4. Full-corpus blob content against the ROM: every blob's ROM bytes at its walked address equal the
//    fixture pool bytes. This proves both the transcription and the instruction-size address walk.
TEST(Sfx, BlobBytesMatchRom) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);

    for (const auto& b : kExpectedSfxBlobs) {
        const std::span<const std::uint8_t> expected = bytesOf(b);
        for (std::size_t i = 0; i < b.length; ++i) {
            EXPECT_EQ(rom[b.addr + i], expected[i])
                << b.name << " byte " << i << " at 0x" << std::hex << (b.addr + i);
        }
    }

    // Corner pins straight from the disassembly.
    const auto& tink = blob("Data_659B");
    EXPECT_EQ(bytesOf(tink)[0], 0x00);
    EXPECT_EQ(bytesOf(tink)[4], 0xC7);
    const auto& liftVol = blob("LiftOffVolumeData");
    EXPECT_EQ(bytesOf(liftVol).front(), 0x70);
    EXPECT_EQ(bytesOf(liftVol).back(), 0x10);
}

// 5. Residual driver data pins: the note-pitch table's head/tail words, the noise-note table head,
//    and the wave-pattern tail that tiles into the note-length region.
TEST(Sfx, ResidualDriverData) {
    const auto& pitches = blob("NotePitches");
    EXPECT_EQ(pitches.length, 146);  // 73 words: $F00 placeholder + C2..B7
    const std::span<const std::uint8_t> p = bytesOf(pitches);
    EXPECT_EQ(p[0] | (p[1] << 8), 0x0F00);           // placeholder
    EXPECT_EQ(p[2] | (p[3] << 8), 0x002C);           // C2
    EXPECT_EQ(p[144] | (p[145] << 8), 0x07DF);       // B7

    const auto& noise = blob("Data_6E94");
    EXPECT_EQ(noise.length, 21);
    EXPECT_EQ(bytesOf(noise)[0], 0x00);

    // Every wave pattern is 16 bytes; DefaultWavePattern is last and ends at the note-length region.
    for (std::string_view name : {"WavePattern_6EA9", "WavePattern_6EB9", "WavePattern_6EC9",
                                  "GameOverWavePattern", "DefaultWavePattern"}) {
        EXPECT_EQ(blob(name).length, 16) << name;
    }
    const auto& dflt = blob("DefaultWavePattern");
    EXPECT_EQ(dflt.addr + dflt.length, kNoteLengthRegionBase);

    const auto& pause = blob(".pauseTuneFirstNoteData");
    EXPECT_EQ(pause.length, 4);
    EXPECT_EQ(bytesOf(pause)[0], 0xB2);
}

// 6. Quirk + cross-link pins: the dead wave pattern is still ported, the wave-SFX id domain is {1,2},
//    and the level-up arpeggio's frequency bytes are strictly increasing (A6, C#7, E7, A7).
TEST(Sfx, QuirksAndCrossLinks) {
    // WavePattern_6EB9 is dead data (referenced nowhere) but ported anyway.
    EXPECT_NE(std::find_if(kExpectedSfxBlobs.begin(), kExpectedSfxBlobs.end(),
                           [](const auto& b) { return b.name == "WavePattern_6EB9"; }),
              kExpectedSfxBlobs.end());

    EXPECT_EQ(kWaveSfxCount, 2);
    EXPECT_EQ(id(WaveSfxId::TETRIS_SWEEP), 1);
    EXPECT_EQ(id(WaveSfxId::GAME_OVER), 2);

    // LevelUpNote1..4 share their first three register bytes and rise in frequency (byte 3).
    std::array<std::uint8_t, 4> freqs{};
    const std::array<std::string_view, 4> notes{
        {"LevelUpNote1", "LevelUpNote2", "LevelUpNote3", "LevelUpNote4"}};
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const auto b = bytesOf(blob(notes[i]));
        EXPECT_EQ(b[0], 0x00) << notes[i];
        EXPECT_EQ(b[1], 0xB0) << notes[i];
        EXPECT_EQ(b[2], 0xF1) << notes[i];
        freqs[i] = b[3];
    }
    for (std::size_t i = 0; i + 1 < freqs.size(); ++i) {
        EXPECT_LT(freqs[i], freqs[i + 1]) << "level-up arpeggio not rising at " << i;
    }
}

}  // namespace
