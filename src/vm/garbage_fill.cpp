#include "vm/garbage_fill.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "retropp/gb.h"  // retropp::gb::A (pulls retropp/vm.h)

namespace kirpich::vm {

retropp::Routine<std::uint8_t()> registerGarbageFold(retropp::Vm& vm) {
    // Registers and the asset path appear only here, inside the binding (the VM host contract; the
    // path is a literal at its use site). Output is register A — the pick leaves the cell's tile
    // there. Embed: the .asm is assembled and baked into the binary at build time — no runtime file.
    return vm.registerRoutine<std::uint8_t()>(
        "src/vm/garbage.asm",
        retropp::RoutineBinding{.output = retropp::gb::A,
                                .throttle = retropp::Throttle::HostSpeed},
        retropp::AssetPolicy::Embed);
}

void initGarbage(systems::GameContext& game, const std::function<std::uint8_t()>& fold,
                 std::size_t topRow) {
    // tetris.asm:4324-4403. The original walks a pointer up the board and then fills downward until
    // the pointer runs off the bottom row; the walk is expressed here as the row range it covers.
    for (std::size_t row = topRow; row < kPlayingFieldRows; ++row) {
        // Whether a gap has landed on this row yet. The original keeps this in a byte it clears at
        // every row end (tetris.asm:4388-4389); it lives only for the length of the fill, so it is a
        // local here. Which branch the pick took is recoverable from the tile it answered, so no
        // separate flag crosses the boundary.
        bool rowHasGap = false;

        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            std::uint8_t cell = fold();

            if (cell == kGarbageEmptyTile) {
                rowHasGap = true;
            } else if (col + 1 == kPlayingFieldCols && !rowHasGap) {
                // The rightmost cell of a row that has no gap yet is forced empty, so no row can be
                // solid (tetris.asm:4355-4366). The original prices this at a 1-in-512 chance.
                cell = kGarbageEmptyTile;
            }

            // Both grids, as the original writes them: once at the map address it is walking, then
            // again $3000 above it in the board (tetris.asm:4371-4381). Garbage appears the moment it
            // is generated — there is no wipe to carry it across. The second write is skipped in a
            // link-cable game, where the row is staged rather than shown.
            game.field.fieldCell(row, col) = cell;
            if (!game.multiplayer.isMultiplayer) {
                game.display.map[row][kPlayingFieldOriginCol + col] = cell;
            }
        }
    }
}

void initDemoGarbage(systems::GameContext& game) {
    // tetris.asm:4286-4314.
    for (std::size_t row = 0; row < kTypeBDemoGarbageRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            game.field.fieldCell(kDemoGarbageTopRow + row, col) = kTypeBDemoGarbage[row][col];
            game.display.map[kDemoGarbageTopRow + row][kPlayingFieldOriginCol + col] =
                kTypeBDemoGarbage[row][col];
        }
    }
}

systems::InitGarbageHook makeInitGarbageHook(std::function<std::uint8_t()> fold) {
    // tetris.asm:4219-4232 — the round init picks the demo table over a procedural fill, and passes
    // the chosen Type B height as the row count.
    return [fold = std::move(fold)](systems::GameContext& game, std::uint8_t rows,
                                    bool useDemoTable) {
        if (useDemoTable) {
            initDemoGarbage(game);
        } else {
            initGarbage(game, fold, typeBGarbageTopRow(rows));
        }
    };
}

}  // namespace kirpich::vm
