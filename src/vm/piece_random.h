#pragma once

// The piece randomizer, split across the SM83 VM and native code.
//
// registerPieceRandom hosts the draw core (src/vm/random.asm) on `vm` and returns it as an ordinary
// callable that yields one folded candidate per call — a piece byte of the form kind * 4,
// orientation 0, so one of {0, 4, 8, 12, 16, 20, 24}. The fold runs on the machine so the divider
// (rDIV) advances across it; the piece distribution depends on that advancement
// (docs/contracts/piece-random.md).
//
// pickRandomPiece is the native selection around that draw: it folds up to three candidates,
// rejects one that repeats the recent piece, and advances the game-flow piece pipeline. It returns
// the piece to play next — the previous next-preview, a one-stage pipeline (see the contract).
//
// The caller advances the VM one tick's worth of cycles each sim tick
// (vm.advanceClock(timing.cpuCyclesPerTick())) so the divider free-runs between draws; without that
// the divider freezes and the draw degenerates into a counter.

#include <cstdint>

#include <kirpich/piece.h>

#include "retropp/vm.h"
#include "state/game_flow_state.h"

namespace kirpich::vm {

// Register the draw core on `vm` and return a callable byte source. The routine is baked into the
// binary at build time (Embed) — no ROM, no runtime file. Each call reads the divider and folds it;
// the returned byte is one of {0, 4, 8, 12, 16, 20, 24}.
[[nodiscard]] retropp::Routine<std::uint8_t()> registerPieceRandom(retropp::Vm& vm);

// Draw a piece via `draw` (up to three tries, rejecting a repeat of the temp-preview kind, with the
// third draw accepted unconditionally), commit the pipeline stage in `flow`, and return the piece
// to play next. Mutates flow.nextPreviewPiece (set to the accepted candidate) and
// flow.tempPreviewPiece (set to the old next-preview); returns the old next-preview.
// See docs/contracts/piece-random.md section 4a.
[[nodiscard]] Piece pickRandomPiece(const retropp::Routine<std::uint8_t()>& draw, GameFlowState& flow);

}  // namespace kirpich::vm
