#pragma once

// The line-clear pipeline: the sequence that runs once a piece locks — find the completed rows,
// flash them, drop the stack into the gaps, and redraw the field a row at a time until the next
// piece spawns. Five free functions on GameContext, the same shape as the piece system: they own no
// state of their own; everything they read or write lives on the GameContext passed by reference.
//
// The sequence spans two points in the frame. checkForCompletedRows and moveBlocksDownAfterLineClear
// run with the gameplay handlers; animateLineClear and playingFieldWipeTick run in the frame's
// vertical-blank pass, after the frame timers tick. That split is what makes the flash and wipe
// cadences exact — the timing law, with source line anchors, is in docs/contracts/line-clear.md.
//
// Two of the functions can spawn the next piece (a lock that cleared no rows, and the final wipe
// step), so they take the randomizer's byte source as `draw` and forward it to nextPiece — the same
// parameter the piece system uses.

#include <cstdint>
#include <functional>

#include "systems/game_context.h"

namespace kirpich::systems {

// Scan the field for completed rows on the frame a piece finishes locking. Records the completed rows
// (by field-row index, top to bottom) and their count, adjusts the line count for the current game
// type, bumps the matching per-kind clear stat, sets the rows-to-send-as-garbage count, and cues the
// lock and clear sounds. Advances the lock into its flash stage. Does nothing outside that one frame
// of the lock.
void checkForCompletedRows(GameContext& game);

// Run one frame of the flash-then-spawn step. While rows are flashing it advances the flash phase on
// its timed cadence and, after the last pass, hands off to the field wipe; the flash pixels
// themselves are a render effect. A lock that cleared no rows takes the shortcut here — it spawns the
// next piece and ends the lock. `draw` feeds that spawn.
void animateLineClear(GameContext& game, const std::function<std::uint8_t()>& draw);

// Compact the field once the flash finishes: drop every row above each cleared row down by one and
// empty the top row. Then clear the completed-rows list and advance the wipe to its first redraw
// step.
void moveBlocksDownAfterLineClear(GameContext& game);

// Empty the completed-rows list. Called at the end of a clear, and from the state-init paths that
// reset a game.
void clearLineClearsList(GameContext& game);

// Run one step of the field wipe. Each call advances the wipe by one row — the row redraw itself is a
// render effect owned by the presentation pass. Particular steps cue the stack-fall sound and, on the
// final step, spawn the next piece or finish a Type B round. `draw` feeds that spawn. Does nothing
// when no wipe is running.
void playingFieldWipeTick(GameContext& game, const std::function<std::uint8_t()>& draw);

}  // namespace kirpich::systems
