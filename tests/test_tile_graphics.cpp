// Tile graphics: the four ROM blocks the extractor turns into the game's PNGs.
//
// The extraction table (src/data/tile_graphics.h) and the extractor (src/assets/extract.h) are
// swept in full against the parser-emitted fixture (tests/fixtures/tile_graphics_expected.h),
// which pins each block's table row, decoded dimensions, and a content hash — never the pixels,
// which are ROM-derived and uncommittable. Expectations come from docs/contracts/tile-graphics.md.
//
// Tests 3–6 read the real ROM. It is resolved from the CI provisioning path first, then the dev
// sibling; a machine with neither FAILS loudly — a missing ROM is a provisioning failure, never a
// skip.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/asset_registry.h>
#include <retropp/image.h>

#include "assets/extract.h"
#include "assets/png_writer.h"
#include "assets/presence.h"
#include "data/tile_graphics.h"
#include "fixtures/tile_graphics_expected.h"

namespace {

namespace fs = std::filesystem;

using kirpich::TileGraphic;
using kirpich::TileGraphicFormat;
using kirpich::bytesPerTile;
using kirpich::kTileGraphics;
using kirpich::assets::decodeTileGraphic;
using kirpich::assets::extractFromRom;
using kirpich::assets::ExtractionResult;
using kirpich::assets::sha1Hex;
using kirpich::assets::writeGreyscalePng;
using kirpich::fixtures::kExpectedTileGraphics;

// FNV-1a-64, implemented here independently of the parser that stamped the fixture hashes: the
// test recomputing the same five lines is the drift check, not a shared helper being equal to
// itself.
std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

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
// whatever was there before — the same pattern the presence tests use, because extraction writes
// through assetRoot() exactly as presence reads through it.
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
        : path_{fs::temp_directory_path() / ("kirpich-extract-" + name)} {
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

// 1. Full sweep: every extraction-table row matches its fixture row, field for field.
TEST(TileGraphics, TableMatchesFixture) {
    ASSERT_EQ(kTileGraphics.size(), kExpectedTileGraphics.size());
    for (std::size_t i = 0; i < kTileGraphics.size(); ++i) {
        const TileGraphic& row      = kTileGraphics[i];
        const auto&        expected = kExpectedTileGraphics[i];

        EXPECT_EQ(row.fileName, expected.fileName) << "fileName at row " << i;
        EXPECT_EQ(row.romOffset, expected.romOffset) << "romOffset at row " << i;
        EXPECT_EQ(row.tileCount, expected.tileCount) << "tileCount at row " << i;
        const std::uint8_t bitDepth = (row.format == TileGraphicFormat::OneBpp) ? 1 : 2;
        EXPECT_EQ(bitDepth, expected.bitDepth) << "format at row " << i;
    }
}

// 2. Full sweep: every block's geometry is the closed form — 16 tiles across, height rounded up
//    to whole tile rows — and its decode window stays inside the 32 KiB ROM.
TEST(TileGraphics, GeometryClosedForm) {
    for (std::size_t i = 0; i < kTileGraphics.size(); ++i) {
        const TileGraphic& row      = kTileGraphics[i];
        const auto&        expected = kExpectedTileGraphics[i];

        EXPECT_EQ(expected.pngWidth, 16 * 8) << "width at row " << i;
        EXPECT_EQ(expected.pngHeight, ((row.tileCount + 15) / 16) * 8) << "height at row " << i;

        const int windowEnd = row.romOffset + row.tileCount * bytesPerTile(row.format);
        EXPECT_LE(windowEnd, 32768) << "decode window escapes the ROM at row " << i;
    }
}

// 3. Full sweep against the real ROM: decoding every block reproduces the fixture's dimensions
//    and content hash.
TEST(TileGraphics, DecodeSweepMatchesFixtureHashes) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const std::vector<std::uint8_t> rom = readBytes(romPath);
    ASSERT_EQ(rom.size(), 32768U);

    for (std::size_t i = 0; i < kTileGraphics.size(); ++i) {
        const auto decoded = decodeTileGraphic(rom, kTileGraphics[i]);
        const auto& expected = kExpectedTileGraphics[i];

        EXPECT_EQ(decoded.width, expected.pngWidth) << "width at row " << i;
        EXPECT_EQ(decoded.height, expected.pngHeight) << "height at row " << i;
        EXPECT_EQ(fnv1a64(decoded.indices), expected.contentHash)
            << "content hash at row " << i << " (" << expected.fileName << ")";
    }
}

// 4. End to end: extraction into a fresh asset root succeeds, satisfies the presence check
//    exactly, and every written PNG decodes through the engine's own loader to the fixture's
//    dimensions and content hash — proving the player route and the dev route agree.
TEST(TileGraphics, ExtractorEndToEnd) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const TempRoot        root{"end-to-end"};
    const ScopedAssetRoot scoped{root.path()};

    const ExtractionResult result = extractFromRom(romPath);
    EXPECT_TRUE(result.succeeded) << result.message;
    EXPECT_FALSE(result.message.empty());

    EXPECT_TRUE(kirpich::assets::checkRequired().complete());

    // Each written file is read back through the engine's loader at its full path spelled as a
    // literal at the call site — the same no-path-constants rule the shipped code follows, so
    // these strings stay greppable against the ones in presence.cpp. The fixture row is looked
    // up by file name; test 1 pins that these four rows are the whole fixture.
    const auto expectExtracted = [&root](std::string_view logical, std::string_view fileName) {
        const auto row = std::find_if(
            kExpectedTileGraphics.begin(), kExpectedTileGraphics.end(),
            [&](const auto& candidate) { return fileName == candidate.fileName; });
        ASSERT_NE(row, kExpectedTileGraphics.end()) << fileName;

        const fs::path written = root.path() / logical;
        ASSERT_TRUE(fs::exists(written)) << written;

        const retropp::LoadedImage image = retropp::loadPng(written);
        EXPECT_EQ(image.kind, retropp::ImageColorKind::Indexed) << fileName;
        EXPECT_EQ(image.width, row->pngWidth) << fileName;
        EXPECT_EQ(image.height, row->pngHeight) << fileName;
        EXPECT_EQ(fnv1a64(image.indices), row->contentHash) << fileName;
    };
    expectExtracted("assets/gfx/default/font.png", "font.png");
    expectExtracted("assets/gfx/default/copyrightandtitlescreen.png",
                    "copyrightandtitlescreen.png");
    expectExtracted("assets/gfx/default/configandgameplay.png", "configandgameplay.png");
    expectExtracted("assets/gfx/default/multiplayerandburan.png", "multiplayerandburan.png");
}

// 5. A wrong ROM — right size, one byte flipped — is refused by the SHA-1 gate before anything
//    is written.
TEST(TileGraphics, WrongRomIsRefused) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const TempRoot root{"wrong-rom"};

    std::vector<std::uint8_t> tampered = readBytes(romPath);
    ASSERT_EQ(tampered.size(), 32768U);
    tampered[0x4000] ^= 0x01;
    const fs::path tamperedPath = root.path() / "tampered.gb";
    {
        std::ofstream out{tamperedPath, std::ios::binary};
        out.write(reinterpret_cast<const char*>(tampered.data()),
                  static_cast<std::streamsize>(tampered.size()));
    }

    const ScopedAssetRoot  scoped{root.path()};
    const ExtractionResult result = extractFromRom(tamperedPath);

    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.message.find("Nothing was written"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(root.path() / "assets"));
    EXPECT_FALSE(kirpich::assets::checkRequired().complete());
}

// 6. A truncated file is refused by the size gate before anything is written.
TEST(TileGraphics, TruncatedRomIsRefused) {
    const fs::path romPath = requireRomPath();
    if (romPath.empty()) {
        return;
    }
    const TempRoot root{"truncated-rom"};

    const std::vector<std::uint8_t> rom = readBytes(romPath);
    const fs::path                  shortPath = root.path() / "short.gb";
    {
        std::ofstream out{shortPath, std::ios::binary};
        out.write(reinterpret_cast<const char*>(rom.data()), 1000);
    }

    const ScopedAssetRoot  scoped{root.path()};
    const ExtractionResult result = extractFromRom(shortPath);

    EXPECT_FALSE(result.succeeded);
    EXPECT_NE(result.message.find("Nothing was written"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(root.path() / "assets"));
}

// 7. The save step round-trips: synthetic index grids at both bit depths encode and decode back
//    through the engine's loader to the identical samples.
//
//    Widths are byte-aligned only: the engine's sub-byte unpack reads rows at a byte-aligned
//    stride, so a width whose row ends mid-byte decodes shifted. Every real asset is 128 px wide
//    and unaffected. Once the engine reads sub-byte rows at their packed stride, partial-final-
//    byte cases (6x2 at depth 2, 7x3 at depth 1) belong here too.
TEST(TileGraphics, PngWriterRoundTrip) {
    struct Case {
        int width;
        int height;
        int bitDepth;
    };
    // 4 px at 2bpp and 8 px at 1bpp are each exactly one packed byte; the 128-wide rows are the
    // real assets' shape.
    const Case cases[] = {{4, 2, 2}, {8, 3, 1}, {128, 8, 2}, {128, 24, 1}};

    for (const Case& c : cases) {
        std::vector<std::uint8_t> samples(static_cast<std::size_t>(c.width) * c.height);
        const int maxVal = (1 << c.bitDepth) - 1;
        for (int y = 0; y < c.height; ++y) {
            for (int x = 0; x < c.width; ++x) {
                samples[static_cast<std::size_t>(y) * c.width + x] =
                    static_cast<std::uint8_t>((x + y) % (maxVal + 1));
            }
        }

        const std::vector<std::uint8_t> png =
            writeGreyscalePng(samples, c.width, c.height, c.bitDepth);
        const retropp::LoadedImage image = retropp::loadPngFromMemory(png);

        EXPECT_EQ(image.kind, retropp::ImageColorKind::Indexed);
        EXPECT_EQ(image.width, c.width) << c.width << "x" << c.height << "@" << c.bitDepth;
        EXPECT_EQ(image.height, c.height) << c.width << "x" << c.height << "@" << c.bitDepth;
        EXPECT_EQ(image.indices, samples) << c.width << "x" << c.height << "@" << c.bitDepth;
    }
}

// 8. The identity gate's SHA-1 against the FIPS 180 test vectors.
TEST(TileGraphics, Sha1MatchesKnownVectors) {
    const auto hashOf = [](std::string_view text) {
        return sha1Hex({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    };

    EXPECT_EQ(hashOf(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(hashOf("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    EXPECT_EQ(hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    const std::string million(1000000, 'a');
    EXPECT_EQ(hashOf(million), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

}  // namespace
