#include "systems/scoring.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/line_clear_kind.h>

#include "data/gravity.h"  // framesPerDrop
#include "data/scoring.h"  // lineClearAward, shouldLevelUp, kLineClearScores, kLevelCap, kScoreSaturation

namespace kirpich::systems {

namespace {

// The per-kind clear counter for one line-clear kind. Both LineClearStats instances — the running
// clear tallies and the results-screen display counts — are indexed the same way. The enum has
// exactly four values, so the switch is total; the trailing return is unreachable.
std::uint8_t& statForKind(LineClearStats& stats, LineClearKind kind) {
    switch (kind) {
        case LineClearKind::SINGLE: return stats.singles;
        case LineClearKind::DOUBLE: return stats.doubles;
        case LineClearKind::TRIPLE: return stats.triples;
        case LineClearKind::TETRIS: return stats.tetrises;
    }
    return stats.singles;
}

// Scoreboard states 0..3 tally singles/doubles/triples/tetrises, in that order (tetris.asm:4887-4904);
// the enum shares the ordering, so the state IS the kind. Only states 0..3 reach this (state 4 is the
// soft-drop drain, handled separately).
LineClearKind kindForScoreboardState(std::uint8_t state) {
    return static_cast<LineClearKind>(state);
}

// The shared kind-complete path (Call_25D9._nextState, tetris.asm:6177-6189): hold 33 frames, disarm
// the phase, advance to the next kind, and — after the last kind — move to the game-over screen. Both
// the per-kind step (on an empty count) and the soft-drop drain (on an empty total) enter here.
void scoreboardNextState(GameContext& game) {
    GameFlowState& flow = game.flow;
    EngineState& engine = game.engine;
    flow.timer1 = 33;
    engine.scoreboardTallyPhase = 0;
    ++engine.scoreboardState;
    if (engine.scoreboardState == 5) {
        flow.gameState = GameState::GAME_OVER_SCREEN;
    }
}

// One tally step for a per-kind count (Call_25D9, tetris.asm:6109-6163). The count-up animates each
// unit over two vertical-blank passes: phase 1 moves the unit and folds its points, phase 2 prints and
// blips.
void scoreboardTallyStep(GameContext& game, LineClearKind kind) {
    EngineState& engine = game.engine;
    GameFlowState& flow = game.flow;

    // Phase 2 — the print pass (:6110-6112, :6165-6173): the score redraw is render; the sim effect is
    // the count-tick blip and disarming the phase.
    if (engine.scoreboardTallyPhase == 2) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        engine.scoreboardTallyPhase = 0;
        return;
    }

    // Phase 1 — with none of this kind left, the kind is complete (:6114-6116).
    std::uint8_t& pending = statForKind(engine.stats, kind);
    if (pending == 0) {
        scoreboardNextState(game);
        return;
    }

    // Move one unit from the pending count to the on-screen display and fold its points into the score
    // (:6117-6160), then arm the phase-2 print pass. The per-kind score accumulator and the on-screen
    // count digits the original also writes are render-derived — see the contract's derivability proof;
    // the display count itself is carried as state because it is not otherwise recoverable.
    --pending;
    ++statForKind(engine.scoreboardDisplayedStats, kind);
    const std::uint32_t base = kLineClearScores[static_cast<std::size_t>(kind)].points;
    engine.score = std::min<std::uint32_t>(engine.score + base * (flow.typeBLevel + 1),
                                           kScoreSaturation);
    engine.scoreboardTallyPhase = 2;
}

// The soft-drop drain (tallySoftDropPoints, tetris.asm:4844-4878): one accumulated soft-drop point per
// call, sped up to one per frame.
void tallySoftDropPoints(GameContext& game) {
    EngineState& engine = game.engine;
    GameFlowState& flow = game.flow;

    // The phase is disarmed first (:4845-4846).
    engine.scoreboardTallyPhase = 0;

    // No points left: this kind is complete. The original jumps into the shared kind-complete path (the
    // upstream "; What? Bug" jump, which lands correctly for the jumped-not-called entry). (:4847-4854)
    if (engine.softDropPoints == 0) {
        scoreboardNextState(game);
        return;
    }

    // One point per call: move it from the total to the count-up display, zero the frame timer so the
    // next point drains next frame ("speed this one up"), add one to the score (saturating), and blip.
    // The two inline prints are render. (:4855-4877)
    --engine.softDropPoints;
    ++engine.softDropPointsTallied;
    flow.timer1 = 0;
    engine.score = std::min<std::uint32_t>(engine.score + 1, kScoreSaturation);
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
}

}  // namespace

void addLineClearScore(GameContext& game) {
    GameFlowState& flow = game.flow;
    EngineState& engine = game.engine;

    // Gates in ROM order (tetris.asm:4993-5001): Type A, normal gameplay, wipe step 5.
    if (flow.gameType != GameType::TYPE_A) {
        return;
    }
    if (flow.gameState != GameState::NORMAL_GAMEPLAY) {
        return;
    }
    if (flow.wipeCounter != 5) {
        return;
    }

    // The first non-empty per-kind stat — singles first — is consumed (zeroed, not decremented,
    // :5002-5024) and awarded base x (level + 1), saturating at the score ceiling (:5025-5037). The
    // original's repeated per-step saturation equals the single final min by monotonicity. All four
    // empty: nothing is awarded.
    for (const LineClearKind kind : {LineClearKind::SINGLE, LineClearKind::DOUBLE,
                                     LineClearKind::TRIPLE, LineClearKind::TETRIS}) {
        std::uint8_t& stat = statForKind(engine.stats, kind);
        if (stat != 0) {
            stat = 0;
            engine.score = std::min<std::uint32_t>(engine.score + lineClearAward(kind, flow.level),
                                                   kScoreSaturation);
            return;
        }
    }
}

void updateScoreboard(GameContext& game) {
    EngineState& engine = game.engine;

    // Disarmed: nothing to do (tetris.asm:4881-4883).
    if (engine.scoreboardTallyPhase == 0) {
        return;
    }

    // State 4 drains the soft-drop total; states 0-3 tally one clear kind each (:4884-4906). State 5 is
    // unreachable here — scoreboardNextState moves to the game-over screen before it can be selected.
    if (engine.scoreboardState == 4) {
        tallySoftDropPoints(game);
        return;
    }
    scoreboardTallyStep(game, kindForScoreboardState(engine.scoreboardState));
}

void checkForLevelUp(GameContext& game) {
    GameFlowState& flow = game.flow;

    // Gates in ROM order (tetris.asm:5826-5835): normal gameplay, Type A, below the level cap.
    if (flow.gameState != GameState::NORMAL_GAMEPLAY) {
        return;
    }
    if (flow.gameType != GameType::TYPE_A) {
        return;
    }
    if (flow.level == kLevelCap) {
        return;
    }

    // The threshold law is the data layer's shouldLevelUp — the original's BCD digit compare (Call_249D
    // + the nibble assembly + cp b, :5836-5851), the 1000-line cutoff living inside it. One level per
    // call.
    if (!shouldLevelUp(flow.lines, flow.level)) {
        return;
    }
    ++flow.level;                                    // (:5852)
    game.audioCues.square = SquareSfxId::LEVEL_UP;   // (:5873-5874)

    // Reload the gravity countdown for the new level; the original's LookupGravity writes both the
    // countdown and its reload value (:5875 / :4258-4259). The level-digit prints (:5853-5870) are
    // render.
    const std::uint8_t reload = kirpich::framesPerDrop(flow.level, flow.heartMode != 0);
    flow.framesPerDrop = reload;
    flow.dropTimer = reload;
}

void printLineClearScores(GameContext& game, LineClearKind kind, std::uint8_t fieldRow,
                          std::uint8_t fieldCol) {
    // The value is the kind's base award times (typeBLevel + 1) (tetris.asm:4664-4678). The original
    // computes it in the score scratch and its caller zeroes that scratch right after the four calls;
    // the port computes it locally and leaves the score untouched — the handler beat is atomic in the
    // port's tick model, so a non-clobbering computation is observation-equivalent (see the contract).
    const std::uint32_t base = kLineClearScores[static_cast<std::size_t>(kind)].points;
    const std::uint32_t value = base * (game.flow.typeBLevel + 1);
    if (value == 0) {
        return;  // unreachable for the real award bases
    }

    // Decimal digits, most-significant first, leading zeros suppressed: one raw digit byte (0-9) per
    // field cell, left-aligned from (fieldRow, fieldCol) (:4680-4706). Cells past the last digit are
    // left as they are. base x (typeBLevel + 1) <= 1200 x 10 = 12000, well under six digits.
    std::array<std::uint8_t, 6> digits{};
    std::size_t len = 0;
    for (std::uint32_t v = value; v > 0; v /= 10) {
        digits[len++] = static_cast<std::uint8_t>(v % 10);
    }
    for (std::size_t i = 0; i < len; ++i) {
        game.field.fieldCell(fieldRow, fieldCol + i) = digits[len - 1 - i];
    }
}

void clearScoreAndStats(GameContext& game) {
    EngineState& engine = game.engine;

    // The original zeroes the 27-byte span wLineClearStats..$C0C6 plus the 3-byte score
    // (tetris.asm:6192-6204): the per-kind stats and their display counts, the (render-derived) per-kind
    // score accumulators, the soft-drop total and its count-up display, and the two scoreboard state
    // bytes. blockSoftDropAfterLock ($C0C7) and scoreRedrawRequested ($C0CE) sit past the span and are
    // left alone.
    engine.stats = LineClearStats{};
    engine.scoreboardDisplayedStats = LineClearStats{};
    engine.softDropPoints = 0;
    engine.softDropPointsTallied = 0;
    engine.scoreboardState = 0;
    engine.scoreboardTallyPhase = 0;
    engine.score = 0;
}

}  // namespace kirpich::systems
