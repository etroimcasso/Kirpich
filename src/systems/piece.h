#pragma once

// The piece system: the per-frame manipulation of the active falling piece — spawning the next
// piece, dropping it by gravity or soft drop, rotating and shifting it, testing it against the board,
// and locking it into the board when it comes to rest. These are the free functions the gameplay and
// line-clear handlers call each frame; the piece system owns no state of its own — every field it
// reads or writes lives on the GameContext it takes by reference.
//
// Collision and locking both work through the active piece's on-screen cells. The original renders
// the piece into the sprite buffer and reads that buffer back; here activePieceCells computes the
// four board cells the piece covers directly from its slot position and its composed sprite, so no
// render step is needed. That geometry carries the original's renderer quirks verbatim (an 8-bit
// carry that leaks between the position adds, and the off-screen row a hidden piece maps to); the
// exact laws, with source line anchors, are in docs/contracts/piece-system.md.

#include <cstdint>
#include <functional>

#include "data/bounded_vec.h"
#include "systems/game_context.h"

namespace kirpich::systems {

// One board cell the active piece covers: its row and column in the board grid and the tile the
// piece would write there when it locks. Row/column index PlayingFieldState::board directly.
struct PieceCell {
    std::uint8_t row = 0;
    std::uint8_t col = 0;
    std::uint8_t tile = 0;

    friend constexpr bool operator==(const PieceCell&, const PieceCell&) = default;
};

// The four board cells the active piece currently covers, resolved from the active slot's position
// and composed sprite through the renderer's position law and the tile-lookup cell map. Collision and
// locking both read this. A piece sprite always composes to exactly four cells.
[[nodiscard]] BoundedVec<PieceCell, 4> activePieceCells(const GameContext& game);

// Whether the active piece overlaps a non-empty board cell in its current position. Any board byte
// other than an empty space collides.
[[nodiscard]] bool detectCollision(const GameContext& game);

// Rotate the active piece per this frame's rotation presses, then shift it left or right per the
// movement presses/holds with auto-repeat. Both stages cue their sound effect before testing for a
// collision and revert the move (and cancel the cue) if the new position collides.
void rotateAndShiftPiece(GameContext& game);

// Step the active piece down one row this frame — by gravity, or faster while soft drop is held —
// and, when the step collides, lock the piece in place: begin the lock, award soft-drop points, and
// end the game if the piece came to rest at the spawn position for the second time.
void dropPiece(GameContext& game);

// Write the resting piece's four tiles into the board and finish the lock. Does nothing unless a lock
// is in progress (drop began it); hides the active piece once the tiles are in the board.
void lockPieceIntoBackground(GameContext& game);

// Spawn the next piece: promote the preview into the active slot at the spawn position, then choose
// the new preview — from the shared piece list during a demo or a two-player game, otherwise by
// drawing from `draw` (the randomizer's byte source) with the game's repeat-rejection. Reloads the
// drop timer. `draw` yields one candidate piece byte per call (a piece kind, orientation zero); it is
// consulted only on the random path.
void nextPiece(GameContext& game, const std::function<std::uint8_t()>& draw);

}  // namespace kirpich::systems
