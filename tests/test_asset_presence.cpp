#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/asset_registry.h>
#include <retropp/image.h>

#include "assets/presence.h"

namespace {

namespace fs = std::filesystem;

// The 8x8 2-bit greyscale PNG authored for this repository, derived from nothing. Pixel
// (x, y) has sample value (x + y) % 4, so all four Game Boy shade levels appear in every
// row. It stands in for the real graphics everywhere a test needs a decodable file: the
// real ones are ROM-derived, gitignored, and absent on CI, so no test may depend on them.
fs::path probeFixture() {
    return fs::path{KIRPICH_PROJECT_ROOT} / "tests" / "fixtures" / "tiny_probe.png";
}

// Points the engine's asset root at a scratch directory for the duration of a test and
// restores whatever was there before. The presence check resolves against assetRoot(), so
// this is what makes it testable without touching a real install.
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
        : path_{fs::temp_directory_path() / ("kirpich-assets-" + name)} {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }

    // Place the probe PNG at a logical asset path beneath this root, creating parents.
    // Callers write the path as a literal at the call site — the same no-path-constants
    // rule the shipped code follows, so the strings in this file stay greppable against
    // the ones in presence.cpp.
    void place(std::string_view logical) const {
        const fs::path destination = path_ / logical;
        fs::create_directories(destination.parent_path());
        fs::copy_file(probeFixture(), destination, fs::copy_options::overwrite_existing);
    }

    TempRoot(const TempRoot&)            = delete;
    TempRoot& operator=(const TempRoot&) = delete;

private:
    fs::path path_;
};

}  // namespace

// ── The presence check ───────────────────────────────────────────────────────────────

TEST(AssetPresence, ReportsNothingMissingWhenEveryRequiredAssetIsPresent) {
    const TempRoot root{"all-present"};
    root.place("assets/gfx/default/configandgameplay.png");
    root.place("assets/gfx/default/font.png");
    root.place("assets/gfx/default/copyrightandtitlescreen.png");
    root.place("assets/gfx/default/multiplayerandburan.png");
    const ScopedAssetRoot scoped{root.path()};

    const kirpich::assets::PresenceResult result = kirpich::assets::checkRequired();

    EXPECT_TRUE(result.complete());
    EXPECT_TRUE(result.missing.empty());
}

TEST(AssetPresence, NamesExactlyTheOneAssetThatIsAbsent) {
    // Everything present except the font — the odd one out in the real data too (1bpp in
    // the ROM where the others are 2bpp) and so the one most worth pinning.
    const TempRoot root{"one-absent"};
    root.place("assets/gfx/default/configandgameplay.png");
    root.place("assets/gfx/default/copyrightandtitlescreen.png");
    root.place("assets/gfx/default/multiplayerandburan.png");
    const ScopedAssetRoot scoped{root.path()};

    const kirpich::assets::PresenceResult result = kirpich::assets::checkRequired();

    EXPECT_FALSE(result.complete());
    EXPECT_EQ(result.missing, std::vector<std::string>{"assets/gfx/default/font.png"});
}

TEST(AssetPresence, NamesEveryAssetInOrderWhenTheRootIsEmpty) {
    const TempRoot        root{"all-absent"};
    const ScopedAssetRoot scoped{root.path()};

    const kirpich::assets::PresenceResult result = kirpich::assets::checkRequired();

    EXPECT_FALSE(result.complete());
    // The full required set, in reporting order. This assertion is the required set's home
    // in the test suite: adding a graphic in presence.cpp turns it red until the new path
    // is added here too, which is exactly the drift alarm it exists to be.
    EXPECT_EQ(result.missing, (std::vector<std::string>{
                                  "assets/gfx/default/configandgameplay.png",
                                  "assets/gfx/default/font.png",
                                  "assets/gfx/default/copyrightandtitlescreen.png",
                                  "assets/gfx/default/multiplayerandburan.png",
                              }));
}

TEST(AssetPresence, MissingMessageListsThePathsAndExplainsTheRomFlow) {
    const TempRoot        root{"message"};
    const ScopedAssetRoot scoped{root.path()};

    const kirpich::assets::PresenceResult result  = kirpich::assets::checkRequired();
    const std::string                     message = kirpich::assets::missingAssetsMessage(result);

    // Every missing path is named, so the player can see exactly what is absent.
    EXPECT_NE(message.find("assets/gfx/default/configandgameplay.png"), std::string::npos);
    EXPECT_NE(message.find("assets/gfx/default/font.png"), std::string::npos);
    EXPECT_NE(message.find("assets/gfx/default/copyrightandtitlescreen.png"), std::string::npos);
    EXPECT_NE(message.find("assets/gfx/default/multiplayerandburan.png"), std::string::npos);

    // And it explains that Kirpich is about to ask for the ROM, rather than sending the
    // player away to run a tool.
    EXPECT_NE(message.find("locate your ROM"), std::string::npos);
    EXPECT_NE(message.find("not copied, moved, or altered"), std::string::npos);
}

// ── The load path the assets travel through ──────────────────────────────────────────

TEST(AssetLoading, DecodesTheProbeFixtureToItsAuthoredPixels) {
    const retropp::LoadedImage image = retropp::loadPng(probeFixture());

    EXPECT_EQ(image.width, 8);
    EXPECT_EQ(image.height, 8);

    // Greyscale decodes as indexed: one raw sample value per pixel, unscaled.
    EXPECT_EQ(image.kind, retropp::ImageColorKind::Indexed);
    ASSERT_EQ(image.indices.size(), 64U);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            EXPECT_EQ(image.indices[static_cast<std::size_t>(y * 8 + x)],
                      static_cast<std::uint8_t>((x + y) % 4))
                << "at (" << x << ", " << y << ")";
        }
    }
}

TEST(AssetLoading, ThrowsRatherThanCrashingWhenTheFileIsNotThere) {
    const fs::path absent = fs::temp_directory_path() / "kirpich-no-such-asset.png";
    std::error_code ignored;
    fs::remove(absent, ignored);

    EXPECT_THROW((void)retropp::loadPng(absent), std::runtime_error);
}
