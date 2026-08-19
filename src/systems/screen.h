#pragma once

// Screen loading: stamping a static backdrop into the displayed map, and choosing the tile art it
// draws through. These are the two things every screen in the game does before it does anything else.
//
// Both write simulation state, which is why they live here and not in src/render/. A backdrop goes
// to the displayed map and not to the board (LoadTilemap writes $9800, tetris.asm:6410-6431): the
// board is the game's own copy of the playing field, and the screens that fill it - the title
// screen's space fill, walls and floor (:538-554), a field-shaped screen (:4621-4623) - write it
// separately. Choosing the art is simulation state for the same reason the map is: nothing downstream
// can turn a tile index into a picture without knowing which set is loaded (see
// src/state/display_state.h).
//
// The hardware keeps two background maps and displays one at a time; pausing switches to the second.
// Both are on DisplayState, and the overload below stamps a backdrop into whichever one is named -
// see docs/contracts/screen.md.

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

// Stamp a full-screen backdrop into the displayed map (LoadTilemap.to9800, tetris.asm:6410-6431).
//
// Writes the tilemap's 18 rows of 20 cells into map[0..17][0..19], the map's top-left corner, which
// is the region the display shows. Every other cell is left alone.
//
// It does not touch the board, so a backdrop cannot erase a live playing field - and it does not lay
// one out either. A gameplay backdrop draws the field's walls in its own columns 1 and 12 and leaves
// columns 2 to 11 as spaces, which is the picture of an empty field; the board behind it is filled by
// the round init.
void loadScreenTilemap(DisplayState& display, const ScreenTilemap& tilemap);

// The same stamp into a chosen map. The round init uses it to put the gameplay backdrop in the
// second map as well as the first (tetris.asm:4155-4157), which is what the paused screen shows.
void loadScreenTilemap(BackgroundMap& map, const ScreenTilemap& tilemap);

// Load a tile set (LoadCopyrightAndTitleScreenTiles :6394-6398 / LoadGameplayTiles :6368-6376).
//
// On hardware each of those routines copies art into the tile block; here the copy is the render
// bridge's business and the only lasting effect is which set is now current, so that is all this
// records. Call it wherever the original calls a loader, and with the set that loader loads.
void loadTileSheet(DisplayState& display, TileSheet sheet);

}  // namespace kirpich::systems
