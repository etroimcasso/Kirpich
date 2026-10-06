#pragma once

// One piece's shape, as sprites: the game's own piece art at its spawn orientation.
//
// The art is object art, placed by pixel and see-through at its lightest shade, so a shape draws over
// whatever is behind it and lines up wherever it is put rather than on the cell grid. It is named
// explicitly as the gameplay set, so it draws correctly whichever set the background is using.
//
// The parts are placed relative to the shape's own smallest offsets rather than to the composed
// origin a falling piece is drawn from. That origin sits two rows above the shape and, for the square,
// one cell to its left; placing from it would leave shapes laid out side by side hanging at different
// heights.

#include <cstdint>
#include <string_view>

#include <kirpich/piece_kind.h>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// How much room a shape takes, in viewport pixels, measured from its parts.
struct PieceShapeExtent {
    int width  = 0;
    int height = 0;

    friend constexpr bool operator==(const PieceShapeExtent&, const PieceShapeExtent&) = default;
};

[[nodiscard]] PieceShapeExtent pieceShapeExtent(kirpich::PieceKind kind) noexcept;

// The shape of `kind` with its top-left pixel at (x, y), at depth `z` in its layer. Each part is
// named `keyStem` followed by a hyphen and the part's index, so a shape keeps its identity between
// frames as long as its caller keeps the stem.
[[nodiscard]] Sprites PieceShape(kirpich::PieceKind kind, int x, int y, std::int32_t z,
                                 std::string_view keyStem, const TileAtlas& atlas,
                                 std::uint8_t ramp);

}  // namespace kirpich::render
