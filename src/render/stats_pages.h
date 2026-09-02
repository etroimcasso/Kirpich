#pragma once

// The seven shapes on a statistics pieces page, drawn as sprites beside the counts the page writes
// into the map.
//
// They are sprites because a shape is the game's own piece art, which is object art: it is placed by
// pixel, it is several tiles wide, and its lightest shade is see-through. Writing its tiles into map
// cells would draw them through a background palette, which is opaque, and would tie a shape to the
// cell grid it is being shown beside rather than laid out on. The Type C rise values are drawn this
// way for the same reasons (render/type_c_difficulty.h).
//
// The art is named explicitly as the gameplay set. These screens run under the copyright-and-title
// regime the caller's screen was saved with, so a piece tile has to say which set it belongs to
// rather than taking whichever one happens to be selected.
//
// A shape is drawn at its spawn orientation, and its parts are placed relative to the shape's own
// smallest offsets rather than to the composed origin the renderer draws a falling piece from. That
// origin sits two rows above the shape and, for the square, one cell to its left; using it here would
// leave the seven hanging at different places on their lines.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // Sprite

#include <kirpich/game_state.h>

#include "render/tile_atlas.h"      // TileAtlas
#include "state/screen_ui_state.h"  // ScreenUiState

namespace kirpich::render {

// How far a shape sits from the corner of the cell its slot names, in viewport pixels. Both are here
// rather than buried in the drawing code because they are what gets nudged when the shapes sit a
// pixel off on a running build.
inline constexpr int kStatsShapeXOffset = 2;
inline constexpr int kStatsShapeYOffset = 0;

// Whether the statistics screen on display is showing a pieces page, and the shapes therefore have to
// be drawn. Every other state, and every other page, draws none.
[[nodiscard]] bool statsPieceShapesShown(kirpich::GameState             state,
                                         const kirpich::ScreenUiState& ui) noexcept;

// Where one shape's top-left pixel sits, by PieceKind index. A page under a picker starts its grid
// lower, which is the only difference between the two layouts.
struct ShapeOrigin {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(const ShapeOrigin&, const ShapeOrigin&) = default;
};

[[nodiscard]] ShapeOrigin statsPieceShapeOrigin(std::size_t kind, bool underPicker) noexcept;

// The seven shapes' sprites, for the page `ui` says is up. Append the result to the sprites the object
// buffer produced, before they are wrapped as the frame's sprite layer.
[[nodiscard]] std::vector<retropp::Sprite> statsPieceShapeSprites(const kirpich::ScreenUiState& ui,
                                                                  const TileAtlas&              atlas,
                                                                  std::uint8_t                  ramp);

}  // namespace kirpich::render
