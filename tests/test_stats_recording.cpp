// Recording a round: what is counted, where it lands, what is left out, and how it is timed.
//
// The timed calls take the clock reading as a plain number, so every case here drives time by hand
// and nothing depends on how long the test takes to run.

#include <cstdint>

#include <gtest/gtest.h>

#include <kirpich/char_tile.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/piece_kind.h>

#include "data/sprites.h"  // SpriteId
#include "state/demo_state.h"
#include "state/sprite_renderer_state.h"  // kActivePieceSlot
#include "state/stats_state.h"
#include "systems/boot.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/piece.h"  // lockPieceIntoBackground
#include "systems/stats.h"

using kirpich::ActiveDemo;
using kirpich::GameType;
using kirpich::StatSlice;
using kirpich::StatsState;
using kirpich::systems::GameContext;

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000ULL;

// The spawn position and the empty cell, for the cases that drive a real lock.
constexpr std::uint8_t kSpawnY = 0x18;
constexpr std::uint8_t kSpawnX = 0x3F;
constexpr auto kSpace = static_cast<std::uint8_t>(kirpich::CharTile::SPACE);

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

// (21) Each shape is counted under its own kind, and the seven of them sum to the drop count exactly.
//
// Driven through the real lock rather than by calling the seams, because the placement IS the
// assertion: the count is taken where the drop is taken. Taken instead where a piece is promoted into
// the active slot it would run ahead of the drops, since the round init promotes several to prime the
// preview pipeline and those are pieces nobody played - and a player would then see two figures on
// the same screen that should agree and do not.
//
// It also covers the kind derivation, which is the slot's own piece byte shifted down: a piece byte
// is kind * 4 + rotation, so every rotation of a shape must count as that shape.
TEST(StatsRecording, EachShapeIsCountedUnderItsOwnKindAndTheySumToTheDrops) {
    GameContext game;
    selectTypeA(game, 3);
    kirpich::systems::beginRound(game, 0);

    // A different number of each shape, so a mapping that crossed two kinds cannot pass - and a
    // different rotation each time round, so the derivation is exercised rather than one facing.
    constexpr std::uint32_t kEach[kirpich::kPieceKindCount] = {1, 2, 3, 4, 5, 6, 7};
    std::uint32_t           dropped = 0;
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        for (std::uint32_t i = 0; i < kEach[kind]; ++i) {
            for (auto& row : game.field.board) row.fill(kSpace);

            auto& slot    = game.spriteRenderer.slots[kirpich::kActivePieceSlot];
            slot.spriteId = static_cast<kirpich::SpriteId>(kind * 4 + (i % 4));
            slot.y        = kSpawnY;
            slot.x        = kSpawnX;
            slot.hidden   = false;

            game.flow.pieceLockStage = 1;
            kirpich::systems::lockPieceIntoBackground(game);
            ++dropped;
        }
    }

    const StatSlice& slice = game.stats.typeA[3];
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        EXPECT_EQ(slice.pieces[kind], kEach[kind]) << "kind " << kind;
    }

    std::uint32_t summed = 0;
    for (const std::uint32_t count : slice.pieces) summed += count;
    EXPECT_EQ(summed, slice.drops) << "the per-kind counts and the drop count must agree";
    EXPECT_EQ(summed, dropped);

    // Nothing reached another slice.
    for (std::size_t level = 0; level < kirpich::kStatLevels; ++level) {
        if (level == 3) continue;
        for (const std::uint32_t count : game.stats.typeA[level].pieces) {
            EXPECT_EQ(count, 0u) << "level " << level;
        }
    }
}

// (22) Nothing is counted while no round is open, and a kind outside the seven is ignored rather than
// written past the end of the table.
TEST(StatsRecording, PieceCountsNeedALiveRoundAndAKnownShape) {
    GameContext game;
    selectTypeA(game, 0);

    // No round yet: the call is inert, as every other seam is.
    kirpich::systems::recordPiece(game, kirpich::PieceKind::T);
    EXPECT_TRUE(game.stats == StatsState{});

    // A demo runs the same pipeline; beginRound refuses, so the seams stay inert.
    game.demo.activeDemo = ActiveDemo::TYPE_A;
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::T);
    EXPECT_TRUE(game.stats == StatsState{}) << "an attract demo must count nothing";

    game.demo.activeDemo = ActiveDemo::NONE;
    kirpich::systems::beginRound(game, 0);
    const StatsState before = game.stats;
    kirpich::systems::recordPiece(game, static_cast<kirpich::PieceKind>(kirpich::kPieceKindCount));
    EXPECT_TRUE(game.stats == before) << "a shape outside the seven has nowhere to go";
}

// (23) The song a round is played under is counted at its start, and it is counted OUTSIDE the slice
// tables - music is not part of a combination, so there is nowhere in a slice for it to live.
//
// The unset byte the flow state holds before a selection has been made has no slot, and is not
// counted into one.
TEST(StatsRecording, TheMusicSelectionIsCountedBesideTheTables) {
    GameContext game;
    selectTypeA(game, 0);

    game.flow.musicType = kirpich::MusicType::MUSIC_B;
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::endRound(game, 0);

    const std::size_t b = kirpich::musicTypeIndex(kirpich::MusicType::MUSIC_B);
    EXPECT_EQ(game.stats.musicRounds[b], 1u);
    for (std::size_t music = 0; music < kirpich::kMusicTypeCount; ++music) {
        if (music == b) continue;
        EXPECT_EQ(game.stats.musicRounds[music], 0u) << "music " << music;
    }

    // Two more under a different selection, and the first one keeps its own count.
    game.flow.musicType = kirpich::MusicType::OFF;
    for (int i = 0; i < 2; ++i) {
        kirpich::systems::beginRound(game, 0);
        kirpich::systems::endRound(game, 0);
    }
    EXPECT_EQ(game.stats.musicRounds[b], 1u);
    EXPECT_EQ(game.stats.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::OFF)], 2u);

    // A byte outside the four is not one of them: the counts stay exactly as they were.
    const auto before   = game.stats.musicRounds;
    game.flow.musicType = static_cast<kirpich::MusicType>(0x00);
    kirpich::systems::beginRound(game, 0);
    EXPECT_EQ(game.stats.musicRounds, before) << "an unset selection has no slot to count into";
    EXPECT_TRUE(game.stats.round.active) << "and the round still starts";

    // A demo is excluded here too, for the same reason it is everywhere else.
    kirpich::systems::endRound(game, 0);
    const auto quiet     = game.stats.musicRounds;
    game.demo.activeDemo = ActiveDemo::TYPE_B;
    game.flow.musicType  = kirpich::MusicType::MUSIC_A;
    kirpich::systems::beginRound(game, 0);
    EXPECT_EQ(game.stats.musicRounds, quiet) << "an attract demo must count no music either";
}

// (24) The folds carry the piece counts up with everything else: a game type's totals are the sum of
// its slices' per-kind counts, and the lifetime totals the sum of all three types'.
TEST(StatsRecording, TheFoldsSumThePieceCountsToo) {
    GameContext game;

    // One piece of a known kind in each of three slices, one per game type.
    selectTypeA(game, 1);
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::I);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::I);
    kirpich::systems::endRound(game, 0);

    selectTypeB(game, 2, 3);
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::I);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::O);
    kirpich::systems::endRound(game, 0);

    selectTypeC(game, 4, 5);
    kirpich::systems::beginRound(game, 0);
    kirpich::systems::recordPiece(game, kirpich::PieceKind::O);
    kirpich::systems::endRound(game, 0);

    constexpr auto kI = static_cast<std::size_t>(kirpich::PieceKind::I);
    constexpr auto kO = static_cast<std::size_t>(kirpich::PieceKind::O);

    EXPECT_EQ(kirpich::systems::totalsFor(game.stats, GameType::TYPE_A).pieces[kI], 2u);
    EXPECT_EQ(kirpich::systems::totalsFor(game.stats, GameType::TYPE_B).pieces[kI], 1u);
    EXPECT_EQ(kirpich::systems::totalsFor(game.stats, GameType::TYPE_B).pieces[kO], 1u);
    EXPECT_EQ(kirpich::systems::totalsFor(game.stats, GameType::TYPE_C).pieces[kO], 1u);

    const StatSlice lifetime = kirpich::systems::lifetimeTotals(game.stats);
    EXPECT_EQ(lifetime.pieces[kI], 3u);
    EXPECT_EQ(lifetime.pieces[kO], 2u);
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        if (kind == kI || kind == kO) continue;
        EXPECT_EQ(lifetime.pieces[kind], 0u) << "kind " << kind << " was never played";
    }
}

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
