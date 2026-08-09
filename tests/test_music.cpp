// Music data: the song/channel/section address graph the ROM's sound driver walks, plus the
// per-song StereoData rows and the note-length tables.
//
// The song sequences are the canonical copyrightable class and are never committed: sections are
// pinned by {address, length, SHA-1} in the parser-emitted fixture (tests/fixtures/music_expected.h),
// and tests 5-6 read the real ROM to prove the fixture graph and re-hash every section's bytes.
// StereoData and the note-length region are the two mechanical-config exceptions pinned as raw
// bytes. Expectations come from docs/contracts/music.md.
//
// Tests 3-6 read the real ROM, resolved from the CI provisioning path first, then the dev sibling;
// a machine with neither FAILS loudly - a missing ROM is a provisioning failure, never a skip. The
// ROM read at kStereoDataAddr is also what guards that hand-entered address (music.h): a wrong
// address makes the 68-byte block mismatch the fixture and fails test 3 loudly.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "assets/extract.h"  // kirpich::assets::sha1Hex - reused, not reinvented
#include "data/music.h"
#include "fixtures/music_expected.h"

namespace {

namespace fs = std::filesystem;

using kirpich::MusicId;
using kirpich::kMusicIdIndexMask;
using kirpich::kMusicPointersAddr;
using kirpich::kMusicSectionBase;
using kirpich::kMusicSectionEnd;
using kirpich::kMusicSongCount;
using kirpich::kNoteLengthRegionBase;
using kirpich::kNoteLengthRegionEnd;
using kirpich::kStereoDataAddr;
using kirpich::assets::sha1Hex;
using kirpich::fixtures::kExpectedMusicChannels;
using kirpich::fixtures::kExpectedMusicSections;
using kirpich::fixtures::kExpectedMusicSongs;
using kirpich::fixtures::kExpectedNoteLengthRegion;
using kirpich::fixtures::kExpectedStereoData;
using kirpich::fixtures::kMusicChannelSectionPool;
using kirpich::fixtures::MusicChannelTerm;

constexpr auto id(MusicId value) { return static_cast<std::uint8_t>(value); }

// The real ROM, resolved the way CI is provisioned: the fixed per-platform path first, then the
// development sibling. Registers a test failure naming both candidates when neither exists. Same
// idiom as tests/test_tile_graphics.cpp (the first ROM-reading unit).
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

// Words a channel spends on its terminator: a stop is one $0000, a repeat is $FFFF + a target.
std::size_t terminatorWords(MusicChannelTerm term) {
    switch (term) {
        case MusicChannelTerm::Stop:   return 1;
        case MusicChannelTerm::Repeat: return 2;
        case MusicChannelTerm::None:   return 0;
    }
    return 0;
}

// 1. Constants and MusicId are the wire values and spans the driver dispatch defines.
TEST(Music, ConstantsAndEnum) {
    EXPECT_EQ(kMusicPointersAddr, 0x64B0);
    EXPECT_EQ(kMusicSongCount, 17);
    EXPECT_EQ(kMusicIdIndexMask, 0x1F);
    EXPECT_EQ(kMusicSectionBase, 0x6F3F);
    EXPECT_EQ(kMusicSectionEnd, 0x7FC6);
    EXPECT_EQ(kStereoDataAddr, 0x6ABE);
    EXPECT_EQ(kNoteLengthRegionBase, 0x6EF9);
    EXPECT_EQ(kNoteLengthRegionEnd, 0x6F3F);

    EXPECT_EQ(id(MusicId::NONE), 0x00);
    EXPECT_EQ(id(MusicId::TOP_SCORE), 0x01);
    EXPECT_EQ(id(MusicId::TYPE_A), 0x05);
    EXPECT_EQ(id(MusicId::TYPE_B), 0x06);
    EXPECT_EQ(id(MusicId::TYPE_C), 0x07);
    EXPECT_EQ(id(MusicId::DANGER), 0x08);
    EXPECT_EQ(id(MusicId::TYPE_B_JINGLE_1), 0x0A);
    EXPECT_EQ(id(MusicId::TYPE_B_JINGLE_6), 0x0F);
    EXPECT_EQ(id(MusicId::ROCKET_LAUNCH), 0x10);
    EXPECT_EQ(id(MusicId::MULTIPLAYER_VICTORY), 0x11);
    EXPECT_EQ(id(MusicId::STOP), 0xFF);

    // `value & mask` is the 1-based MusicPointers index; the 17 songs stay in range.
    EXPECT_EQ(id(MusicId::TOP_SCORE) & kMusicIdIndexMask, 1);
    EXPECT_EQ(id(MusicId::MULTIPLAYER_VICTORY) & kMusicIdIndexMask, 17);
}

// 2. Device-free graph integrity: song references resolve, sections are sorted and non-overlapping,
//    and songs + channels + sections tile [kMusicSectionBase, kMusicSectionEnd) exactly.
TEST(Music, FixtureGraphTiles) {
    ASSERT_EQ(kExpectedMusicSongs.size(), kMusicSongCount);

    const auto sectionAt = [](std::uint16_t addr) {
        return std::find_if(kExpectedMusicSections.begin(), kExpectedMusicSections.end(),
                            [addr](const auto& s) { return s.addr == addr; });
    };
    const auto channelAt = [](std::uint16_t addr) {
        return std::find_if(kExpectedMusicChannels.begin(), kExpectedMusicChannels.end(),
                            [addr](const auto& c) { return c.addr == addr; });
    };

    // Song references resolve to channels; length tables sit in the note-length region.
    for (std::size_t i = 0; i < kExpectedMusicSongs.size(); ++i) {
        const auto& song = kExpectedMusicSongs[i];
        EXPECT_EQ(song.id, i + 1) << "song " << i;
        EXPECT_GE(song.lengthTableAddr, kNoteLengthRegionBase) << "song " << i;
        EXPECT_LT(song.lengthTableAddr, kNoteLengthRegionEnd) << "song " << i;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            if (song.channelAddrs[slot] != 0) {
                EXPECT_NE(channelAt(song.channelAddrs[slot]), kExpectedMusicChannels.end())
                    << "song " << i << " slot " << slot;
            }
        }
    }

    // Every channel's sections exist and its pool window is in bounds.
    for (const auto& ch : kExpectedMusicChannels) {
        ASSERT_LE(static_cast<std::size_t>(ch.poolOffset) + ch.sectionCount,
                  kMusicChannelSectionPool.size());
        for (std::size_t k = 0; k < ch.sectionCount; ++k) {
            const std::uint16_t s = kMusicChannelSectionPool[ch.poolOffset + k];
            EXPECT_NE(sectionAt(s), kExpectedMusicSections.end())
                << "channel 0x" << std::hex << ch.addr << " section 0x" << s;
        }
        if (ch.terminator != MusicChannelTerm::Repeat) {
            EXPECT_EQ(ch.repeatTargetAddr, 0) << "non-repeat channel carries a target";
        }
    }

    // Sections are address-sorted, in bounds, and non-overlapping.
    for (std::size_t i = 0; i < kExpectedMusicSections.size(); ++i) {
        const auto& s = kExpectedMusicSections[i];
        EXPECT_GE(s.addr, kMusicSectionBase) << "section " << i;
        EXPECT_LE(s.addr + s.length, kMusicSectionEnd) << "section " << i;
        if (i + 1 < kExpectedMusicSections.size()) {
            EXPECT_LT(s.addr, kExpectedMusicSections[i + 1].addr) << "sections unsorted at " << i;
            EXPECT_LE(s.addr + s.length, kExpectedMusicSections[i + 1].addr)
                << "sections overlap at " << i;
        }
    }

    // Tiling: collect every element's [start, end) and confirm a gapless cover of the span.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    for (const auto& song : kExpectedMusicSongs) {
        spans.emplace_back(song.headerAddr, song.headerAddr + 11);
    }
    for (const auto& ch : kExpectedMusicChannels) {
        const auto words = ch.sectionCount + terminatorWords(ch.terminator);
        spans.emplace_back(ch.addr, ch.addr + static_cast<std::uint32_t>(words) * 2);
    }
    for (const auto& s : kExpectedMusicSections) {
        spans.emplace_back(s.addr, s.addr + s.length);
    }
    std::sort(spans.begin(), spans.end());
    ASSERT_FALSE(spans.empty());
    EXPECT_EQ(spans.front().first, kMusicSectionBase);
    EXPECT_EQ(spans.back().second, kMusicSectionEnd);
    for (std::size_t i = 0; i + 1 < spans.size(); ++i) {
        EXPECT_EQ(spans[i].second, spans[i + 1].first)
            << "gap or overlap after 0x" << std::hex << spans[i].first;
    }
}

// 3. StereoData against the ROM at the hand-entered kStereoDataAddr: all 17 rows, mode in {1,3},
//    and the two corner pins. A wrong address makes the block mismatch and fails here.
TEST(Music, StereoDataMatchesRom) {
    ASSERT_EQ(kExpectedStereoData.size(), kMusicSongCount);
    EXPECT_EQ(kExpectedStereoData.front(),
              (std::array<std::uint8_t, 4>{{0x01, 0x24, 0xEF, 0x56}}));
    EXPECT_EQ(kExpectedStereoData.back(),
              (std::array<std::uint8_t, 4>{{0x01, 0x20, 0xEF, 0xF7}}));
    for (const auto& row : kExpectedStereoData) {
        EXPECT_TRUE(row[0] == 1 || row[0] == 3) << "mode byte " << int(row[0]);
    }

    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);
    for (std::size_t r = 0; r < kExpectedStereoData.size(); ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            EXPECT_EQ(rom[kStereoDataAddr + r * 4 + c], kExpectedStereoData[r][c])
                << "StereoData[" << r << "][" << c << "]";
        }
    }
}

// 4. The note-length region against the ROM: all 70 bytes and the Data_6EF9 head pins.
TEST(Music, NoteLengthRegionMatchesRom) {
    ASSERT_EQ(kExpectedNoteLengthRegion.size(),
              static_cast<std::size_t>(kNoteLengthRegionEnd - kNoteLengthRegionBase));
    const std::array<std::uint8_t, 6> head{{2, 4, 8, 16, 32, 64}};
    for (std::size_t i = 0; i < head.size(); ++i) {
        EXPECT_EQ(kExpectedNoteLengthRegion[i], head[i]) << "head " << i;
    }

    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);
    for (std::size_t i = 0; i < kExpectedNoteLengthRegion.size(); ++i) {
        EXPECT_EQ(rom[kNoteLengthRegionBase + i], kExpectedNoteLengthRegion[i]) << "byte " << i;
    }
}

// 5. ROM header/channel walk: re-derive every song header and channel list from ROM bytes at the
//    fixture addresses and assert they equal the fixture graph.
TEST(Music, RomHeaderAndChannelWalk) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);

    // MusicPointers: the 17 words at kMusicPointersAddr equal the song header addresses.
    for (std::size_t i = 0; i < kExpectedMusicSongs.size(); ++i) {
        EXPECT_EQ(word(rom, kMusicPointersAddr + i * 2), kExpectedMusicSongs[i].headerAddr)
            << "MusicPointers[" << i << "]";
    }

    // Song headers: 11 bytes each.
    for (const auto& song : kExpectedMusicSongs) {
        EXPECT_EQ(rom[song.headerAddr], song.byte0) << "song 0x" << std::hex << song.headerAddr;
        EXPECT_EQ(word(rom, song.headerAddr + 1), song.lengthTableAddr);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            EXPECT_EQ(word(rom, song.headerAddr + 3 + slot * 2), song.channelAddrs[slot]);
        }
    }

    // Channel section lists + terminators.
    for (const auto& ch : kExpectedMusicChannels) {
        std::size_t at = ch.addr;
        for (std::size_t k = 0; k < ch.sectionCount; ++k, at += 2) {
            EXPECT_EQ(word(rom, at), kMusicChannelSectionPool[ch.poolOffset + k])
                << "channel 0x" << std::hex << ch.addr << " section " << std::dec << k;
        }
        if (ch.terminator == MusicChannelTerm::Stop) {
            EXPECT_EQ(word(rom, at), 0x0000) << "stop channel 0x" << std::hex << ch.addr;
        } else if (ch.terminator == MusicChannelTerm::Repeat) {
            EXPECT_EQ(word(rom, at), 0xFFFF) << "repeat marker 0x" << std::hex << ch.addr;
            EXPECT_EQ(word(rom, at + 2), ch.repeatTargetAddr)
                << "repeat target 0x" << std::hex << ch.addr;
        }
    }
}

// 6. ROM section content + grammar: every section's ROM bytes hash to the fixture SHA-1, and every
//    byte is a legal driver command (note < $92, $01 rest, $00 end, $9D + 3 operands, $Ax length).
TEST(Music, RomSectionContentAndGrammar) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readRom(romPath);
    ASSERT_EQ(rom.size(), 32768U);

    for (const auto& s : kExpectedMusicSections) {
        const std::span<const std::uint8_t> bytes{rom.data() + s.addr, s.length};
        EXPECT_EQ(sha1Hex(bytes), s.sha1) << "section 0x" << std::hex << s.addr;

        for (std::size_t i = 0; i < s.length;) {
            const std::uint8_t b = bytes[i];
            if (b == 0x00 || b == 0x01) {
                i += 1;
            } else if (b == 0x9D) {
                ASSERT_LE(i + 4, s.length) << "section 0x" << std::hex << s.addr << " $9D operands";
                i += 4;
            } else if ((b & 0xF0) == 0xA0) {
                i += 1;
            } else {
                EXPECT_LT(b, 0x92) << "section 0x" << std::hex << s.addr << " illegal byte at " << i;
                i += 1;
            }
        }
    }
}

}  // namespace
