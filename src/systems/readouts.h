#pragma once

// The number readouts: the four values the stats panel shows beside the playing field, and the
// routine that draws all of them.
//
// The panel differs by game type. Type A shows a score, a level and a line count; Type B shows a
// level, the height of its starting garbage under the label HIGH, and the lines still to clear; Type C
// shows a score, a level, the pieces left until the floor rises under the label RISE, and a line
// count. Each backdrop leaves those cells blank and the functions here fill them.
//
// Most of them write both background maps. The second map is the paused screen, and it carries the
// panel too, so a readout that wrote only the live map would leave a stale number visible for as
// long as the game stayed paused. The exception is the line count, which reaches the second map only
// when the player actually pauses - see copyLinesToSecondMap.
//
// Cells are (row, column) in map space; row 0 is the top of the visible screen. See
// docs/contracts/readouts.md for the full table and its derivation.

#include <cstddef>
#include <cstdint>

#include "state/display_state.h"
#include "state/game_flow_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

// Draw the low `2 * digitPairs` decimal digits of `value` into `map`, left to right from
// (row, col), one digit per cell (tetris.asm:6624-6673).
//
// Leading zeros are drawn as spaces rather than zeros, and everything from the first nonzero digit
// on is drawn, zeros included. The final digit is always drawn, so a value of zero reads as a single
// `0` preceded by spaces rather than as a blank run.
//
// Digits above the printed width are dropped: the original reads a fixed number of packed-decimal
// bytes and this reads the matching number of digits, so both wrap the same way.
//
// Clears `flow.scorePrintFlag` on the way out, which is what the original's printer does and what
// makes a print suppress the *next* score draw. Every drawing routine here goes through this one, so
// printing the line count clears the score's request as surely as printing the score does.
void printNumber(BackgroundMap& map, GameFlowState& flow, std::size_t row, std::size_t col,
                 std::uint32_t value, std::uint8_t digitPairs);

// Draw the six-digit score into one map, if it should be drawn at all (tetris.asm:5814-5823).
//
// Draws nothing unless the game is in normal gameplay, the game type is Type A - the Type B panel
// has no score cells - and the score has changed since the last draw.
void printScore(GameContext& game, BackgroundMap& map);

// Draw the score into both maps, from the frame's last beat (tetris.asm:236-249).
//
// Draws nothing unless a redraw has been requested and the piece-lock process is at stage 3. Sets
// the print flag between the two maps so the second one is drawn as well, and clears the request
// when it is done.
void redrawScore(GameContext& game);

// Draw the level into the cell its game type uses, in both maps, with the heart glyph beside it in
// heart mode (tetris.asm:4162-4175). Type A draws at row 7, Type B at row 2.
void printLevel(GameContext& game);

// Redraw the level after it increases, in both maps (tetris.asm:5853-5870). Type A only, and the
// tens digit appears in the cell to the left once the level reaches ten.
void printLevelStep(GameContext& game);

// Draw the round's opening line count into the live map (tetris.asm:4183-4194). Type A draws a
// single `0`; Type B draws its two digits.
void printLinesSeed(GameContext& game);

// Redraw the line count into the live map after a clear (tetris.asm:5760-5771). Type A draws four
// digits from column 14, Type B two from column 16.
void printLines(GameContext& game);

// Draw the Type B starting garbage height into both maps (tetris.asm:4214-4218). One digit, and it
// does not change for the rest of the round.
void printStartHeight(GameContext& game);

// Draw the Type C rise countdown - the pieces still to lock before the floor comes up - into one map,
// two digits under the label RISE.
//
// Unlike its neighbours this one takes the map to write, because it is called at two different times:
// the round init draws it into both maps, and each later change draws only the live one. The paused
// screen therefore shows the count as it stood when the player paused, which is how the line count
// behaves as well.
void printRise(GameContext& game, BackgroundMap& map);

// Copy the four line-count digits from the live map into the second one (tetris.asm:4464-4476).
//
// This is the only write that puts a line count in the second map, so the paused screen shows the
// count as it was at the moment of pausing rather than as it was at the last clear.
void copyLinesToSecondMap(GameContext& game);

}  // namespace kirpich::systems
