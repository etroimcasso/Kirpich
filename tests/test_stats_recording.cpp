// Recording a round: what is counted, where it lands, what is left out, and how it is timed.
//
// The timed calls take the clock reading as a plain number, so every case here drives time by hand
// and nothing depends on how long the test takes to run.

#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/game_type.h>

#include "state/demo_state.h"
#include "state/stats_state.h"
#include "systems/boot.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/stats.h"

using kirpich::ActiveDemo;
using kirpich::GameType;
using kirpich::StatSlice;
using kirpich::StatsState;
using kirpich::systems::GameContext;

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000ULL;

// A draw that always returns the same piece. The round init needs a randomizer; which piece it hands
// back does not matter to anything counted here.
std::uint8_t drawNothing() { return 0; }

void selectTypeA(GameContext& game, std::uint8_t level) {
    game.flow.gameType   = GameType::TYPE_A;
    game.flow.typeALevel = level;
}

void selectTypeB(GameContext& game, std::uint8_t level, std::uint8_t height) {
    game.flow.gameType         = GameType::TYPE_B;
    game.flow.typeBLevel       = level;
    game.flow.typeBStartHeight = height;
}

void selectTypeC(GameContext& game, std::uint8_t level, std::uint8_t rise) {
    game.flow.gameType   = GameType::TYPE_C;
    game.flow.typeCLevel = level;
    game.flow.typeCRise  = rise;
}

// Every slice in the state except the one named, so a case can assert a count landed nowhere else.
std::uint32_t roundsEverywhereExcept(const StatsState& stats, const StatSlice& except) {
    std::uint32_t total = 0;
    for (const auto& slice : stats.typeA) {
        if (&slice != &except) total += slice.rounds;
    }
    for (const auto& level : stats.typeB) {
        for (const auto& slice : level) {
            if (&slice != &except) total += slice.rounds;
        }
    }
    for (const auto& level : stats.typeC) {
        for (const auto& slice : level) {
            if (&slice != &except) total += slice.rounds;
        }
    }
    return total;
}

}  // namespace

// (11) The round latches the combination it was started at, and Type A carries no second value.
TEST(StatsRecording, BeginRoundLatchesTheCombination) {
    GameContext game;

    selectTypeA(game, 6);
    kirpich::systems::beginRound(game, 0);
    EXPECT_TRUE(game.stats.round.active);
    EXPECT_EQ(game.stats.round.type, GameType::TYPE_A);
    EXPECT_EQ(game.stats.round.level, 6);
    EXPECT_FALSE(game.stats.round.hasVariant);

    selectTypeB(game, 4, 5);
    kirpich::systems::beginRound(game, 0);
    EXPECT_EQ(game.stats.round.type, GameType::TYPE_B);
    EXPECT_EQ(game.stats.round.level, 4);
    EXPECT_EQ(game.stats.round.variant, 5);
    EXPECT_TRUE(game.stats.round.hasVariant);

    selectTypeC(game, 9, 2);
    kirpich::systems::beginRound(game, 0);
    EXPECT_EQ(game.stats.round.type, GameType::TYPE_C);
    EXPECT_EQ(game.stats.round.level, 9);
    EXPECT_EQ(game.stats.round.variant, 2);
    EXPECT_TRUE(game.stats.round.hasVariant);
}

// (12) A round left open closes into the combination it was STARTED in, not the one selected by the
// time it is closed. This is what the latch is for: reading the flow at the end would file an
// abandoned round under whatever the player picked next.
TEST(StatsRecording, AnOpenRoundClosesIntoItsOwnCombination) {
    GameContext game;

    selectTypeB(game, 3, 2);
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::recordDrop(game);

    // The player leaves without the round ending, and picks something else.
    selectTypeC(game, 7, 4);
    kirpich::systems::beginRound(game, 10 * kSecond);

    EXPECT_EQ(game.stats.typeB[3][2].rounds, 1u) << "the abandoned round is its own";
    EXPECT_EQ(game.stats.typeB[3][2].drops, 1u);
    EXPECT_EQ(game.stats.typeB[3][2].seconds, 10u);
    EXPECT_EQ(game.stats.typeC[7][4].rounds, 0u) << "the new round has not finished yet";
    EXPECT_EQ(game.stats.round.type, GameType::TYPE_C);
}

// (13) Drops and clears land on the latched slice, and on no other slice in the table.
TEST(StatsRecording, DropsAndClearsLandOnTheLatchedSliceOnly) {
    GameContext game;
    selectTypeB(game, 3, 2);
    kirpich::systems::beginRound(game, 0);

    kirpich::systems::recordDrop(game);
    kirpich::systems::recordDrop(game);
    kirpich::systems::recordLineClear(game, 1);
    kirpich::systems::recordLineClear(game, 4);
    kirpich::systems::recordLineClear(game, 2);

    const StatSlice& slice = game.stats.typeB[3][2];
    EXPECT_EQ(slice.drops, 2u);
    EXPECT_EQ(slice.lines, 7u) << "one plus four plus two";
    EXPECT_EQ(slice.singles, 1u);
    EXPECT_EQ(slice.doubles, 1u);
    EXPECT_EQ(slice.triples, 0u);
    EXPECT_EQ(slice.tetrises, 1u);

    kirpich::systems::endRound(game, 0);
    EXPECT_EQ(roundsEverywhereExcept(game.stats, slice), 0u)
        << "a count reached a slice the round was not played in";
}

// (14) An attract demo plays through the same round pipeline, and records nothing. The refusal is at
// the start, so every later call is inert on its own.
TEST(StatsRecording, ADemoRecordsNothing) {
    GameContext game;
    selectTypeA(game, 5);
    game.demo.activeDemo = ActiveDemo::TYPE_A;

    kirpich::systems::initGame(game, drawNothing);
    EXPECT_FALSE(game.stats.round.active) << "a demo started a round";

    kirpich::systems::recordDrop(game);
    kirpich::systems::recordLineClear(game, 4);
    kirpich::systems::endRound(game, 30 * kSecond);
    EXPECT_TRUE(game.stats == StatsState{}) << "a demo reached the tables";

    // The same init, with no demo running, does start one - so the gate is the demo and not the seam.
    game.demo.activeDemo = ActiveDemo::NONE;
    kirpich::systems::initGame(game, drawNothing);
    EXPECT_TRUE(game.stats.round.active);
}

// (15) Paused time is not played time, and several pause cycles compose without losing anything. Each
// stretch here is half a second: banking whole seconds would record none of them.
TEST(StatsRecording, PausedTimeIsNotPlayedTime) {
    GameContext game;
    selectTypeA(game, 0);

    std::uint64_t now = 0;
    kirpich::systems::beginRound(game, now);

    for (int cycle = 0; cycle < 10; ++cycle) {
        now += kSecond / 2;                          // played
        kirpich::systems::pauseRound(game, now);
        now += 60 * kSecond;                         // paused, and not counted
        kirpich::systems::resumeRound(game, now);
    }
    kirpich::systems::endRound(game, now);

    EXPECT_EQ(game.stats.typeA[0].seconds, 5u) << "ten half-seconds of play";
    EXPECT_EQ(game.stats.typeA[0].longestRoundSeconds, 5u);
}

// (16) Ending a round twice counts it once.
TEST(StatsRecording, EndRoundIsIdempotent) {
    GameContext game;
    selectTypeA(game, 1);
    kirpich::systems::beginRound(game, 0);

    kirpich::systems::endRound(game, 5 * kSecond);

    // The whole state after the first call, so a later one cannot record anywhere at all - an
    // unguarded second call lands on a slice this round was never played in.
    const StatsState afterFirst = game.stats;

    kirpich::systems::endRound(game, 90 * kSecond);
    kirpich::systems::endRound(game, 90 * kSecond);

    EXPECT_EQ(game.stats.typeA[1].rounds, 1u);
    EXPECT_EQ(game.stats.typeA[1].seconds, 5u);
    EXPECT_TRUE(game.stats == afterFirst) << "a later call recorded something";
}

// (17) Quitting during a round records it: the close-out the exit guard performs counts the round and
// carries what it had earned.
TEST(StatsRecording, QuittingMidRoundStillRecordsTheRound) {
    GameContext game;
    selectTypeC(game, 2, 1);
    kirpich::systems::beginRound(game, 0);

    kirpich::systems::recordDrop(game);
    kirpich::systems::recordLineClear(game, 3);
    game.engine.score = 4321;

    // What the exit guard does.
    kirpich::systems::endRound(game, 45 * kSecond);

    const StatSlice& slice = game.stats.typeC[2][1];
    EXPECT_EQ(slice.rounds, 1u);
    EXPECT_EQ(slice.seconds, 45u);
    EXPECT_EQ(slice.longestRoundSeconds, 45u);
    EXPECT_EQ(slice.drops, 1u);
    EXPECT_EQ(slice.lines, 3u);
    EXPECT_EQ(slice.triples, 1u);
    EXPECT_EQ(slice.score, 4321u);
}

// (18) The longest round is the played time, so a long pause does not win it, and a later shorter
// round does not displace it.
TEST(StatsRecording, TheLongestRoundExcludesPauses) {
    GameContext game;
    selectTypeA(game, 0);

    std::uint64_t now = 0;
    kirpich::systems::beginRound(game, now);
    now += 20 * kSecond;
    kirpich::systems::pauseRound(game, now);
    now += 600 * kSecond;
    kirpich::systems::resumeRound(game, now);
    now += 10 * kSecond;
    kirpich::systems::endRound(game, now);

    EXPECT_EQ(game.stats.typeA[0].longestRoundSeconds, 30u) << "ten minutes paused is not played";

    kirpich::systems::beginRound(game, now);
    now += 5 * kSecond;
    kirpich::systems::endRound(game, now);

    EXPECT_EQ(game.stats.typeA[0].rounds, 2u);
    EXPECT_EQ(game.stats.typeA[0].seconds, 35u);
    EXPECT_EQ(game.stats.typeA[0].longestRoundSeconds, 30u) << "a shorter round does not displace it";
}

// (19) The program's own total carries the part of a second that does not yet make a whole one, so a
// run of short banks adds up to what one long one would.
TEST(StatsRecording, ApplicationSecondsCarryTheRemainder) {
    GameContext game;
    kirpich::systems::beginSession(game, 0);

    std::uint64_t now = 0;
    for (int bank = 0; bank < 7; ++bank) {
        now += kSecond / 2;
        kirpich::systems::bankApplicationTime(game, now);
    }

    EXPECT_EQ(game.stats.applicationSeconds, 3u) << "seven half-seconds is three whole ones";

    now += kSecond / 2;
    kirpich::systems::bankApplicationTime(game, now);
    EXPECT_EQ(game.stats.applicationSeconds, 4u) << "the carried half completed the fourth";
}

// (20) The statistics outlive the reset chord, as the top scores do, and a cold boot clears them.
TEST(StatsRecording, SoftResetKeepsTheStatisticsAndColdBootDoesNot) {
    GameContext game;
    game.stats.typeB[1][2].rounds = 9;
    game.stats.applicationSeconds = 4242;
    game.stats.applicationStampNanos = 77 * kSecond;

    kirpich::systems::softReset(game);
    EXPECT_EQ(game.stats.typeB[1][2].rounds, 9u);
    EXPECT_EQ(game.stats.applicationSeconds, 4242u);
    EXPECT_EQ(game.stats.applicationStampNanos, 77 * kSecond)
        << "a zeroed stamp would make the next reading measure from the clock's origin";

    kirpich::systems::coldBoot(game);
    EXPECT_TRUE(game.stats == StatsState{});
}
