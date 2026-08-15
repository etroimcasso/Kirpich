#pragma once

// The playing-field state: the board the game actually plays on, expressed as one plain C++ struct.
// Where EngineState (src/state/engine_state.h) holds the $C000 gameplay globals and the other state
// blocks hold the main-loop, sprite, serial, demo, and high-score bytes, PlayingFieldState holds the
// board itself: the 32x32 tile grid the original keeps at $C800 - the authoritative copy of the field
// that collision reads, piece locking writes, line clears scan, and garbage fills - plus the 10-byte
// staging row at $C400 that a multiplayer attack is built in before it is pushed under the opponent's
// stack.
//
// The grid is the whole 32x32 background-map shadow, not just the 18x10 visible field. The game touches
// well beyond the visible cells: the walls run the full height of the grid, the floor sits a row below
// the field, a multiplayer round parks incoming garbage in the rows beneath the floor, and startup
// clears the whole page. Modelling only the visible field would strand live cells, so the struct
// carries every row. board[row][col] is the cell at $C800 + row*32 + col; the visible field begins at
// column kPlayingFieldOriginCol (2), so fieldCell(fieldRow, fieldCol) reaches the same cell the field's
// own row/column names.
//
// Each cell is a raw tile index - the same tile space as the character map, the static tilemaps, and the
// garbage table; the empty cell is the character map's space (kGarbageEmptyTile / CharTile::SPACE). The
// struct is the single authoritative copy of the board: the original mirrors it into video RAM at a
// fixed +$3000 offset as it writes, but that mirroring is engine mechanism the render bridge owns, not
// state carried here.
//
// Every member is zero-initialised, so a default-constructed PlayingFieldState is the boot state (the
// original's startup zeroes $C000-$CFFF; the space-fill that makes the field playable, the walls, and
// the floor are title-screen setup, not boot - see the contract). reset() returns a live instance to
// boot. It is a sibling of the other state structs, not a member of any; aggregating the state blocks
// into the running game is later wiring. The board's behaviour - collision, locking, the line-clear scan
// and compaction, the wipe, the fills, the overlay streaming, and all the multiplayer attack machinery -
// is gameplay and presentation work built on this struct in later phases and specified, with source line
// anchors, in docs/contracts/playing-field-state.md.

#include <array>
#include <cstddef>
#include <cstdint>

#include "data/playing_field.h"

namespace kirpich {

// The board is the full background-map shadow the original keeps at $C800: a 32x32 tile grid (the
// $20-byte row stride over the $C800-$CC00 window, ($CC00 - $C800) / $20 = 32 rows of $20 cells). All of
// it is live - the visible 18x10 field is only its top-left interior.
inline constexpr std::size_t kBoardRows = 32;
inline constexpr std::size_t kBoardCols = 32;

// The visible field's left edge within the board: its top-left cell is board[0][kPlayingFieldOriginCol]
// ($C802). The field's own top row is board row 0. The floor row (18), the wall columns (1 and 12), and
// the other landmarks are closed-form relations recorded in the contract and pinned by the tests, not
// constants.
inline constexpr std::size_t kPlayingFieldOriginCol = 2;

// The brick tile a multiplayer attack row is built from before a hole is punched in it.
inline constexpr std::uint8_t kAttackRowBrickTile = 0x28;

struct PlayingFieldState {
    // The board: the full 32x32 background-map shadow, row-major. board[row][col] is the cell at
    // $C800 + row*kBoardCols + col; the top-left visible cell is board[0][kPlayingFieldOriginCol].
    std::array<std::array<std::uint8_t, kBoardCols>, kBoardRows> board{};

    // The multiplayer attack staging row ($C400): one field-width row of tiles, built full of bricks
    // with a single hole, then inserted under the opponent's stack. Ten cells, matching the field width.
    std::array<std::uint8_t, kPlayingFieldCols> attackRow{};

    // The board cell at a visible-field coordinate: field row 0..kPlayingFieldRows-1 from the top, field
    // column 0..kPlayingFieldCols-1 from the field's left edge. Equivalent to
    // board[row][kPlayingFieldOriginCol + col].
    std::uint8_t& fieldCell(std::size_t row, std::size_t col) {
        return board[row][kPlayingFieldOriginCol + col];
    }
    const std::uint8_t& fieldCell(std::size_t row, std::size_t col) const {
        return board[row][kPlayingFieldOriginCol + col];
    }

    // Return the board and the staging row to their boot (all-zero) value.
    void reset() { *this = PlayingFieldState{}; }

    friend bool operator==(const PlayingFieldState&, const PlayingFieldState&) = default;
};

}  // namespace kirpich
