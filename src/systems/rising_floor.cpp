#include "systems/rising_floor.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/garbage.h"         // kGarbageEmptyTile
#include "data/playing_field.h"   // field extent + origin column
#include "data/sfx.h"             // SquareSfxId
#include "systems/readouts.h"     // printRise

namespace kirpich::systems {

namespace {

// Whether this round is the one the floor rises in.
bool typeC(const GameFlowState& flow) {
    return flow.gameType == GameType::TYPE_C;
}

}  // namespace

void armRiseCounter(GameContext& game) {
    game.flow.riseCounter = typeC(game.flow) ? kTypeCRiseInterval : 0;
    if (typeC(game.flow)) {
        printRise(game, game.display.map);
    }
}

void recordLock(GameContext& game, std::uint8_t clearedRows) {
    if (!typeC(game.flow)) {
        return;
    }

    // One off for the piece, the rows it cleared back on, held between zero and a full interval. The
    // arithmetic is done wide so neither end can wrap on the way.
    int next = static_cast<int>(game.flow.riseCounter) - 1 + static_cast<int>(clearedRows);
    next = std::clamp(next, 0, static_cast<int>(kTypeCRiseInterval));
    game.flow.riseCounter = static_cast<std::uint8_t>(next);

    // The panel follows the count. Without this the number would only ever be drawn twice - once by the
    // round init and once by the rise that reloads it - and would read as a fixed ten.
    //
    // The live map only, as the rise's own redraw does: the paused screen keeps the count it had when
    // the player paused, which is how the line count behaves too.
    printRise(game, game.display.map);
}

void riseFloor(GameContext& game, const std::function<std::uint8_t()>& fold) {
    PlayingFieldState& field = game.field;

    // Every field row takes the contents of the row below it. Row 0's own contents are overwritten and
    // not stored anywhere, which is how a stack that reaches the ceiling loses its top row.
    for (std::size_t row = 0; row + 1 < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            const std::uint8_t cell = field.fieldCell(row + 1, col);
            field.fieldCell(row, col) = cell;
            game.display.map[row][kPlayingFieldOriginCol + col] = cell;
        }
    }

    // The row that arrives, one cell at a time. The rightmost cell of a row that has no gap yet is
    // forced empty, the same guarantee the starting garbage carries: a solid row would be unclearable
    // and the stack would only ever climb.
    const std::size_t bottom = kPlayingFieldRows - 1;
    bool rowHasGap = false;
    for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
        std::uint8_t cell = fold();

        if (cell == kGarbageEmptyTile) {
            rowHasGap = true;
        } else if (col + 1 == kPlayingFieldCols && !rowHasGap) {
            cell = kGarbageEmptyTile;
        }

        field.fieldCell(bottom, col) = cell;
        game.display.map[bottom][kPlayingFieldOriginCol + col] = cell;
    }

    game.audioCues.square = SquareSfxId::GARBAGE_ATTACK;
}

RiseFloorHook makeRiseFloorHook(std::function<std::uint8_t()> fold) {
    return [fold = std::move(fold)](GameContext& game) {
        if (!typeC(game.flow)) {
            return;
        }
        if (game.flow.riseCounter != 0) {
            return;
        }
        if (game.flow.gameState != GameState::NORMAL_GAMEPLAY) {
            return;
        }

        riseFloor(game, fold);
        game.flow.riseCounter = kTypeCRiseInterval;

        // The live map only. The paused screen keeps the count it had when the player paused, which is
        // how the line count behaves as well.
        printRise(game, game.display.map);
    };
}

}  // namespace kirpich::systems
