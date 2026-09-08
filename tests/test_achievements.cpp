// Achievements: the round-end check that awards the set.
//
// Each condition shape gets a behavioural case that proves it fires exactly when its criterion is met
// and not one notch below, with a fixed clock so the unlock stamp is pinned. The demo exclusion and
// the first-unlock-wins idempotency each get their own case, both of which redden under a broken gate.

#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/game_type.h>

#include "state/achievement_state.h"
#include "state/stats_state.h"
#include "systems/achievements.h"
#include "systems/game_context.h"

using namespace kirpich;
using namespace kirpich::systems;

namespace {

NowDate fixedDate(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    return [=] { return AchievementDate{.year = year, .month = month, .day = day}; };
}

// How a round is set up for the check: its combination, its final numbers, and the observations the
// scene and clear seams would have gathered.
struct Round {
    GameType      type       = GameType::TYPE_A;
    std::uint8_t  typeALevel = 0;
    std::uint8_t  typeBLevel = 0;
    std::uint8_t  typeBHeight = 0;
    std::uint8_t  typeCRise  = 0;
    std::uint8_t  heartByte  = 0;
    std::uint32_t score      = 0;
    std::uint16_t lines      = 0;
    std::uint8_t  level      = 0;
    bool          won        = false;
    bool          rocket     = false;
    bool          topRocket  = false;
    bool          buran      = false;
    std::uint32_t tetrises   = 0;
    bool          demo       = false;  // a demo never arms the round; nothing should unlock
};

// Play the round through the real seams and run the check.
void run(GameContext& game, const Round& r, const NowDate& date) {
    game.flow.gameType         = r.type;
    game.flow.typeALevel       = r.typeALevel;
    game.flow.typeBLevel       = r.typeBLevel;
    game.flow.typeBStartHeight = r.typeBHeight;
    game.flow.typeCRise        = r.typeCRise;
    game.flow.heartMode        = r.heartByte;
    game.engine.score          = r.score;
    game.flow.lines            = r.lines;
    game.flow.level            = r.level;

    // A real round arms the observations; a demo is turned away before this, so it never does.
    if (!r.demo) beginAchievementRound(game.achievements);
    for (std::uint32_t i = 0; i < r.tetrises; ++i) noteRoundTetris(game.achievements);
    if (r.rocket) noteRocketScene(game.achievements, r.topRocket);
    if (r.buran) noteBuranScene(game.achievements);
    noteRoundConcluded(game.achievements, r.won);

    evaluateRoundEnd(game, date);
}

bool has(const GameContext& game, AchievementId id) {
    return game.achievements.unlocked[achievementIndex(id)].unlocked;
}

}  // namespace

// (1) ScoreAtLeast fires at the threshold, not below, and only for the named game type.
TEST(Achievements, ScoreAtLeastFiresAtThresholdInTheRightType) {
    const NowDate date = fixedDate(2026, 9, 7);

    {  // one below
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .score = 4999}, date);
        EXPECT_FALSE(has(game, AchievementId::GETTING_WARM));
    }
    {  // at the threshold
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .score = 5000}, date);
        EXPECT_TRUE(has(game, AchievementId::GETTING_WARM));
    }
    {  // the same score in another type does not count for a Type A achievement
        GameContext game;
        run(game, {.type = GameType::TYPE_C, .score = 5000}, date);
        EXPECT_FALSE(has(game, AchievementId::GETTING_WARM));
    }
}

// (2) A Type C score achievement with a rise floor: the score alone earns the plain one, and the score
// on the fastest rise earns the gated one.
TEST(Achievements, TypeCScoreWithARiseFloor) {
    const NowDate date = fixedDate(2026, 9, 7);

    {  // 30 000 at a slower rise: HOLD GROUND, not BEDROCK
        GameContext game;
        run(game, {.type = GameType::TYPE_C, .typeCRise = 4, .score = 30000}, date);
        EXPECT_TRUE(has(game, AchievementId::HOLD_GROUND));
        EXPECT_FALSE(has(game, AchievementId::BEDROCK));
    }
    {  // 30 000 on the fastest rise (index 5): both
        GameContext game;
        run(game, {.type = GameType::TYPE_C, .typeCRise = 5, .score = 30000}, date);
        EXPECT_TRUE(has(game, AchievementId::HOLD_GROUND));
        EXPECT_TRUE(has(game, AchievementId::BEDROCK));
    }
}

// (3) LinesInRoundAtLeast reads the round's final line count in Type A.
TEST(Achievements, LinesInRoundFiresAtThreshold) {
    const NowDate date = fixedDate(2026, 9, 7);
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .lines = 49}, date);
        EXPECT_FALSE(has(game, AchievementId::LINE_WORKER));
    }
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .lines = 50}, date);
        EXPECT_TRUE(has(game, AchievementId::LINE_WORKER));
    }
}

// (4) TetrisesInRound counts the four-line clears observed this round.
TEST(Achievements, TetrisesInRoundCountsThisRoundsFours) {
    const NowDate date = fixedDate(2026, 9, 7);
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .tetrises = 9}, date);
        EXPECT_FALSE(has(game, AchievementId::TETRIS_TIMES_TEN));
    }
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .tetrises = 10}, date);
        EXPECT_TRUE(has(game, AchievementId::TETRIS_TIMES_TEN));
    }
}

// (5) The lifetime folds read the slice tables (both cartridge and heart), updated with this round.
TEST(Achievements, LifetimeFoldsReadTheSliceTables) {
    const NowDate date = fixedDate(2026, 9, 7);

    {  // lifetime lines, at and below 10 000
        GameContext game;
        game.stats.typeA[0].lines = 9999;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_FALSE(has(game, AchievementId::TEN_THOUSAND));

        GameContext game2;
        game2.stats.typeA[0].lines = 10000;
        run(game2, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game2, AchievementId::TEN_THOUSAND));
    }
    {  // lifetime tetrises, folded across a heart slice too
        GameContext game;
        game.stats.typeBHeart[0][0].tetrises = 1;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game, AchievementId::FOUR_AT_ONCE));
    }
    {  // lifetime drops
        GameContext game;
        game.stats.typeC[3][2].drops = 100000;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game, AchievementId::PIECE_BY_PIECE));
    }
    {  // lifetime rounds
        GameContext game;
        game.stats.typeA[0].rounds = 50;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game, AchievementId::REGULAR));
    }
}

// (6) A Type B win: at level 9 and a start height at or above the rung, and never on a top-out.
TEST(Achievements, TypeBWinLaddersByHeightAndRequiresAWin) {
    const NowDate date = fixedDate(2026, 9, 7);

    {  // won at level 9, height 1: the first rung, not the second
        GameContext game;
        run(game, {.type = GameType::TYPE_B, .typeBLevel = 9, .typeBHeight = 1, .won = true}, date);
        EXPECT_TRUE(has(game, AchievementId::DUG_OUT));
        EXPECT_FALSE(has(game, AchievementId::IN_DEEP));
    }
    {  // the top rung is height 5
        GameContext game;
        run(game, {.type = GameType::TYPE_B, .typeBLevel = 9, .typeBHeight = 5, .won = true}, date);
        EXPECT_TRUE(has(game, AchievementId::MIRACLE_DIG));
    }
    {  // a top-out at the same combination earns nothing
        GameContext game;
        run(game, {.type = GameType::TYPE_B, .typeBLevel = 9, .typeBHeight = 5, .won = false}, date);
        EXPECT_FALSE(has(game, AchievementId::DUG_OUT));
        EXPECT_FALSE(has(game, AchievementId::MIRACLE_DIG));
    }
    {  // a win below level 9 does not count
        GameContext game;
        run(game, {.type = GameType::TYPE_B, .typeBLevel = 8, .typeBHeight = 5, .won = true}, date);
        EXPECT_FALSE(has(game, AchievementId::DUG_OUT));
    }
}

// (7) LevelReached reads the level the round climbed to, in any game type.
TEST(Achievements, LevelReachedReadsTheClimbedLevel) {
    const NowDate date = fixedDate(2026, 9, 7);
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .level = 14}, date);
        EXPECT_FALSE(has(game, AchievementId::NINTH_GEAR));
    }
    {
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .level = 15}, date);
        EXPECT_TRUE(has(game, AchievementId::NINTH_GEAR));
    }
}

// (8) Heart conditions read the round's heart flag, and combine with score.
TEST(Achievements, HeartConditionsReadTheHeartFlag) {
    const NowDate date = fixedDate(2026, 9, 7);
    {  // a plain finished round earns nothing heart-shaped
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .heartByte = 0, .score = 100000}, date);
        EXPECT_FALSE(has(game, AchievementId::HAVE_A_HEART));
        EXPECT_FALSE(has(game, AchievementId::HEART_OF_STEEL));
    }
    {  // a heart round finished
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .heartByte = 1, .score = 100000}, date);
        EXPECT_TRUE(has(game, AchievementId::HAVE_A_HEART));
        EXPECT_TRUE(has(game, AchievementId::HEART_OF_STEEL));  // heart + 100 000 in Type A
    }
    {  // 100 000 without heart does not earn the heart score badge
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .heartByte = 0, .score = 100000}, date);
        EXPECT_FALSE(has(game, AchievementId::HEART_OF_STEEL));
    }
}

// (9) SceneReached reads the observed scene flags, and the top-tier rocket is distinct from any rocket.
TEST(Achievements, SceneReachedReadsTheObservedScenes) {
    const NowDate date = fixedDate(2026, 9, 7);
    {  // a lower-tier rocket: LIFTOFF only
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .rocket = true, .topRocket = false}, date);
        EXPECT_TRUE(has(game, AchievementId::LIFTOFF));
        EXPECT_FALSE(has(game, AchievementId::ESCAPE_VELOCITY));
    }
    {  // the top-tier rocket: both
        GameContext game;
        run(game, {.type = GameType::TYPE_A, .rocket = true, .topRocket = true}, date);
        EXPECT_TRUE(has(game, AchievementId::LIFTOFF));
        EXPECT_TRUE(has(game, AchievementId::ESCAPE_VELOCITY));
    }
    {  // the Buran
        GameContext game;
        run(game, {.type = GameType::TYPE_B, .typeBLevel = 9, .typeBHeight = 5, .won = true,
                   .buran = true},
            date);
        EXPECT_TRUE(has(game, AchievementId::COSMONAUT));
    }
}

// (10) The cross-mode folds: all three types played, all music heard, and total play time.
TEST(Achievements, CrossModeAndTimeFolds) {
    const NowDate date = fixedDate(2026, 9, 7);
    {  // two of three types is not enough; the third completes it
        GameContext game;
        game.stats.typeA[0].rounds = 1;
        game.stats.typeB[0][0].rounds = 1;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_FALSE(has(game, AchievementId::WELL_ROUNDED));

        GameContext game2;
        game2.stats.typeA[0].rounds    = 1;
        game2.stats.typeB[0][0].rounds = 1;
        game2.stats.typeC[0][0].rounds = 1;
        run(game2, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game2, AchievementId::WELL_ROUNDED));
    }
    {  // every music selection heard
        GameContext game;
        for (std::size_t m = 0; m < kMusicTypeCount; ++m) game.stats.musicRounds[m] = 1;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game, AchievementId::ALL_EARS));

        GameContext game2;
        for (std::size_t m = 0; m < kMusicTypeCount; ++m) game2.stats.musicRounds[m] = 1;
        game2.stats.musicRounds[0] = 0;  // one never heard
        run(game2, {.type = GameType::TYPE_A}, date);
        EXPECT_FALSE(has(game2, AchievementId::ALL_EARS));
    }
    {  // whole-application play time
        GameContext game;
        game.stats.applicationSeconds = 86400;
        run(game, {.type = GameType::TYPE_A}, date);
        EXPECT_TRUE(has(game, AchievementId::TIME_SINK));
    }
}

// (11) An attract demo earns nothing: it never arms the round, so the check never runs for it.
//      (Red under a noteRoundConcluded that ignores the armed gate.)
TEST(Achievements, ADemoEarnsNothing) {
    const NowDate date = fixedDate(2026, 9, 7);
    GameContext   game;
    run(game, {.type = GameType::TYPE_A, .score = 500000, .demo = true}, date);
    EXPECT_EQ(achievementsUnlocked(game), 0u);
    EXPECT_FALSE(has(game, AchievementId::KIRPICH_LEGEND));
}

// (12) First unlock wins: a later round that meets the same criterion never rewrites the stamp.
//      (Red under an evaluator that does not skip an already-unlocked record.)
TEST(Achievements, FirstUnlockWinsAndKeepsItsStamp) {
    GameContext game;

    run(game, {.type = GameType::TYPE_A, .score = 5000}, fixedDate(2026, 1, 2));
    const AchievementUnlock first = game.achievements.unlocked[achievementIndex(AchievementId::GETTING_WARM)];
    ASSERT_TRUE(first.unlocked);
    EXPECT_EQ(first.datePacked, 20260102u);

    run(game, {.type = GameType::TYPE_A, .score = 6000}, fixedDate(2026, 12, 31));
    const AchievementUnlock again = game.achievements.unlocked[achievementIndex(AchievementId::GETTING_WARM)];
    EXPECT_EQ(again, first);  // unchanged: the first stamp stands
}

// (13) An unlock records the calendar date and the play time at the moment it fired.
TEST(Achievements, AnUnlockRecordsTheDateAndPlayTime) {
    GameContext game;
    game.stats.applicationSeconds = 4321;
    run(game, {.type = GameType::TYPE_A, .score = 5000}, fixedDate(2026, 9, 7));

    const AchievementUnlock& record =
        game.achievements.unlocked[achievementIndex(AchievementId::GETTING_WARM)];
    EXPECT_TRUE(record.unlocked);
    EXPECT_EQ(record.datePacked, 20260907u);
    EXPECT_EQ(record.playSecondsAtUnlock, 4321u);
}

// (14) The check consumes the round: a second exit's call finds nothing pending and awards nothing new.
TEST(Achievements, TheCheckRunsOncePerConcludedRound) {
    const NowDate date = fixedDate(2026, 9, 7);
    GameContext   game;
    run(game, {.type = GameType::TYPE_A, .score = 5000}, date);
    ASSERT_TRUE(has(game, AchievementId::GETTING_WARM));
    EXPECT_EQ(game.achievements.round, AchievementRound{});  // cleared

    // A second fire with a higher score but no fresh round does nothing.
    game.engine.score = 25000;
    evaluateRoundEnd(game, date);
    EXPECT_FALSE(has(game, AchievementId::STACKING_UP));
}

// (15) The count accessors: the total matches the set size, and the unlocked count tracks unlocks.
TEST(Achievements, CountAccessorsMatchTheSet) {
    EXPECT_EQ(achievementsTotal(), kAchievementCount);
    EXPECT_EQ(achievementSet().size(), kAchievementCount);

    GameContext game;
    EXPECT_EQ(achievementsUnlocked(game), 0u);
    run(game, {.type = GameType::TYPE_A, .score = 5000, .lines = 50}, fixedDate(2026, 9, 7));
    // GETTING WARM (score) and LINE WORKER (lines) both fire from this one round.
    EXPECT_EQ(achievementsUnlocked(game), 2u);
}
