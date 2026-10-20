#pragma once

// The seven pieces, as sprites, drawn through a palette so a player judging it sees the game's own
// art in it.
//
// Two rows - L J I O, then S Z T, in PieceKind order - each centered across the screen from the
// shapes' own widths with kPiecePreviewGap between neighbors. The rows stand at a fixed place, the
// same on every screen that draws them, so the shapes are where the player saw them last. Each shape
// is a PieceShape named for its kind, so the preview keeps its identity as the palette changes.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "render/sprites.h"  // kScreenHeightPx
#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

// The tops of the two rows, in viewport pixels.
inline constexpr int kPiecePreviewFirstRowY  = 80;
inline constexpr int kPiecePreviewSecondRowY = 104;

// The space between two neighboring shapes in a row, and the height of the tallest spawn shape.
inline constexpr int kPiecePreviewGap       = 8;
inline constexpr int kPiecePreviewShapeSpan = 16;

// How many kinds the first row holds; the rest stand on the second.
inline constexpr std::size_t kPiecePreviewFirstRowCount = 4;

// The shapes' depth in the layer they are drawn on.
inline constexpr std::int32_t kPiecePreviewZ = 10;

static_assert(kPiecePreviewSecondRowY >= kPiecePreviewFirstRowY + kPiecePreviewShapeSpan,
              "the preview's two rows overlap");
static_assert(kPiecePreviewSecondRowY + kPiecePreviewShapeSpan <= kScreenHeightPx,
              "the preview runs off the bottom of the screen");

// The seven shapes through `ramp`'s first object palette. Each part is named
// `<keyStem>-<kind>-<part>`, with `kind` the PieceKind's value, so two screens drawing the preview
// never ease one's shapes into the other's.
[[nodiscard]] Sprites PiecePreview(std::string_view keyStem, const TileAtlas& atlas,
                                   std::uint8_t ramp);

}  // namespace kirpich::render
