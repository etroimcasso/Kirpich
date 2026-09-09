#pragma once

// What a component returns.
//
// A component returns the primitives it is - sprites, or background cells. One that aggregates other
// components into a layer returns the layer, and a screen returns its layers. These are the names for
// those three, shared by every component the render layer has, so each one's signature reads the same
// way whichever screen it belongs to.

#include <vector>

#include <retropp/draw_state.h>  // DrawLayer, Sprite, TileCell

namespace kirpich::render {

using Sprites = std::vector<retropp::Sprite>;
using Cells   = std::vector<retropp::TileCell>;
using Regions = std::vector<retropp::Region>;
using Layers  = std::vector<retropp::DrawLayer>;

// Everything a layer carries beyond its name, its depth and what is on it.
//
// An aggregate decides the first three - that is what makes it the aggregate - and passes these
// through untouched, so a caller can fade a layer, tint it, confine an effect to a shape or move the
// whole thing without the aggregate knowing what any of it means. The defaults are the engine's own,
// so leaving it out draws exactly as it would have.
struct LayerOptions {
    retropp::LayerScroll                   scroll{};
    float                                  alpha = 1.0f;
    retropp::BlendMode                     blend = retropp::BlendMode::Normal;
    std::vector<retropp::ScreenSpaceEffect> effects{};
    std::vector<retropp::Region>            regions{};
    retropp::Transform                     transform{};
    retropp::DisplacementEdge transformEdge = retropp::DisplacementEdge::Blank;
};

}  // namespace kirpich::render
