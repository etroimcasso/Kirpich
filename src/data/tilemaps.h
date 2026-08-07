#pragma once

// The background tilemaps: the static screens the game draws, as grids of tile indices.
//
// Every full screen, banner, overlay, window message, tower column, and the congratulations strip is
// stored here as a composed grid - rows of raw tile indices, one index per background cell. Twenty-two
// maps in six shapes:
//
//   - Full screens (20 x kTilemapScreenRows): the two gameplay backdrops, the copyright, title,
//     config, and difficulty screens, and the two multiplayer screens.
//   - Banner strips (20 wide, a few rows tall): the two multiplayer victory banners and the Buran
//     launch backdrop.
//   - Field overlays (kPlayingFieldCols x kPlayingFieldRows): the scoreboard and the dancers screen,
//     written into the playing field's shape.
//   - Window blocks (kTilemapWindowCols wide): the pause message, the game-over frame, and the
//     "please try again" message.
//   - Tower columns (kTowerTilemapRows tall, one tile wide): the four sides of the Buran launch
//     towers, each a vertical strip.
//   - The congratulations strip: 16 tiles printed one at a time.
//
// Each grid is row-major: grid[row][col] is the tile at that screen cell, row 0 at the top. The
// congratulations strip and the tower columns are one-dimensional. The tables carry only the tile
// data; where each screen is drawn on the display, which cells the game later overwrites (level
// digits, player names, live scores, the tower umbilicals), and the pacing of the printed screens are
// consumer behavior, specified with source line anchors in docs/contracts/tilemaps.md.
//
// The four dimension constants and all 22 grids are generated from the disassembly by
// tools/asm_parser/parse_tilemaps.py; edit the source and regenerate, not here.

#include <array>
#include <cstdint>

#include "playing_field.h"

namespace kirpich {

#include "generated/tilemaps_data.inc"

}  // namespace kirpich
