#pragma once

// The background bridge: the board, drawn.
//
// The board (PlayingFieldState) is the port's model of the original's background map, and every
// screen the game shows is written into it - backdrops by the screen loader, blocks by piece
// locking, text by the game-over and scoreboard printers. So the picture is a pure function of
// simulation state, and this is that function: read the board's visible corner, resolve each cell's
// tile index against the regime, and hand the result to the engine as one tile layer.
//
// The visible corner is the top-left 20x18 of the 32x32 board, which is exactly the Game Boy's
// screen. The rest of the board is real and live - the floor row, the wall columns past the screen,
// the garbage a link-cable round parks below the floor - but it is off-screen, and the original does
// not scroll, so nothing else is ever shown.
//
// What this does NOT do: sprites (the falling piece, the preview, every cursor and the dancers are
// not here), and the original's palette register writes (the line-clear flash, the fades, the blank
// at a screen change). Both are named in docs/contracts/screen.md as visible differences.

#include <cstddef>
#include <vector>

#include <retropp/draw_state.h>  // DrawLayer, TileCell
#include <retropp/viewport.h>    // ViewportResolution

#include "render/tile_atlas.h"
#include "state/display_state.h"
#include "state/playing_field_state.h"

namespace kirpich::render {

// The visible window: the Game Boy's 20x18 cells of background, the board's top-left corner.
inline constexpr std::size_t kVisibleCols = 20;
inline constexpr std::size_t kVisibleRows = 18;
inline constexpr std::size_t kVisibleCells = kVisibleCols * kVisibleRows;

static_assert(kVisibleCols <= kBoardCols && kVisibleRows <= kBoardRows,
              "the visible window must fit inside the board it reads from");

// The layer's identity and depth. One background layer, drawn behind everything.
inline constexpr const char* kBackgroundLayerKey = "background";
inline constexpr std::int32_t kBackgroundLayerZ  = 0;

// Resolve the board's visible window into tile cells, row-major, 20 across and 18 down.
//
// Writes into `cells` rather than returning a fresh vector so a caller can keep one buffer for the
// whole run: the frame is rebuilt every time, and a per-frame allocation for a grid that never
// changes size is waste. `cells` is resized to kVisibleCells if it is not already.
void composeBackground(const PlayingFieldState& field, TileSheet sheet, const TileAtlas& atlas,
                       std::vector<retropp::TileCell>& cells);

// Wrap composed cells as the frame's background layer.
//
// The layer BORROWS the cells - the engine's tile content holds a span, valid only for the
// renderFrame call that consumes it - so the vector must outlive that call. Keep it alive across the
// frame; do not hand this a temporary.
[[nodiscard]] retropp::DrawLayer backgroundLayer(
    const std::vector<retropp::TileCell>& cells,
    retropp::ViewportResolution viewport = retropp::ViewportResolution::GameBoy);

}  // namespace kirpich::render
