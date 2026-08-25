#pragma once

// The rising floor: what makes a Type C round different from a Type A one.
//
// A count of drops sits on the panel. Every drop takes one off it and every row cleared puts one back,
// so the floor arrives in kTypeCRiseInterval drops if the player clears nothing and is held off only
// for as long as they keep clearing faster than that. When the count reaches zero the whole stack
// shifts up one row and a fresh garbage row arrives at the bottom of the field. Anything pushed past
// the top of the field is gone, and the round ends the way it always does - the next piece spawns into
// an occupied cell and the top-out count runs out.
//
// Three points in the frame, in the order they happen:
//
//   armRiseCounter    the round init, once, after the pipeline draws
//   recordLock        each lock, from the completed-row scan, with the rows it cleared
//   the rise itself   the next spawn point after the counter reaches zero
//
// The rise is deliberately not on a timer. It fires at a spawn point, which is the one moment in a
// round when no piece is in flight, so it can never move the field under a falling piece or invalidate
// a collision test mid-drop. It also means the pressure is per-piece rather than per-second: the
// gravity ramp already makes seconds scarcer as the level climbs, and the interval does not have to
// chase it.
//
// The new bottom row comes from the same procedural cell source a Type B round's starting garbage
// uses, and carries the same guarantee - at least one gap per row, so the row is always clearable.
// Register that source on the same VM as the piece randomizer (see src/vm/garbage_fill.h); the two
// share a divider, and splitting them across two machines throws the coupling away.
//
//     auto vm    = retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
//     auto fold  = kirpich::vm::registerGarbageFold(vm);
//     auto rise  = kirpich::systems::makeRiseFloorHook(fold);
//
// Leave the seam empty and no floor ever rises: every other mode passes nothing and is unaffected.

#include <cstdint>
#include <functional>

#include "systems/game_context.h"

namespace kirpich::systems {

// The count a round starts on. Flat across levels - the gravity ramp supplies the difficulty curve, so
// this stays constant and the round gets harder because the drops arrive faster, not because the floor
// does.
//
// Not a ceiling. A player who clears faster than they drop banks the difference and keeps it, which is
// what lets a good stretch pay for a bad one.
inline constexpr std::uint8_t kTypeCRiseInterval = 10;

// The largest count the panel can show, and therefore the largest the count is allowed to reach: the
// readout is two digits, and a bigger number would print as its last two. This is what the display can
// say, not a rule about how the mode plays - the economy keeps the count far below it anyway, since a
// drop always costs one and no piece can average more than a line.
inline constexpr std::uint8_t kRiseCountShown = 99;

// The seam the line-clear pipeline fires at each point a lock can spawn the next piece. An empty hook
// is skipped, which is what every non-Type-C build and every existing test gets.
using RiseFloorHook = std::function<void(GameContext&)>;

// Load the counter with a full interval, and put that number on the panel. The round init calls it
// after the pipeline draws, so the three pieces that fill the pipeline do not spend the player's first
// interval.
void armRiseCounter(GameContext& game);

// Settle the counter for a drop that has just locked, and follow it on the panel.
//
// Every drop costs one: left alone, the floor arrives in kTypeCRiseInterval drops. Each row the drop
// cleared is credited straight back, so a single line holds the count exactly where it was, a double
// gains one, and a tetris gains three - and the credit is kept, so a player clearing well builds a
// buffer to spend later rather than being capped at what they started with.
//
// That is the whole difficulty curve of the mode: clearing is not a reprieve from the floor, it is the
// only thing holding the floor off, and one line a drop is merely breaking even.
//
// Does nothing outside a Type C round, and stops at zero rather than wrapping: the count sits at zero
// from the drop that empties it until the spawn point that acts on it.
void recordLock(GameContext& game, std::uint8_t clearedRows);

// Shift the stack up one row and fill the bottom row from `fold`.
//
// Field row 0's contents are discarded: a block pushed past the top of the field is gone, and nothing
// records that it was ever there. Both grids are written - the board and the displayed map - because
// the rise appears at once rather than being carried in by a wipe. The walls, the floor, the rows
// below the field and the multiplayer attack row are all left alone.
void riseFloor(GameContext& game, const std::function<std::uint8_t()>& fold);

// Package the rise as the seam the line-clear pipeline takes: fires only in a Type C round whose
// counter has reached zero during normal gameplay, then reloads the counter and redraws the readout.
[[nodiscard]] RiseFloorHook makeRiseFloorHook(std::function<std::uint8_t()> fold);

}  // namespace kirpich::systems
