#pragma once

// The starting garbage: the rows a Type B round begins buried under, and the same fill a multiplayer
// round start uses.
//
// A field gets its garbage one of two ways. `registerGarbageFold` hosts the per-cell pick
// (src/vm/garbage.asm) on `vm` and returns it as an ordinary callable that answers one cell per call —
// either the empty tile or one of the eight block tiles. `initGarbage` walks the field with it,
// filling from a top row down to the bottom of the field and guaranteeing at least one gap per row so
// the field is always clearable. `initDemoGarbage` does not use the machine at all: an attract-mode
// demo replays recorded input, so its garbage has to be identical every time and comes from a fixed
// table.
//
// `makeInitGarbageHook` packages the Type B geometry as the hook the round init takes. Install it and
// a Type B round starts buried; leave it out and the round starts on an empty field.
//
//     auto vm   = retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
//     auto fold = kirpich::vm::registerGarbageFold(vm);
//     wiring.initGarbage = kirpich::vm::makeInitGarbageHook(fold);
//
// The pick runs on the machine because the divider (rDIV) advances across it, and which of the eight
// block tiles a cell becomes depends on that advancement. Advance the VM one tick's worth of cycles
// per sim tick, as the piece randomizer's caller does, so the divider free-runs between fills.
//
// Register this on the SAME Vm as the piece randomizer (vm/piece_random.h). There is one divider, and
// a round init draws pieces and then fills garbage in the same frame, so the draws advance the divider
// the fill goes on to read. Routines registered on one Vm share its machine, which reproduces that
// coupling; giving each routine its own Vm gives each its own divider and throws the coupling away.
// Nothing in the types enforces this — it is a wiring requirement.
//
//     auto vm   = retropp::Vm(retropp::VMPlatform::GameBoy, retropp::TimingProfile::GameBoy);
//     auto draw = kirpich::vm::registerPieceRandom(vm);   // same vm
//     auto fold = kirpich::vm::registerGarbageFold(vm);   // same vm
//
// Two things to know about the extent. The fill always runs to the bottom of the field — the row
// count a caller supplies chooses where it *starts*, not how many rows it writes — so a multiplayer
// round start, whose count is six, covers ten rows. And the fill writes only the board; mirroring the
// board into video memory belongs to the render bridge.
//
// The exact per-cell mechanism, the row extents per start path, and the one property the port does not
// reproduce (the divider does not advance across the native work between cells, so the field is not
// the one the original hardware would produce from the same starting divider) are in
// docs/contracts/garbage-init.md.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "data/garbage.h"
#include "data/playing_field.h"
#include "retropp/vm.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"

namespace kirpich::vm {

// The field row a Type B start of `height` reaches up to; the garbage covers that row down to the
// bottom of the field. Height 1 covers the bottom kTypeBGarbageRowsPerHeight rows and each further
// level adds that many more. A height large enough to bury the whole field answers row 0.
[[nodiscard]] constexpr std::size_t typeBGarbageTopRow(std::uint8_t height) noexcept {
    const std::size_t covered = std::size_t{kTypeBGarbageRowsPerHeight} * height;
    return covered >= kPlayingFieldRows ? std::size_t{0} : kPlayingFieldRows - covered;
}

// The field row the fixed demo table is stamped at. The table is kTypeBDemoGarbageRows rows tall and
// sits on the bottom of the field, which is the same extent a Type B height of two covers.
inline constexpr std::size_t kDemoGarbageTopRow = kPlayingFieldRows - kTypeBDemoGarbageRows;

// Where a multiplayer round start's fill begins and ends. The original hands the fill a row of the
// board and a count; the fill climbs count - 1 rows from there and then runs to the bottom of the
// field, so the count sets the top row rather than the number of rows written.
inline constexpr std::size_t kMultiplayerGarbageStartRow = 13;
inline constexpr std::size_t kMultiplayerGarbageTopRow =
    kMultiplayerGarbageStartRow - (kMultiplayerRoundStartGarbageRows - 1);

// Register the cell pick on `vm` and return a callable tile source. The routine is baked into the
// binary at build time — no ROM, no runtime file. Each call reads the divider and answers one cell:
// kGarbageEmptyTile, or one of kGarbageBlockTileBase .. kGarbageBlockTileBase +
// kGarbageBlockTileCount - 1.
[[nodiscard]] retropp::Routine<std::uint8_t()> registerGarbageFold(retropp::Vm& vm);

// Fill the board with procedural garbage from `topRow` down to the bottom of the field, ten cells per
// row, taking each cell from `fold`. Every row is left with at least one gap: if the picks would have
// filled a row solid, its rightmost cell is forced empty. A `topRow` at or past the bottom of the
// field writes nothing. Only the board is written.
void initGarbage(systems::GameContext& game, const std::function<std::uint8_t()>& fold,
                 std::size_t topRow);

// Stamp the fixed demo garbage table onto the bottom kTypeBDemoGarbageRows rows of the board. Reads
// no divider and calls no fold, so an attract-mode demo replays identically every time.
void initDemoGarbage(systems::GameContext& game);

// The garbage seam the round init calls: stamps the demo table when the round is a demo, and
// otherwise fills a Type B start of the given height.
[[nodiscard]] systems::InitGarbageHook makeInitGarbageHook(std::function<std::uint8_t()> fold);

}  // namespace kirpich::vm
