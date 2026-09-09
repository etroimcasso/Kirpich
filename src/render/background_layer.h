#pragma once

// A tile layer, from the cells a component returned.
//
// It is the aggregate: hand it what a background component produced and it gives back the layer that
// draws it. The engine reads a layer's content as a span while it draws the frame, so the layer keeps
// what it was handed, under its own key, until it is handed a new set next frame.

#include <cstdint>
#include <string_view>

#include <retropp/draw_state.h>  // DrawLayer

#include "render/types.h"

namespace kirpich::render {

// `options` is everything a layer carries beyond its name, its depth and what is on it - a fade, a
// tint, a confined effect, a transform. It passes straight through; leaving it out draws exactly as
// the layer would have.
[[nodiscard]] retropp::DrawLayer BackgroundLayer(std::string_view key, std::int32_t z, Cells cells,
                                                 LayerOptions options = {});

}  // namespace kirpich::render
