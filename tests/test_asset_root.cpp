// The development asset root's applies-to-me rule — behavioral tests against src/assets/asset_root.h.
//
// A development build bakes its project tree into the binary. The rule decides whether that baked
// path applies to the binary that is actually running, and the answer has to be no whenever the
// binary has moved: a copied build that keeps its build tree reads assets from a directory the
// player will never look in, and the extractor writes there too.
//
// This is the regression guard for a shipped build that asked for a ROM, extracted it into the
// continuous-integration runner's checkout on the same machine, and left the player's own directory
// empty. Device-free and filesystem-free: pure path logic.

#include <gtest/gtest.h>

#include <filesystem>

#include "assets/asset_root.h"

namespace {

namespace fs = std::filesystem;
using kirpich::assets::developmentAssetRoot;

}  // namespace

// ── Test 1: TheProjectTreeAppliesOnlyToTheBinaryInsideIt ────────────────────────────────────────────
TEST(AssetRoot, TheProjectTreeAppliesOnlyToTheBinaryInsideIt) {
    const fs::path project{"/home/dev/Tetris Port/cpp-port"};

    // A development build: the executable sits in the tree's own build directory.
    EXPECT_EQ(developmentAssetRoot(project / "build", project), project);

    // Nested deeper — a multi-configuration generator puts it under a configuration directory.
    EXPECT_EQ(developmentAssetRoot(project / "build" / "Release", project), project);

    // The degenerate case: the executable directory IS the project root.
    EXPECT_EQ(developmentAssetRoot(project, project), project);

    // Downloaded and run from elsewhere — the case that shipped broken. Keep the engine default.
    EXPECT_FALSE(developmentAssetRoot("/home/dev/Downloads", project).has_value());
    EXPECT_FALSE(developmentAssetRoot("/Applications/Kirpich", project).has_value());

    // A different machine's layout, where the build path does not exist at all.
    EXPECT_FALSE(developmentAssetRoot("/Users/player/Desktop", project).has_value());

    // The parent of the project tree is not inside it.
    EXPECT_FALSE(developmentAssetRoot("/home/dev/Tetris Port", project).has_value());
}

// ── Test 2: APrefixIsNotContainment ─────────────────────────────────────────────────────────────────
// The reason the rule walks path components instead of comparing strings. A sibling directory whose
// name begins with the project's would pass a string prefix test and take over its asset root.
TEST(AssetRoot, APrefixIsNotContainment) {
    const fs::path project{"/w/Kirpich"};

    EXPECT_FALSE(developmentAssetRoot("/w/Kirpich-old", project).has_value());
    EXPECT_FALSE(developmentAssetRoot("/w/Kirpich-old/build", project).has_value());
    EXPECT_FALSE(developmentAssetRoot("/w/KirpichBackup", project).has_value());

    // ...while the real thing still matches.
    EXPECT_EQ(developmentAssetRoot("/w/Kirpich/build", project), project);

    // Empty inputs answer no rather than matching everything: an empty path is a prefix of any path,
    // so a missing project root would otherwise capture every binary.
    EXPECT_FALSE(developmentAssetRoot("/w/Kirpich/build", {}).has_value());
    EXPECT_FALSE(developmentAssetRoot({}, project).has_value());
}
