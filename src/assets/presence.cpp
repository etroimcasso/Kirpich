#include "assets/presence.h"

#include <filesystem>
#include <string_view>

#include <retropp/asset_registry.h>

namespace kirpich::assets {
namespace {

// Records `logical` as missing when nothing exists for it under the asset root. Each call
// below writes its path as a literal at the call site — the path text never lives in a
// constant, so it stays visible to any textual scan and cannot be referenced from anywhere
// else. Resolution is a plain join against retropp::assetRoot(), the engine's public
// runtime base — the engine's prescribed route for names its literal-only path doors
// cannot take.
void checkOne(PresenceResult& result, std::string_view logical) {
    if (!std::filesystem::exists(retropp::assetRoot() / logical)) {
        result.missing.emplace_back(logical);
    }
}

}  // namespace

PresenceResult checkRequired() {
    PresenceResult result;
    checkOne(result, "assets/gfx/default/configandgameplay.png");
    checkOne(result, "assets/gfx/default/font.png");
    checkOne(result, "assets/gfx/default/copyrightandtitlescreen.png");
    checkOne(result, "assets/gfx/default/multiplayerandburan.png");
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
