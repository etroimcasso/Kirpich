#include "systems/line_clear.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/bounded_vec.h"
#include "data/music.h"           // MusicId
#include "data/playing_field.h"   // field extent + wipe-counter domain
#include "data/sfx.h"             // NoiseSfxId, SquareSfxId
#include "systems/piece.h"        // nextPiece

namespace kirpich::systems {

namespace {

// The top two field rows are never scanned for completion: the original starts its row scan two rows
// below the field top ($C842), so a piece resting in the top two rows never triggers a clear.
constexpr std::uint8_t kUnclearedTopRows = 2;

// The empty-cell tile: a field cell holds a space when nothing occupies it.
constexpr std::uint8_t kEmptyCell = static_cast<std::uint8_t>(CharTile::SPACE);

// Build the completed-rows list from the first `count` field-row indices of a scan buffer
// (count <= 4). The list stores row indices; a shorter list simply has fewer live entries. The value
// is rebuilt through BoundedVec's constructor rather than mutated in place.
BoundedVec<std::uint8_t, 4> makeLineClears(const std::array<std::uint8_t, 4>& rows,
                                           std::uint8_t count) {
    switch (count) {
        case 1: return {rows[0]};
        case 2: return {rows[0], rows[1]};
        case 3: return {rows[0], rows[1], rows[2]};
        case 4: return {rows[0], rows[1], rows[2], rows[3]};
        default: return {};
    }
}

// The final wipe step (counter 19). Latches out soft drop, ends the wipe, and then either spawns the
// next piece, finishes a Type B round, or (for a garbage-driven wipe) simply stops.
// (tetris.asm:5744-5810)
void wipeTerminal(GameContext& game, const std::function<std::uint8_t()>& draw) {
    GameFlowState& flow = game.flow;
    MultiplayerState& mp = game.multiplayer;

    // Suppress soft drop until the next fresh press, and end the wipe. (:5748-5753)
    game.engine.blockSoftDropAfterLock = true;
    flow.wipeCounter = 0;

    if (!mp.isMultiplayer) {
        // Solo: continue only during normal gameplay. (:5754-5759)
        if (flow.gameState != GameState::NORMAL_GAMEPLAY) {
            return;
        }
    } else {
        // Two-player: continue only during the game proper. (:5802-5804)
        if (flow.gameState != GameState::TWO_PLAYER_GAME) {
            return;
        }
        // A garbage-driven wipe ends here — no line print, no spawn. (:5805-5810)
        if (mp.garbageWipeActive) {
            mp.garbageWipeActive = false;
            return;
        }
    }

    // The line count is redrawn here (render). Type A always spawns the next piece; Type B spawns
    // while lines remain. (:5760-5777)
    if (flow.gameType == GameType::TYPE_A) {
        nextPiece(game, draw);
        return;
    }
    if (flow.lines != 0) {
        nextPiece(game, draw);
        return;
    }

    // Type B, line goal reached: hold, cue the stage-clear music, and finish the round. (:5778-5796)
    flow.timer1 = 0x64;
    game.audioCues.music = MusicId::STAGE_CLEAR;
    if (mp.isMultiplayer) {
        mp.linesGoalReached = true;
        return;
    }
    flow.gameState = (flow.typeBLevel == 9) ? GameState::INIT_TYPE_B_BONUS
                                            : GameState::TYPE_B_VICTORY_JINGLE;
}

}  // namespace

void checkForCompletedRows(GameContext& game) {
    GameFlowState& flow = game.flow;
    EngineState& engine = game.engine;

    // Run once, on the frame the piece finishes locking (stage 2). (tetris.asm:5301-5304)
    if (flow.pieceLockStage != 2) {
        return;
    }

    // The lock sound cues on every stage-2 entry, whether or not a row clears. (:5305-5306)
    game.audioCues.noise = NoiseSfxId::LOCK_PIECE;

    // Scan the field from row kUnclearedTopRows down. A row is complete when none of its ten cells is
    // empty; complete rows are recorded top to bottom by field-row index. (:5307-5337)
    flow.completedRowCount = 0;
    std::array<std::uint8_t, 4> completed{};
    std::uint8_t count = 0;
    for (std::uint8_t row = kUnclearedTopRows; row < kPlayingFieldRows; ++row) {
        bool complete = true;
        for (std::uint8_t col = 0; col < kPlayingFieldCols; ++col) {
            if (game.field.fieldCell(row, col) == kEmptyCell) {
                complete = false;
                break;
            }
        }
        if (complete) {
            assert(count < completed.size() && "at most four rows clear from one lock");
            completed[count++] = row;
        }
    }
    engine.lineClears = makeLineClears(completed, count);
    flow.completedRowCount = count;

    // Advance to lock stage 3 and arm the flash timer. (:5339-5342)
    flow.pieceLockStage = 3;
    flow.timer1 = 2;

    // No completed rows: nothing to tally. (:5343-5345)
    if (count == 0) {
        return;
    }

    // Line-count update, forked on game type. (:5346-5410)
    if (flow.gameType == GameType::TYPE_A) {
        // Type A accumulates lines, saturating at 9999. (:5351-5364)
        const int total = static_cast<int>(flow.lines) + count;
        flow.lines = static_cast<std::uint16_t>(std::min(total, 9999));
    } else {
        // Type B counts down to zero. (:5366-5376, :5407-5410)
        if (flow.lines <= count) {
            flow.lines = 0;
        } else {
            flow.lines -= count;
            // A borrow that lands the tens digit on 9 zeroes the count. Unreachable for legal Type B
            // states (the line goal never exceeds 25); preserved from the original. (:5374-5376)
            if ((flow.lines % 100) / 10 == 9) {
                flow.lines = 0;
            }
        }
    }

    // Per-kind tally: bump the matching stat, set the rows-to-send-as-garbage count, and cue the clear
    // (or Tetris) sound. (:5377-5401)
    SquareSfxId clearSfx = SquareSfxId::LINE_CLEAR;
    std::uint8_t garbageRows = 0;
    switch (count) {
        case 1:  ++engine.stats.singles;  garbageRows = 0; break;
        case 2:  ++engine.stats.doubles;  garbageRows = 1; break;
        case 3:  ++engine.stats.triples;  garbageRows = 2; break;
        default: ++engine.stats.tetrises; garbageRows = 4; clearSfx = SquareSfxId::TETRIS; break;
    }
    game.multiplayer.garbageRowsToSend = garbageRows;
    game.audioCues.square = clearSfx;
}

void animateLineClear(GameContext& game, const std::function<std::uint8_t()>& draw) {
    GameFlowState& flow = game.flow;

    // Runs in lock stage 3, and only once the flash timer has elapsed. (tetris.asm:5412-5418)
    if (flow.pieceLockStage != 3 || flow.timer1 != 0) {
        return;
    }

    // On an even flash phase with no rows to clear, this is a plain lock: spawn the next piece and end
    // the lock. (:5420-5426, :5494-5496)
    if ((flow.blinkCounter & 1) == 0 && game.engine.lineClears.empty()) {
        nextPiece(game, draw);
        flow.pieceLockStage = 0;
        return;
    }

    // The flash itself paints video memory (solid blocks, then a blank pass) and is re-derived by the
    // renderer from the flash phase and the completed-rows list; it changes no game state here.
    // Advance the flash phase. (:5427-5456)
    ++flow.blinkCounter;
    if (flow.blinkCounter == 7) {
        // Seven passes done: reset the phase, hold before the wipe, and start it. (:5458-5468)
        flow.blinkCounter = 0;
        flow.timer1 = 13;
        flow.wipeCounter = 1;
        flow.pieceLockStage = 0;
    } else {
        flow.timer1 = 10;
    }
}

void moveBlocksDownAfterLineClear(GameContext& game) {
    GameFlowState& flow = game.flow;
    PlayingFieldState& field = game.field;

    // Runs on the frame the flash's hold timer reaches zero, at the first wipe step.
    // (tetris.asm:5498-5504)
    if (flow.timer1 != 0 || flow.wipeCounter != 1) {
        return;
    }

    // For each cleared row, drop every row above it down by one. Row 0 is never written inside this
    // walk (the original stops when it reaches the row above the field top). (:5505-5538)
    for (std::uint8_t r : game.engine.lineClears) {
        for (std::uint8_t dest = r; dest >= 1; --dest) {
            for (std::uint8_t col = 0; col < kPlayingFieldCols; ++col) {
                field.fieldCell(dest, col) = field.fieldCell(dest - 1, col);
            }
        }
    }

    // Clear the top row once, after every cleared row has been processed. With more than one row
    // cleared this leaves shifted copies of the old top row in rows 1..n-1 — the original's behavior,
    // preserved. (:5540-5546)
    for (std::uint8_t col = 0; col < kPlayingFieldCols; ++col) {
        field.fieldCell(0, col) = kEmptyCell;
    }

    clearLineClearsList(game);  // (:5547)
    flow.wipeCounter = 2;       // (:5548-5549)
}

void clearLineClearsList(GameContext& game) {
    // Empty the completed-rows list; nothing else is touched. (tetris.asm:5552-5560)
    game.engine.lineClears = {};
}

void playingFieldWipeTick(GameContext& game, const std::function<std::uint8_t()>& draw) {
    GameFlowState& flow = game.flow;

    // The wipe runs for counter values kPlayingFieldWipeCounterFirst..Last; outside that range there
    // is nothing to do (each per-row step in the original gates on its own counter value).
    if (flow.wipeCounter < kPlayingFieldWipeCounterFirst ||
        flow.wipeCounter > kPlayingFieldWipeCounterLast) {
        return;
    }

    const std::uint8_t step = flow.wipeCounter;

    // Each step redraws one field row back to the screen, bottom row first
    // (playingFieldRowForWipeCounter(step)); that copy is a render effect owned by the presentation
    // pass. The step's own state effect is to advance the counter — except the final step, which sets
    // the counter itself below.
    if (step != kPlayingFieldWipeCounterLast) {
        ++flow.wipeCounter;
    }

    switch (step) {
        case 8:
            // As the stack finishes falling, cue the settle sound — or, for a garbage-driven wipe in a
            // two-player game, the garbage-attack sweep. (tetris.asm:5619-5645)
            if (!game.multiplayer.isMultiplayer) {
                if (flow.gameState == GameState::NORMAL_GAMEPLAY) {
                    game.audioCues.noise = NoiseSfxId::STACK_FALL;
                }
            } else if (flow.gameState == GameState::TWO_PLAYER_GAME) {
                if (game.multiplayer.garbageWipeActive) {
                    game.audioCues.square = SquareSfxId::GARBAGE_ATTACK;
                } else {
                    game.audioCues.noise = NoiseSfxId::STACK_FALL;
                }
            }
            break;

        case 16:
            // The original checks for a Type A level-up here. Level-up gating lands with the scoring
            // work; this step carries no state effect yet. (tetris.asm:5710-5718)
            break;

        case 17:
        case 18:
            // Score and line-count redraws into the two paused-screen tilemaps — render only, no state
            // effect. (tetris.asm:5720-5742)
            break;

        case 19:
            wipeTerminal(game, draw);
            break;

        default:
            break;
    }
}

}  // namespace kirpich::systems
