#pragma once

// Screen loading: stamping a static backdrop into the board, and choosing the tile art it draws
// through. These are the two things every screen in the game does before it does anything else.
//
// Both write simulation state, which is why they live here and not in src/render/. The original's
// LoadTilemap copies a stored screen into the background map, and the board (PlayingFieldState) is
// the port's model of that map - the same 32x32 grid, the same cells, written by the same screens.
// Piece locking, line clears and garbage fills already write it; a backdrop load is one more writer,
// and once it has run the visible background is a pure function of the board. Choosing the art is
// simulation state for the same reason: nothing downstream can turn a tile index into a picture
// without knowing which set is loaded (see src/state/display_state.h).
//
// The board is the port's whole model of the background. The original keeps a second background map
// and pauses by switching to it; that map has no counterpart here, so the paused screen is not drawn
// - see docs/contracts/screen.md.

#include <array>
#include <cstdint>

#include "data/tilemaps.h"
#include "state/display_state.h"
#include "state/playing_field_state.h"

namespace kirpich::systems {

// A full-screen backdrop: the exact shape the nine stored screens share, and exactly the visible
// area of the Game Boy display.
using ScreenTilemap =
    std::array<std::array<std::uint8_t, kTilemapScreenCols>, kTilemapScreenRows>;

// Stamp a full-screen backdrop into the board (LoadTilemap.to9800, tetris.asm:6410-6431).
//
// Writes the tilemap's 18 rows of 20 cells into board[0..17][0..19], the board's top-left corner,
// which is the region the display shows. Every other cell is left alone: the columns past 19 and the
// rows past 17 hold the board's own off-screen content (the floor row, the garbage the multiplayer
// mode parks below it) and no backdrop reaches them.
//
// A backdrop overwrites whatever the board held in that region, including the playing field. That is
// what the original does too, and the stored screens are authored for it - a gameplay backdrop
// carries the field's walls in its own columns 1 and 12 and leaves columns 2 to 11 as spaces, so
// stamping it lays out an empty field rather than erasing one.
void loadScreenTilemap(PlayingFieldState& field, const ScreenTilemap& tilemap);

// Load a tile set (LoadCopyrightAndTitleScreenTiles :6394-6398 / LoadGameplayTiles :6368-6376).
//
// On hardware each of those routines copies art into the tile block; here the copy is the render
// bridge's business and the only lasting effect is which set is now current, so that is all this
// records. Call it wherever the original calls a loader, and with the set that loader loads.
void loadTileSheet(DisplayState& display, TileSheet sheet);

}  // namespace kirpich::systems
