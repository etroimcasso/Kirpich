#include "assets/presence.h"

#include <filesystem>

#include <retropp/asset_registry.h>

namespace kirpich::assets {

PresenceResult checkRequired() {
    PresenceResult result;
    for (const retropp::LiteralPath& logical : kRequired) {
        // assetPath performs the assetRoot join, so no base path is ever built by hand.
        if (!std::filesystem::exists(retropp::assetPath(logical))) {
            result.missing.emplace_back(logical.view());
        }
    }
    return result;
}

std::string missingAssetsMessage(const PresenceResult& result) {
    std::string message =
        "Kirpich needs the game's graphics, and they are not here yet.\n"
        "\n"
        "They are derived from the Game Boy Tetris ROM, so Kirpich never distributes them —\n"
        "they come from a copy of the game you already own.\n"
        "\n"
        "Missing:\n";

    for (const std::string& logical : result.missing) {
        message += "  ";
        message += logical;
        message += '\n';
    }

    message +=
        "\n"
        "Kirpich will now ask you to locate your ROM, and will read what it needs out of it.\n"
        "Your ROM is only read — it is not copied, moved, or altered.\n";

    return message;
}

}  // namespace kirpich::assets
