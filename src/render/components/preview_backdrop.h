#pragma once

// The backdrop behind a held palette preview: the plain backdrop with a brick wall down each edge of
// the screen.
//
// The bricks are the playfield's own left wall, read from the Type A playfield's tilemap row by row,
// so they repeat down each column exactly as they run beside the well in a round. They are background
// cells drawn through the ramp's content palette, which draws the brick's lightest shade as a color; an
// object palette would draw that shade see-through.
//
// The two walls are the one place a screen showing the preview is laid out by cell: they are columns
// kPreviewWallLeftColumn and kPreviewWallRightColumn of the backdrop. kPreviewWallLeftX and
// kPreviewWallRightX are their left edges in pixels, for the screens' fit checks - anything drawn over
// the preview stands at or after kPreviewWallLeftX + kPreviewBrickSide and ends at or before
// kPreviewWallRightX.

#include <cstddef>
#include <cstdint>

#include "render/background.h"  // kVisibleCols
#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

inline constexpr int         kPreviewBrickSide       = 8;
inline constexpr std::size_t kPreviewWallLeftColumn  = 0;
inline constexpr std::size_t kPreviewWallRightColumn = kVisibleCols - 1;
inline constexpr int kPreviewWallLeftX  = static_cast<int>(kPreviewWallLeftColumn) * kPreviewBrickSide;
inline constexpr int kPreviewWallRightX = static_cast<int>(kPreviewWallRightColumn) * kPreviewBrickSide;

// The whole backdrop, kVisibleCols by kVisibleRows cells, through `ramp`'s content palette.
[[nodiscard]] Cells PreviewBackdrop(const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
