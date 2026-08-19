#pragma once

// The background bridge: the displayed map, drawn.
//
// A background map is a 32x32 grid of tile indices, written by the backdrop loader, by piece locking,
// by the readouts, and a row at a time by the field wipe carrying the board across. There are two of
// them and DisplayState::displayedMap() names the one the hardware is showing - the second is the
// paused screen. This is the function that draws it: read the visible corner, resolve each cell's
// tile index against the live art regime, and hand the result to the engine as one tile layer.
//
// It is deliberately NOT the board. The board is the game's own copy of the playing field and is what
// collision and locking read; the map is what is on screen. The two hold different things whenever an
// effect lives in the gap between them - during a wipe, during the line-clear flash - which is the
// whole reason the port carries both.
//
// The visible corner is the top-left 20x18 of the map, which is exactly the Game Boy's screen. The
// rest is real but off-screen, and the original does not scroll, so nothing else is ever shown.
//
// What this does NOT do: sprites (see render/sprites.h), and the original's palette register writes
// (the fades and the blank at a screen change). Both are named in docs/contracts/screen.md.

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

// Resolve the displayed map's visible window into tile cells, row-major, 20 across and 18 down.
//
// Writes into `cells` rather than returning a fresh vector so a caller can keep one buffer for the
// whole run: the frame is rebuilt every time, and a per-frame allocation for a grid that never
// changes size is waste. `cells` is resized to kVisibleCells if it is not already.
void composeBackground(const DisplayState& display, const TileAtlas& atlas,
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
