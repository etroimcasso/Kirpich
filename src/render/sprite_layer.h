#pragma once

// A sprite layer, from the components declared as its children.
//
// It is the aggregate: name it, give it a depth, and list the components that go on it. Each child is
// the sprites a component returned, and the layer is what draws them together. The engine reads a
// layer's content as a span while it draws the frame, so the layer keeps its children, under its own
// key, until it is given new ones next frame.

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/types.h"

namespace kirpich::render {

// `options` is everything a layer carries beyond its name, its depth and what is on it - a fade, a
// tint, a confined effect, a transform. It passes straight through; leaving it out draws exactly as
// the layer would have.
[[nodiscard]] retropp::DrawLayer SpriteLayer(std::string_view key, std::int32_t z,
                                             std::initializer_list<Sprites> children,
                                             LayerOptions                   options = {});

}  // namespace kirpich::render
