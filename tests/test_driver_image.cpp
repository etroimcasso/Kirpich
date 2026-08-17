// The sound driver image: the span of the ROM the audio system places into the machine that runs
// the game's original sound engine.
//
// The image is one span — the audio section through the end of the ROM — because the driver reaches
// its own data by absolute address: MusicPointers holds raw addresses into the sequence region, and
// the SFX pointer tables hold raw addresses of driver routines. Song and effect data therefore
// cannot be separated from the code that reads it, and tests 2 and 4 below pin exactly that.
//
// Tests 3-7 read the real ROM. It is resolved from the CI provisioning path first, then the dev
// sibling; a machine with neither FAILS loudly - a missing ROM is a provisioning failure, never a
// skip. Nothing here writes into the real asset tree: extraction runs against a scratch root.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/asset_registry.h>

#include "assets/extract.h"
#include "assets/presence.h"
#include "data/music.h"
#include "data/sfx.h"

namespace {

namespace fs = std::filesystem;

using kirpich::kAudioSectionBase;
using kirpich::kMusicPointersAddr;
using kirpich::kMusicSectionBase;
using kirpich::kMusicSectionEnd;
using kirpich::kNoiseSfxContinuePointersAddr;
using kirpich::kNoiseSfxStartPointersAddr;
using kirpich::kNoteLengthRegionBase;
using kirpich::kNoteLengthRegionEnd;
using kirpich::kSquareSfxContinuePointersAddr;
using kirpich::kSquareSfxStartPointersAddr;
using kirpich::kStereoDataAddr;
using kirpich::assets::checkRequired;
using kirpich::assets::ExtractionResult;
using kirpich::assets::extractFromRom;
using kirpich::assets::kRomSize;
using kirpich::assets::kSoundDriverImageBase;
using kirpich::assets::kSoundDriverImageEnd;
using kirpich::assets::kSoundDriverImageSize;
using kirpich::assets::kSoundDriverInitEntry;
using kirpich::assets::kSoundDriverTickEntry;
using kirpich::assets::soundDriverImage;

// The real ROM, resolved the way CI is provisioned: the fixed per-platform path first, then the
// development sibling. Registers a test failure naming both candidates when neither exists.
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

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// Points the engine's asset root at a scratch directory for the duration of a test and restores
// whatever was there before, so extraction never touches the real asset tree.
class ScopedAssetRoot {
public:
    explicit ScopedAssetRoot(const fs::path& root) : previous_{retropp::assetRoot()} {
        retropp::setAssetRoot(root);
    }
    ~ScopedAssetRoot() { retropp::setAssetRoot(previous_); }

    ScopedAssetRoot(const ScopedAssetRoot&)            = delete;
    ScopedAssetRoot& operator=(const ScopedAssetRoot&) = delete;

private:
    fs::path previous_;
};

// A unique scratch directory that cleans itself up.
class TempRoot {
public:
    explicit TempRoot(const std::string& name)
        : path_{fs::temp_directory_path() / ("kirpich-driver-" + name)} {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }

    TempRoot(const TempRoot&)            = delete;
    TempRoot& operator=(const TempRoot&) = delete;

private:
    fs::path path_;
};

constexpr std::uint8_t kJumpOpcode = 0xC3;  // SM83 `jp nn`

// 1. The span is the audio section through the end of the ROM, and both entry points fall inside it.
TEST(DriverImage, SpanIsTheAudioSectionThroughEndOfRom) {
    EXPECT_EQ(kSoundDriverImageBase, kAudioSectionBase);
    EXPECT_EQ(kSoundDriverImageEnd, kRomSize);
    EXPECT_EQ(kSoundDriverImageSize, kSoundDriverImageEnd - kSoundDriverImageBase);
    EXPECT_EQ(kSoundDriverImageSize, 7040U);

    EXPECT_GE(kSoundDriverTickEntry, kSoundDriverImageBase);
    EXPECT_LT(kSoundDriverTickEntry, kSoundDriverImageEnd);
    EXPECT_GE(kSoundDriverInitEntry, kSoundDriverImageBase);
    EXPECT_LT(kSoundDriverInitEntry, kSoundDriverImageEnd);

    // The init trampoline follows the tick trampoline immediately - one three-byte jump apart.
    EXPECT_EQ(kSoundDriverInitEntry, kSoundDriverTickEntry + 3);
}

// 2. Every audio table the data layer pins lives inside the image. This is what makes one span
//    sufficient: the driver indexes these by absolute address, so none of them can be split off.
TEST(DriverImage, EveryAudioTableIsInsideTheImage) {
    const auto inside = [](std::size_t address) {
        return address >= kSoundDriverImageBase && address < kSoundDriverImageEnd;
    };

    // SFX side: the four pointer tables and the section base.
    EXPECT_TRUE(inside(kAudioSectionBase));
    EXPECT_TRUE(inside(kSquareSfxStartPointersAddr));
    EXPECT_TRUE(inside(kSquareSfxContinuePointersAddr));
    EXPECT_TRUE(inside(kNoiseSfxStartPointersAddr));
    EXPECT_TRUE(inside(kNoiseSfxContinuePointersAddr));

    // Music side: the pointer table, the stereo table, the note-length region, and every byte of
    // the sequence region (checked at both ends, the end being exclusive).
    EXPECT_TRUE(inside(kMusicPointersAddr));
    EXPECT_TRUE(inside(kStereoDataAddr));
    EXPECT_TRUE(inside(kNoteLengthRegionBase));
    EXPECT_TRUE(inside(kNoteLengthRegionEnd - 1));
    EXPECT_TRUE(inside(kMusicSectionBase));
    EXPECT_TRUE(inside(kMusicSectionEnd - 1));
}

// 3. The image is the ROM's own bytes at the span's offset - no copy, no transform.
TEST(DriverImage, ImageIsTheRomsOwnBytes) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), kRomSize);

    const std::span<const std::uint8_t> image = soundDriverImage(rom);
    ASSERT_EQ(image.size(), kSoundDriverImageSize);
    for (std::size_t i = 0; i < image.size(); ++i) {
        ASSERT_EQ(image[i], rom[kSoundDriverImageBase + i]) << "byte " << i << " of the image";
    }
}

// 4. Both entry points are jumps, and both target the audio section - the trampolines the audio
//    system names, verified rather than assumed.
TEST(DriverImage, EntryPointsAreJumpsIntoTheAudioSection) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), kRomSize);

    for (const std::size_t entry : {kSoundDriverTickEntry, kSoundDriverInitEntry}) {
        EXPECT_EQ(rom[entry], kJumpOpcode) << "entry at " << entry << " is not a jump";
        const std::size_t target =
            static_cast<std::size_t>(rom[entry + 1]) |
            (static_cast<std::size_t>(rom[entry + 2]) << 8);
        EXPECT_GE(target, kAudioSectionBase) << "jump at " << entry << " leaves the audio section";
        EXPECT_LT(target, kMusicSectionBase) << "jump at " << entry << " lands in music data";
    }

    // The two trampolines target different routines.
    const std::size_t tickTarget = static_cast<std::size_t>(rom[kSoundDriverTickEntry + 1]) |
                                   (static_cast<std::size_t>(rom[kSoundDriverTickEntry + 2]) << 8);
    const std::size_t initTarget = static_cast<std::size_t>(rom[kSoundDriverInitEntry + 1]) |
                                   (static_cast<std::size_t>(rom[kSoundDriverInitEntry + 2]) << 8);
    EXPECT_NE(tickTarget, initTarget);
}

// 5. Extraction writes the image where the presence check looks for it, byte for byte.
TEST(DriverImage, ExtractionWritesTheDriverImage) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const TempRoot        root{"extract"};
    const ScopedAssetRoot scoped{root.path()};

    const ExtractionResult result = extractFromRom(romPath);
    ASSERT_TRUE(result.succeeded) << result.message;

    const fs::path written = root.path() / "assets/audio/default/sound_driver.bin";
    ASSERT_TRUE(fs::exists(written)) << "the driver image was not written";

    const std::vector<std::uint8_t> onDisk = readBytes(written);
    ASSERT_EQ(onDisk.size(), kSoundDriverImageSize);

    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), kRomSize);
    for (std::size_t i = 0; i < onDisk.size(); ++i) {
        ASSERT_EQ(onDisk[i], rom[kSoundDriverImageBase + i]) << "byte " << i << " on disk";
    }
}

// 6. A ROM that is not the expected one writes nothing at all - the driver image is behind the same
//    identity gate as the graphics.
TEST(DriverImage, WrongAndTruncatedRomsWriteNothing) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), kRomSize);

    {
        const TempRoot        root{"wrong"};
        const ScopedAssetRoot scoped{root.path()};

        std::vector<std::uint8_t> tampered = rom;
        tampered[kSoundDriverImageBase] ^= 0xFF;  // right size, one byte flipped inside the driver
        const fs::path tamperedPath = root.path() / "tampered.gb";
        {
            std::ofstream out{tamperedPath, std::ios::binary};
            out.write(reinterpret_cast<const char*>(tampered.data()),
                      static_cast<std::streamsize>(tampered.size()));
        }

        const ExtractionResult result = extractFromRom(tamperedPath);
        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(fs::exists(root.path() / "assets/audio/default/sound_driver.bin"));
    }

    {
        const TempRoot        root{"truncated"};
        const ScopedAssetRoot scoped{root.path()};

        const fs::path shortPath = root.path() / "short.gb";
        {
            std::ofstream out{shortPath, std::ios::binary};
            out.write(reinterpret_cast<const char*>(rom.data()), 1000);
        }

        const ExtractionResult result = extractFromRom(shortPath);
        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(fs::exists(root.path() / "assets/audio/default/sound_driver.bin"));
    }
}

// 7. The presence check requires the driver image, so a install that predates it is sent back
//    through first start rather than reaching the audio system with nothing to place.
TEST(DriverImage, PresenceRequiresTheDriverImage) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const TempRoot        root{"presence"};
    const ScopedAssetRoot scoped{root.path()};

    ASSERT_TRUE(extractFromRom(romPath).succeeded);
    EXPECT_TRUE(checkRequired().complete()) << "a full extraction should satisfy the check";

    const fs::path written = root.path() / "assets/audio/default/sound_driver.bin";
    ASSERT_TRUE(fs::remove(written));

    const kirpich::assets::PresenceResult after = checkRequired();
    EXPECT_FALSE(after.complete());
    ASSERT_EQ(after.missing.size(), 1U);
    EXPECT_EQ(after.missing.front(), "assets/audio/default/sound_driver.bin");
}

}  // namespace
