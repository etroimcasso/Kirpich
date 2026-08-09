#pragma once

// Garbage: the rows a Type B game starts buried under, and the constants that describe them.
//
// A Type B game begins with the bottom of the field pre-filled with "garbage" - rows of blocks with
// gaps, which the player clears to win. Two things about that garbage live here:
//
//   - kTypeBDemoGarbage, the fixed 4 x kPlayingFieldCols table the attract-mode demo stamps into the
//     field. The demo cannot use random garbage (the recording has to line up with the replay), so
//     these exact rows are stored in the ROM. Each cell is a raw tile index: kGarbageEmptyTile for a
//     gap, or one of the eight block tiles (kGarbageBlockTileBase through
//     kGarbageBlockTileBase + kGarbageBlockTileCount - 1). Every row leaves at least one gap.
//   - The constants the procedural fill and its call sites use: how many rows a Type B start height is
//     worth (kTypeBGarbageRowsPerHeight), how many rows a multiplayer round starts with
//     (kMultiplayerRoundStartGarbageRows), the block-tile range, and the empty tile.
//
// The grid is row-major: kTypeBDemoGarbage[row][col] is the tile at that field cell, row 0 topmost.
//
// This unit is the data only. The procedural fill itself - the DIV-driven random pick, the
// guarantee of at least one gap per row, the buffer mirroring, and the loop that walks up the field -
// is timing- and RNG-dependent gameplay logic and ports with that code; where each start path writes,
// and the fill's mechanism, are specified with source line anchors in
// docs/contracts/garbage-init.md.
//
// The six constants and the table are generated from the disassembly by
// tools/asm_parser/parse_garbage.py; edit the source and regenerate, not here.

#include <array>
#include <cstdint>

#include "playing_field.h"

namespace kirpich {

#include "generated/garbage_data.inc"

}  // namespace kirpich
