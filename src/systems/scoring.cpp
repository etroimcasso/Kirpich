#include "systems/scoring.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/line_clear_kind.h>

#include "data/gravity.h"     // framesPerDrop
#include "systems/readouts.h"  // printLevelStep
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
// The tally's print pass writes straight into the displayed screen (tetris.asm:6167 targets $9A25, a
// map address, not the board). That is what makes the count-up visible: no wipe runs during the
// tally, so a print into the board would never reach the screen.
//
// Six digits, most-significant first, leading zeros left as the stored screen has them - the running
// score at the bottom of the scoreboard.
void printSixDigits(GameContext& game, std::size_t row, std::size_t col, std::uint32_t value) {
    constexpr std::size_t kDigits = 6;
    for (std::size_t i = 0; i < kDigits; ++i) {
        game.display.map[row][col + kDigits - 1 - i] = static_cast<std::uint8_t>(value % 10);
        value /= 10;
    }
}

// Where each kind's line on the scoreboard is (tetris.asm:4888, :4893, :4898, :4903 - $9823 / $9883 /
// $98E3 / $9943, all map addresses). The count's units digit sits at that cell and its tens digit one
// to the left; the kind's running score is six digits at bc + $23, which is the next row at column 6.
constexpr std::size_t kCountCol = 3;

// The drop count's line ($99A5, :4866).
constexpr std::size_t kDropsRow = 13;
constexpr std::size_t kDropsCol = 5;
constexpr std::size_t kKindScoreCol = 6;
constexpr std::array<std::size_t, 4> kKindCountRow{1, 4, 7, 10};

// The count is two digits, units first, and the tens digit is left alone when it is zero
// (:6124-6130), so a stored screen's blank stays blank rather than becoming a 0.
void printKindCount(GameContext& game, LineClearKind kind, std::uint8_t count) {
    const std::size_t row = kKindCountRow[static_cast<std::size_t>(kind)];
    game.display.map[row][kCountCol] = static_cast<std::uint8_t>(count % 10);
    const std::uint8_t tens = static_cast<std::uint8_t>((count / 10) % 10);
    if (tens != 0) {
        game.display.map[row][kCountCol - 1] = tens;
    }
}

// The kind's running score: its base times the level multiplier, once per unit counted so far. The
// original accumulates it a unit at a time (:6133-6141); the same total re-derives from the display
// count, which is why the accumulator itself is not carried.
void printKindScore(GameContext& game, LineClearKind kind, std::uint8_t displayed) {
    const std::uint32_t base = kLineClearScores[static_cast<std::size_t>(kind)].points;
    const std::uint32_t value = base * (game.flow.typeBLevel + 1) * displayed;
    printSixDigits(game, kKindCountRow[static_cast<std::size_t>(kind)] + 1, kKindScoreCol, value);
}

void printScoreToScreen(GameContext& game) {
    constexpr std::size_t kScoreRow = 17;  // $9A25: ($9A25 - $9800) / 32
    constexpr std::size_t kScoreCol = 5;   // ($9A25 - $9800) % 32

    printSixDigits(game, kScoreRow, kScoreCol, game.engine.score);
}

void scoreboardTallyStep(GameContext& game, LineClearKind kind) {
    EngineState& engine = game.engine;
    GameFlowState& flow = game.flow;

    // Phase 2 — the print pass (:6110-6112, :6165-6173): redraw the score, and the sim effect is
    // the count-tick blip and disarming the phase.
    if (engine.scoreboardTallyPhase == 2) {
        const std::uint8_t displayed = statForKind(engine.scoreboardDisplayedStats, kind);
        printKindCount(game, kind, displayed);
        printKindScore(game, kind, displayed);
        printScoreToScreen(game);
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
    flow.scorePrintFlag = 1;  // AddBCD marks the score changed (:187-188, called at :6138)
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
    // (:4855-4877)
    --engine.softDropPoints;
    ++engine.softDropPointsTallied;
    flow.timer1 = 0;
    engine.score = std::min<std::uint32_t>(engine.score + 1, kScoreSaturation);
    flow.scorePrintFlag = 1;  // AddBCD marks the score changed (:187-188, called at :4864/:4872)
    game.audioCues.square = SquareSfxId::CHANGE_SCREEN;

    // Both prints go to the displayed screen: the drop count on its own line ($99A5, :4866) and the
    // running score at the bottom.
    printSixDigits(game, kDropsRow, kDropsCol, engine.softDropPointsTallied);
    printScoreToScreen(game);
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
            flow.scorePrintFlag = 1;  // AddBCD marks the score changed (:187-188, called at :5032)
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
    printLevelStep(game);                            // (:5853-5870)
    game.audioCues.square = SquareSfxId::LEVEL_UP;   // (:5873-5874)

    // Reload the gravity countdown for the new level; the original's LookupGravity writes both the
    // countdown and its reload value (:5875 / :4258-4259).
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
