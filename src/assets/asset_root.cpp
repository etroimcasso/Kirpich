#include "assets/asset_root.h"

#include <algorithm>

namespace kirpich::assets {

std::optional<std::filesystem::path> developmentAssetRoot(
    const std::filesystem::path& executableDir, const std::filesystem::path& projectRoot) {
    if (projectRoot.empty() || executableDir.empty()) {
        return std::nullopt;
    }

    // Walking components rather than comparing strings is what makes a sibling directory whose name
    // shares the project's prefix a miss: "/w/Kirpich-old" and "/w/Kirpich" differ at their last
    // component, where a string prefix test would call the first one inside the second.
    const auto [rootEnd, dirEnd] =
        std::mismatch(projectRoot.begin(), projectRoot.end(), executableDir.begin(),
                      executableDir.end());
    (void)dirEnd;
    if (rootEnd != projectRoot.end()) {
        return std::nullopt;
    }
    return projectRoot;
}

}  // namespace kirpich::assets
