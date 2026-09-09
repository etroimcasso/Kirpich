#include "render/background_layer.h"

#include <map>
#include <span>
#include <string>

#include <retropp/viewport.h>

#include "render/background.h"  // kVisibleCols / kVisibleRows

namespace kirpich::render {

namespace {

// What each layer was handed, under its own key. A layer's content is a span the renderer reads while
// it draws, so the cells have to outlive the call that submits them; they are replaced, not added to,
// the next time that key is drawn.
Cells& keptFor(std::string_view key) {
    static std::map<std::string, Cells, std::less<>> store;
    const auto                                      it = store.find(key);
    return it != store.end() ? it->second : store.emplace(std::string{key}, Cells{}).first->second;
}

}  // namespace

retropp::DrawLayer BackgroundLayer(std::string_view key, std::int32_t z, Cells cells,
                                   LayerOptions options) {
    Cells& kept = keptFor(key);
    kept        = std::move(cells);

    return retropp::DrawLayer{
        .key     = retropp::ObjectKey{std::string{key}},
        .z       = z,
        .size    = retropp::ViewportResolution::GameBoy.size(),
        .scroll  = options.scroll,
        .alpha   = options.alpha,
        .blend   = options.blend,
        .content = retropp::TileContent{.widthInTiles  = static_cast<int>(kVisibleCols),
                                        .heightInTiles = static_cast<int>(kVisibleRows),
                                        .cells = std::span<const retropp::TileCell>(kept),
                                        .wrap  = retropp::TileWrap::Blank},
        .effects       = std::move(options.effects),
        .regions       = std::move(options.regions),
        .transform     = options.transform,
        .transformEdge = options.transformEdge,
    };
}

}  // namespace kirpich::render
