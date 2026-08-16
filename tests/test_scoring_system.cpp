// Scoring pipeline — behavioral tests against docs/contracts/scoring-system.md.
//
// Device-free: the five functions are pure logic over the game-state aggregate. Every asserted value
// is traced to the tetris.asm lines named in the contract (AddLineClearScore :4992, UpdateScoreboard
// :4880 with Call_25D9 :6109 and tallySoftDropPoints :4844, Call_244B :5825, PrintLineClearScores
// :4662, ClearScoreAndStats :6191, and the wipe-16 seam :5710). No ROM read, no virtual machine.
//
// The Type B results count-up is co-driven by the scoreboard handler (GameState_0B, :4708-4716), which
// re-arms the tally when the frame timer expires. A file-local frame harness reproduces that: the
// handler beat re-arms, then the saturating frame-timer decrement, then the vertical-blank beat runs
// the tally. The award math (lineClearAward / shouldLevelUp / framesPerDrop) is the data layer's;
// tests cross-check the wired result against those functions rather than re-deriving the numbers.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/line_clear_kind.h>

#include "data/gravity.h"         // framesPerDrop
#include "data/scoring.h"         // lineClearAward, kLineClearScores, kLevelCap, kScoreSaturation
#include "data/sfx.h"             // SquareSfxId
#include "systems/game_context.h"
#include "systems/line_clear.h"  // playingFieldWipeTick (wipe-16 wiring)
#include "systems/scoring.h"

namespace {

using kirpich::GameState;
using kirpich::GameType;
using kirpich::kLevelCap;
using kirpich::kLineClearScores;
using kirpich::kScoreSaturation;
using kirpich::LineClearKind;
using kirpich::LineClearStats;
using kirpich::lineClearAward;
using kirpich::SquareSfxId;
using kirpich::systems::addLineClearScore;
using kirpich::systems::checkForLevelUp;
using kirpich::systems::clearScoreAndStats;
using kirpich::systems::GameContext;
using kirpich::systems::playingFieldWipeTick;
using kirpich::systems::printLineClearScores;
using kirpich::systems::updateScoreboard;

const std::function<std::uint8_t()> noDraw = []() -> std::uint8_t { return 0; };

std::uint32_t base(LineClearKind kind) {
    return kLineClearScores[static_cast<std::size_t>(kind)].points;
}

// One frame of the Type B results count-up: the scoreboard handler re-arms the tally on timer expiry,
// then the timers tick, then the vertical-blank tally runs.
void scoreboardFrame(GameContext& game) {
    if (game.flow.timer1 == 0) {                 // GameState_0B (:4708-4716)
        game.engine.scoreboardTallyPhase = 1;
        game.flow.timer1 = 5;
    }
    if (game.flow.timer1 > 0) --game.flow.timer1;
    updateScoreboard(game);
}

}  // namespace

// 1. AddLineClearScoreVectors — the gate matrix (:4993-5001), the first-non-zero walk with the
// zero-not-decrement consume and the two-kinds-pending order (:5002-5024), the base x (level+1) award
// and the 999,999 clamp (:5025-5037), and the all-zero no-op.
TEST(ScoringSystem, AddLineClearScoreVectors) {
    auto armed = [](GameContext& game) {
        game.flow.gameType = GameType::TYPE_A;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.wipeCounter = 5;
    };

    // Gate: wrong game type.
    {
        GameContext game;
        armed(game);
        game.flow.gameType = GameType::TYPE_B;
        game.engine.stats.singles = 1;
        const GameContext snap = game;
        addLineClearScore(game);
        EXPECT_EQ(game, snap);
    }
    // Gate: wrong game state.
    {
        GameContext game;
        armed(game);
        game.flow.gameState = GameState::GAME_OVER_SCREEN;
        game.engine.stats.singles = 1;
        const GameContext snap = game;
        addLineClearScore(game);
        EXPECT_EQ(game, snap);
    }
    // Gate: wipe counter not at 5.
    {
        GameContext game;
        armed(game);
        game.flow.wipeCounter = 4;
        game.engine.stats.singles = 1;
        const GameContext snap = game;
        addLineClearScore(game);
        EXPECT_EQ(game, snap);
    }

    // Each kind alone: it is zeroed and awarded base x (level + 1).
    const std::array<LineClearKind, 4> kinds = {LineClearKind::SINGLE, LineClearKind::DOUBLE,
                                                LineClearKind::TRIPLE, LineClearKind::TETRIS};
    for (const LineClearKind kind : kinds) {
        for (const std::uint8_t level : {std::uint8_t{0}, std::uint8_t{5}, kLevelCap}) {
            GameContext game;
            armed(game);
            game.flow.level = level;
            std::uint8_t& stat = (kind == LineClearKind::SINGLE)   ? game.engine.stats.singles
                                 : (kind == LineClearKind::DOUBLE) ? game.engine.stats.doubles
                                 : (kind == LineClearKind::TRIPLE) ? game.engine.stats.triples
                                                                   : game.engine.stats.tetrises;
            stat = 1;
            addLineClearScore(game);
            EXPECT_EQ(game.engine.score, lineClearAward(kind, level))
                << "kind " << int(kind) << " level " << int(level);
            EXPECT_EQ(stat, 0) << "kind " << int(kind) << " level " << int(level);
        }
    }

    // Two kinds pending: only the first in order (single before double) is awarded and zeroed; the
    // second is retained for the next call.
    {
        GameContext game;
        armed(game);
        game.flow.level = 0;
        game.engine.stats.singles = 1;
        game.engine.stats.doubles = 1;
        addLineClearScore(game);
        EXPECT_EQ(game.engine.score, base(LineClearKind::SINGLE));  // 40
        EXPECT_EQ(game.engine.stats.singles, 0);
        EXPECT_EQ(game.engine.stats.doubles, 1);
        addLineClearScore(game);  // second call awards the double
        EXPECT_EQ(game.engine.score, base(LineClearKind::SINGLE) + base(LineClearKind::DOUBLE));  // 140
        EXPECT_EQ(game.engine.stats.doubles, 0);
    }

    // The award saturates at the score ceiling.
    {
        GameContext game;
        armed(game);
        game.flow.level = kLevelCap;
        game.engine.score = kScoreSaturation - 10;
        game.engine.stats.tetrises = 1;
        addLineClearScore(game);
        EXPECT_EQ(game.engine.score, kScoreSaturation);
    }

    // All kinds empty: nothing is written.
    {
        GameContext game;
        armed(game);
        const GameContext snap = game;
        addLineClearScore(game);
        EXPECT_EQ(game, snap);
    }
}

// 2. ScoreboardTallyWalk — the per-unit mechanics (:6117-6160), the phase 1->2->0 sequence and the
// print-pass cue, the score fold x (typeBLevel+1), the kind-complete 33-frame hold and state advance
// (:6175-6189), and a full drive to the terminal game-over transition.
TEST(ScoringSystem, ScoreboardTallyWalk) {
    // Per-unit mechanics, driven by directly arming each phase.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.typeBLevel = 2;  // fold multiplier 3
        game.engine.stats.doubles = 1;
        game.engine.scoreboardState = 1;  // doubles

        game.engine.scoreboardTallyPhase = 1;  // handler re-arm
        updateScoreboard(game);
        EXPECT_EQ(game.engine.stats.doubles, 0);                   // pending drained
        EXPECT_EQ(game.engine.scoreboardDisplayedStats.doubles, 1);  // display rose
        EXPECT_EQ(game.engine.score, base(LineClearKind::DOUBLE) * 3);  // 300
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 2);            // print pass armed
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);      // no cue yet

        updateScoreboard(game);  // phase 2
        EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
    }

    // Kind-complete: an empty count holds 33 frames and advances the state.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.engine.scoreboardState = 2;  // triples, none pending
        game.engine.scoreboardTallyPhase = 1;
        updateScoreboard(game);
        EXPECT_EQ(game.flow.timer1, 33);
        EXPECT_EQ(game.engine.scoreboardState, 3);
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
    }

    // Full drive to the terminal: singles 1, doubles 2, triples 0, tetris 1; no soft-drop points.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.typeBLevel = 2;
        game.flow.gameState = GameState::INIT_TYPE_B_SCOREBOARD;
        game.engine.stats = LineClearStats{1, 2, 0, 1};
        game.engine.softDropPoints = 0;
        game.engine.scoreboardState = 0;
        game.flow.timer1 = 0;

        std::uint8_t maxTimer = 0;
        int frames = 0;
        for (; frames < 5000; ++frames) {
            scoreboardFrame(game);
            maxTimer = std::max(maxTimer, game.flow.timer1);
            if (game.flow.gameState == GameState::GAME_OVER_SCREEN) {
                break;
            }
        }
        ASSERT_LT(frames, 5000) << "tally must terminate";

        const std::uint32_t expected = base(LineClearKind::SINGLE) * 3 * 1 +
                                       base(LineClearKind::DOUBLE) * 3 * 2 +
                                       base(LineClearKind::TETRIS) * 3 * 1;  // 120 + 600 + 3600
        EXPECT_EQ(game.engine.score, expected);
        EXPECT_EQ(game.engine.scoreboardDisplayedStats, (LineClearStats{1, 2, 0, 1}));
        EXPECT_EQ(game.engine.stats, LineClearStats{});      // every count drained
        EXPECT_EQ(game.engine.scoreboardState, 5);           // past the last kind
        EXPECT_EQ(maxTimer, 33);                             // the between-kinds hold was reached
    }
}

// 3. SoftDropDrainVectors — one point per drain with the timer zeroed each call (:4855-4877), the
// tallied+remaining invariant, the score +1 and its clamp, the cue per drain, and the zero-points
// early-out into the shared kind-complete path (:4847-4854).
TEST(ScoringSystem, SoftDropDrainVectors) {
    // Drain three points, one per armed call.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.engine.scoreboardState = 4;  // soft-drop slot
        game.engine.softDropPoints = 3;
        game.engine.softDropPointsTallied = 0;
        game.engine.score = 100;
        for (int i = 1; i <= 3; ++i) {
            game.engine.scoreboardTallyPhase = 1;  // handler re-arm
            game.flow.timer1 = 7;                  // a live timer the drain must zero
            updateScoreboard(game);
            EXPECT_EQ(game.engine.softDropPointsTallied, i);
            EXPECT_EQ(game.engine.softDropPoints, 3 - i);
            EXPECT_EQ(game.engine.softDropPointsTallied + game.engine.softDropPoints, 3);  // invariant
            EXPECT_EQ(game.engine.score, 100u + i);
            EXPECT_EQ(game.flow.timer1, 0);  // "speed this one up"
            EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
            EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
        }
    }

    // The score +1 saturates at the ceiling.
    {
        GameContext game;
        game.engine.scoreboardState = 4;
        game.engine.softDropPoints = 1;
        game.engine.score = kScoreSaturation;
        game.engine.scoreboardTallyPhase = 1;
        updateScoreboard(game);
        EXPECT_EQ(game.engine.score, kScoreSaturation);
    }

    // Zero points: straight to the kind-complete path — hold 33, advance state (4->5 tips into the
    // game-over screen), phase disarmed, score untouched.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.gameState = GameState::INIT_TYPE_B_SCOREBOARD;
        game.engine.scoreboardState = 4;
        game.engine.softDropPoints = 0;
        game.engine.score = 555;
        game.engine.scoreboardTallyPhase = 1;
        updateScoreboard(game);
        EXPECT_EQ(game.flow.timer1, 33);
        EXPECT_EQ(game.engine.scoreboardState, 5);
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_SCREEN);
        EXPECT_EQ(game.engine.score, 555u);
    }
}

// 4. LevelUpVectors — the gate matrix (:5826-5835), threshold vectors against shouldLevelUp with the
// 1000-line cutoff and one-level-per-call (:5836-5852), both timers reloaded via framesPerDrop incl. a
// heart-mode vector (:5875 / :4258-4259), and the level-up cue (:5873-5874).
TEST(ScoringSystem, LevelUpVectors) {
    auto typeA = [](GameContext& game) {
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_A;
    };

    // Gate: wrong state.
    {
        GameContext game;
        typeA(game);
        game.flow.gameState = GameState::TITLE_SCREEN;
        game.flow.lines = 50;
        const GameContext snap = game;
        checkForLevelUp(game);
        EXPECT_EQ(game, snap);
    }
    // Gate: wrong type.
    {
        GameContext game;
        typeA(game);
        game.flow.gameType = GameType::TYPE_B;
        game.flow.lines = 50;
        const GameContext snap = game;
        checkForLevelUp(game);
        EXPECT_EQ(game, snap);
    }
    // Gate: already at the level cap.
    {
        GameContext game;
        typeA(game);
        game.flow.level = kLevelCap;
        game.flow.lines = 9999;
        const GameContext snap = game;
        checkForLevelUp(game);
        EXPECT_EQ(game, snap);
    }

    // Below the threshold: no level up.
    {
        GameContext game;
        typeA(game);
        game.flow.level = 0;
        game.flow.lines = 9;  // floor(9/10) = 0, not past level 0
        const GameContext snap = game;
        checkForLevelUp(game);
        EXPECT_EQ(game, snap);
    }
    // At the threshold: level up, both timers reloaded, the cue set.
    {
        GameContext game;
        typeA(game);
        game.flow.level = 0;
        game.flow.lines = 10;  // floor(10/10) = 1 > 0
        checkForLevelUp(game);
        EXPECT_EQ(game.flow.level, 1);
        EXPECT_EQ(game.flow.framesPerDrop, kirpich::framesPerDrop(1, false));
        EXPECT_EQ(game.flow.dropTimer, kirpich::framesPerDrop(1, false));
        EXPECT_EQ(game.audioCues.square, SquareSfxId::LEVEL_UP);
    }
    // One level per call: a line count several tens ahead still advances only one level per call.
    {
        GameContext game;
        typeA(game);
        game.flow.level = 0;
        game.flow.lines = 25;  // floor = 2
        checkForLevelUp(game);
        EXPECT_EQ(game.flow.level, 1);
        checkForLevelUp(game);
        EXPECT_EQ(game.flow.level, 2);
        checkForLevelUp(game);  // floor(25/10) = 2, not past level 2
        EXPECT_EQ(game.flow.level, 2);
    }
    // The 1000-line cutoff: past 999 lines the game never levels again.
    {
        GameContext game;
        typeA(game);
        game.flow.level = 5;
        game.flow.lines = 1000;
        const GameContext snap = game;
        checkForLevelUp(game);
        EXPECT_EQ(game, snap);
    }
    // Heart mode shifts the gravity reload up.
    {
        GameContext game;
        typeA(game);
        game.flow.level = 0;
        game.flow.lines = 10;
        game.flow.heartMode = 1;
        checkForLevelUp(game);
        EXPECT_EQ(game.flow.level, 1);
        EXPECT_EQ(game.flow.framesPerDrop, kirpich::framesPerDrop(1, true));
        EXPECT_EQ(game.flow.dropTimer, kirpich::framesPerDrop(1, true));
    }
}

// 5. Wipe16WiringVectors — through playingFieldWipeTick: a qualifying Type A walk levels up exactly at
// step 16 and nowhere else (:5710-5718); a non-Type-A walk stays inert at 16; the counter still
// advances one step per call.
TEST(ScoringSystem, Wipe16WiringVectors) {
    // Type A, qualifying: level bumps only on the step-16 call.
    {
        GameContext game;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.level = 0;
        game.flow.lines = 10;  // qualifies for one level up
        game.flow.wipeCounter = 2;
        for (std::uint8_t step = 2; step <= 18; ++step) {
            const std::uint8_t levelBefore = game.flow.level;
            ASSERT_EQ(game.flow.wipeCounter, step);
            playingFieldWipeTick(game, noDraw);
            EXPECT_EQ(game.flow.wipeCounter, step + 1);  // one step per call
            if (step == 16) {
                EXPECT_EQ(game.flow.level, levelBefore + 1) << "level up fires at step 16";
                EXPECT_EQ(game.audioCues.square, SquareSfxId::LEVEL_UP);
                EXPECT_EQ(game.flow.framesPerDrop, kirpich::framesPerDrop(1, false));
            } else {
                EXPECT_EQ(game.flow.level, levelBefore) << "no level up at step " << int(step);
            }
        }
        EXPECT_EQ(game.flow.level, 1);  // exactly one level up over the whole walk
    }

    // Type B walk: step 16 is inert (the level-up check gates on Type A).
    {
        GameContext game;
        game.flow.gameState = GameState::NORMAL_GAMEPLAY;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.level = 0;
        game.flow.lines = 10;
        game.flow.wipeCounter = 16;
        playingFieldWipeTick(game, noDraw);
        EXPECT_EQ(game.flow.level, 0);
        EXPECT_EQ(game.flow.wipeCounter, 17);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
}

// 6. PrintLineClearScoresVectors — the four caller rows (each kind at row 1/4/7/10, col 5) at
// representative levels, decimal digits left-aligned with leading zeros suppressed as raw 0-9 bytes,
// cells outside the span untouched, and the score left alone (:4662-4706).
TEST(ScoringSystem, PrintLineClearScoresVectors) {
    struct Vec {
        LineClearKind kind;
        std::uint8_t row;
        std::uint8_t typeBLevel;
        std::vector<std::uint8_t> digits;
    };
    const std::array<Vec, 4> vectors = {{
        {LineClearKind::SINGLE, 1, 2, {1, 2, 0}},        // 40 * 3 = 120
        {LineClearKind::DOUBLE, 4, 0, {1, 0, 0}},        // 100 * 1 = 100
        {LineClearKind::TRIPLE, 7, 4, {1, 5, 0, 0}},     // 300 * 5 = 1500
        {LineClearKind::TETRIS, 10, 9, {1, 2, 0, 0, 0}},  // 1200 * 10 = 12000
    }};
    const std::uint8_t col = 5;
    const std::uint8_t marker = 0xEE;

    for (const Vec& v : vectors) {
        GameContext game;
        game.flow.typeBLevel = v.typeBLevel;
        game.engine.score = 4242;
        // Fill the whole board row with a marker so untouched cells (including the one just past a
        // full-width Tetris row) are detectable.
        game.field.board[v.row].fill(marker);
        printLineClearScores(game, v.kind, v.row, col);

        for (std::size_t i = 0; i < v.digits.size(); ++i) {
            EXPECT_EQ(game.field.fieldCell(v.row, col + i), v.digits[i])
                << "kind " << int(v.kind) << " digit " << i;
        }
        // The cell just past the last digit is untouched.
        EXPECT_EQ(game.field.fieldCell(v.row, col + v.digits.size()), marker);
        // A cell before the span is untouched.
        EXPECT_EQ(game.field.fieldCell(v.row, col - 1), marker);
        // The score is not touched.
        EXPECT_EQ(game.engine.score, 4242u);
    }
}

// 7. ClearScoreAndStatsVectors — a fully populated scoring state zeroes exactly the D9 span; the
// post-lock latch and the score-redraw flag survive, as do fields outside the span (:6191-6205).
TEST(ScoringSystem, ClearScoreAndStatsVectors) {
    GameContext game;
    game.engine.score = 123456;
    game.engine.stats = LineClearStats{5, 6, 7, 8};
    game.engine.scoreboardDisplayedStats = LineClearStats{1, 2, 3, 4};
    game.engine.softDropPoints = 99;
    game.engine.softDropPointsTallied = 42;
    game.engine.scoreboardState = 3;
    game.engine.scoreboardTallyPhase = 2;
    // Survivors, both inside EngineState but past the zeroed span.
    game.engine.blockSoftDropAfterLock = true;
    game.engine.scoreRedrawRequested = true;
    game.engine.hidePreviewPiece = true;

    clearScoreAndStats(game);

    EXPECT_EQ(game.engine.score, 0u);
    EXPECT_EQ(game.engine.stats, LineClearStats{});
    EXPECT_EQ(game.engine.scoreboardDisplayedStats, LineClearStats{});
    EXPECT_EQ(game.engine.softDropPoints, 0);
    EXPECT_EQ(game.engine.softDropPointsTallied, 0);
    EXPECT_EQ(game.engine.scoreboardState, 0);
    EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
    // Survivors untouched.
    EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
    EXPECT_TRUE(game.engine.scoreRedrawRequested);
    EXPECT_TRUE(game.engine.hidePreviewPiece);
}
