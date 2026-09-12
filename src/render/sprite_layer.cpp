#include "render/sprite_layer.h"

#include <map>
#include <span>
#include <string>

#include <retropp/viewport.h>

namespace kirpich::render {

namespace {

// What each layer was handed, under its own key. A layer's content is a span the renderer reads while
// it draws, so the sprites have to outlive the call that submits them; they are replaced, not added
// to, the next time that key is drawn.
Sprites& keptFor(std::string_view key) {
    static std::map<std::string, Sprites, std::less<>> store;
    const auto                                        it = store.find(key);
    return it != store.end() ? it->second : store.emplace(std::string{key}, Sprites{}).first->second;
}

}  // namespace

retropp::DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                               std::initializer_list<Sprites> children, LayerOptions options) {
    Sprites& kept = keptFor(key);
    kept.clear();
    for (const Sprites& child : children) {
        kept.insert(kept.end(), child.begin(), child.end());
    }

    return retropp::DrawLayer{
        .key           = retropp::ObjectKey{std::string{key}},
        .z             = z,
        .size          = retropp::ViewportResolution::GameBoy.size(),
        .scroll        = options.scroll,
        .alpha         = options.alpha,
        .blend         = options.blend,
        .content       = retropp::SpriteContent{std::span<const retropp::Sprite>(kept)},
        .effects       = std::move(options.effects),
        .regions       = std::move(options.regions),
        .transform     = options.transform,
        .transformEdge = options.transformEdge,
    };
}

}  // namespace kirpich::render
