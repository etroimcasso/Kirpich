#include <cstdlib>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

#include <retropp/asset_registry.h>
#include <retropp/version.h>

#include "assets/first_start.h"
#include "engine.h"

namespace {

// Where LoadFromPath assets resolve from. A development build points this at the project
// tree so the files scripts/setup-dev-assets writes are the ones the engine reads; a
// distributable leaves the engine's own default, the executable's directory, which is
// where the extractor writes.
void configureAssetRoot() {
#ifdef KIRPICH_PROJECT_ROOT
    retropp::setAssetRoot(std::filesystem::path{KIRPICH_PROJECT_ROOT});
#endif
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    spdlog::info("kirpich 0.1.0 — Retro++ engine {}", retropp::version());

    configureAssetRoot();

    // Before anything is constructed: are the graphics here, and if not, get them. This
    // runs ahead of the engine because it has to — there is nothing to show a message on
    // yet, and nothing to load until it succeeds.
    const bool ready = kirpich::assets::ensureAssetsPresent(
        [](const std::string& text) { spdlog::warn("{}", text); });
    if (!ready) {
        return EXIT_FAILURE;
    }

    kirpich::Engine engine{};
    (void)engine;

    return EXIT_SUCCESS;
}
