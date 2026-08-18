#pragma once

// Whether a build's baked project root applies to the binary that is running.
//
// A development build bakes its project tree in, so a developer's binary reads the assets
// scripts/setup-dev-assets writes. The path is fixed at compile time, and a binary can be copied
// off the machine that built it — or just to another directory on the same one. Pointing a moved
// binary at its build tree is wrong twice over: it reads assets from somewhere the player will never
// look, and the extractor then WRITES there, populating a directory unrelated to where the program
// is running.
//
// The rule: the project tree belongs to the binary still sitting inside it. Anything else keeps the
// engine's default, which is the executable's own directory.

#include <filesystem>
#include <optional>

namespace kirpich::assets {

// The asset root to adopt for a binary whose executable lives in `executableDir`, given the project
// root baked in at build time — or nothing, meaning keep the engine's default.
//
// Answers the project root when `executableDir` is inside it (or is it), and nothing otherwise.
// Comparison is per path component, so a sibling directory whose name merely begins with the
// project's does not count as being inside it. Both paths are taken as given; a caller that wants
// symlinks and `..` resolved should normalise before calling.
[[nodiscard]] std::optional<std::filesystem::path> developmentAssetRoot(
    const std::filesystem::path& executableDir, const std::filesystem::path& projectRoot);

}  // namespace kirpich::assets
