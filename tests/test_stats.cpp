// Statistics: the table, its wire codec, the folds that read it back, and the duration text.
//
// The tables are the port's own - no cartridge address, no fixture to check against - so these cases
// are written against the contract the header states: what a slice holds, what the image looks like,
// how a rollup is computed from the slices, and which slice a longest round names.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/save_store.h>

#include "state/high_score_persistence.h"
#include "state/stats_persistence.h"
#include "state/stats_state.h"
#include "systems/stats.h"

using kirpich::GameType;
using kirpich::StatSlice;
using kirpich::StatsState;

namespace {

// A slice whose every field is distinct and derived from its position, so a codec that transposes two
// indices or two fields cannot round-trip by accident.
StatSlice sliceAt(std::uint32_t seed) {
    return StatSlice{.rounds              = seed + 1,
                     .seconds             = seed + 2,
                     .longestRoundSeconds = seed + 3,
                     .drops               = seed + 4,
                     .score               = seed + 5,
                     .lines               = seed + 6,
                     .singles             = seed + 7,
                     .doubles             = seed + 8,
                     .triples             = seed + 9,
                     .tetrises            = seed + 10};
}

StatsState populated() {
    StatsState stats;
    std::uint32_t seed = 100;
    for (std::size_t level = 0; level < kirpich::kStatLevels; ++level) {
        stats.typeA[level] = sliceAt(seed);
        seed += 100;
        for (std::size_t variant = 0; variant < kirpich::kStatVariants; ++variant) {
            stats.typeB[level][variant] = sliceAt(seed);
            seed += 100;
            stats.typeC[level][variant] = sliceAt(seed);
            seed += 100;
        }
    }
    stats.applicationSeconds = 987654;
    return stats;
}

}  // namespace

// (1) A default StatsState is empty, and reset() returns a populated one to that.
TEST(Stats, BootStateIsEmptyAndResetReturnsToIt) {
    StatsState boot;
    for (const auto& slice : boot.typeA) EXPECT_TRUE(slice == StatSlice{});
    for (const auto& level : boot.typeB) {
        for (const auto& slice : level) EXPECT_TRUE(slice == StatSlice{});
    }
    for (const auto& level : boot.typeC) {
        for (const auto& slice : level) EXPECT_TRUE(slice == StatSlice{});
    }
    EXPECT_EQ(boot.applicationSeconds, 0u);
    EXPECT_FALSE(boot.round.active);

    StatsState live = populated();
    live.round.active = true;
    live.reset();
    EXPECT_TRUE(live == StatsState{});
}

// (2) The codec round-trips every field of every slice, and the application total with them.
TEST(Stats, CodecRoundTripsEveryField) {
    const StatsState saved = populated();

    const auto image = kirpich::encodeStats(saved);
    StatsState loaded;
    ASSERT_TRUE(kirpich::decodeStats(image, loaded));

    EXPECT_TRUE(loaded.typeA == saved.typeA);
    EXPECT_TRUE(loaded.typeB == saved.typeB);
    EXPECT_TRUE(loaded.typeC == saved.typeC);
    EXPECT_EQ(loaded.applicationSeconds, saved.applicationSeconds);
}

// (3) The image is the size the header states, and its blocks are in the top-score document's order:
// Type B, then Type A, then Type C, then the application total.
TEST(Stats, WireImageSizeAndBlockOrder) {
    EXPECT_EQ(kirpich::kStatSliceBytes, 40u);
    EXPECT_EQ(kirpich::kStatsImageBytes, 5204u);
    EXPECT_EQ(kirpich::kStatsImageBytes, kirpich::kStatsTypeBBytes + kirpich::kStatsTypeABytes +
                                             kirpich::kStatsTypeCBytes +
                                             kirpich::kStatsApplicationBytes);

    StatsState stats;
    stats.typeB[0][0].rounds  = 0x11223344;
    stats.typeA[0].rounds     = 0x55667788;
    stats.typeC[0][0].rounds  = 0x99AABBCC;
    stats.applicationSeconds  = 0x0D0C0B0A;

    const auto image = kirpich::encodeStats(stats);

    // Each block's first slice starts where the block does, little-endian.
    const auto readAt = [&image](std::size_t at) {
        return static_cast<std::uint32_t>(image[at]) |
               (static_cast<std::uint32_t>(image[at + 1]) << 8) |
               (static_cast<std::uint32_t>(image[at + 2]) << 16) |
               (static_cast<std::uint32_t>(image[at + 3]) << 24);
    };
    EXPECT_EQ(readAt(0), 0x11223344u) << "the Type B block comes first";
    EXPECT_EQ(readAt(kirpich::kStatsTypeBBytes), 0x55667788u) << "then Type A";
    EXPECT_EQ(readAt(kirpich::kStatsTypeBBytes + kirpich::kStatsTypeABytes), 0x99AABBCCu)
        << "then Type C";
    EXPECT_EQ(readAt(kirpich::kStatsImageBytes - 4), 0x0D0C0B0Au)
        << "and the application total last";
}

// (4) An image of any other length is refused, and the state it was handed is left alone.
TEST(Stats, WrongLengthImageIsRefusedAndLeavesTheStateAlone) {
    const StatsState before = populated();

    for (const std::size_t size : {std::size_t{0}, std::size_t{1},
                                   kirpich::kStatsImageBytes - 1, kirpich::kStatsImageBytes + 1}) {
        std::vector<std::uint8_t> image(size, 0xFF);
        StatsState state = before;
        EXPECT_FALSE(kirpich::decodeStats(image, state)) << "accepted an image of " << size;
        EXPECT_TRUE(state == before) << "wrote into the state while refusing, at " << size;
    }
}

// (5) Through a hermetic store: absent leaves the boot zeros, a save round-trips, and a corrupt
// document leaves the zeros with the damaged file still on disk.
TEST(Stats, StoreRoundTripAbsentAndCorrupt) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_stats_store_roundtrip";
    std::filesystem::remove_all(root);

    {
        auto       store = retropp::SaveStore::atPath(root);
        StatsState state;
        EXPECT_FALSE(kirpich::loadStats(store, state));
        EXPECT_TRUE(state == StatsState{});
    }

    {
        auto             store = retropp::SaveStore::atPath(root);
        const StatsState saved = populated();
        ASSERT_TRUE(kirpich::saveStats(saved, store));

        StatsState loaded;
        ASSERT_TRUE(kirpich::loadStats(store, loaded));
        EXPECT_TRUE(loaded.typeA == saved.typeA);
        EXPECT_TRUE(loaded.typeB == saved.typeB);
        EXPECT_TRUE(loaded.typeC == saved.typeC);
        EXPECT_EQ(loaded.applicationSeconds, saved.applicationSeconds);
    }

    {
        auto store      = retropp::SaveStore::atPath(root);
        bool corrupted = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (!entry.is_regular_file()) continue;
            std::ofstream(entry.path(), std::ios::binary | std::ios::trunc).put('\x01');
            corrupted = true;
        }
        ASSERT_TRUE(corrupted) << "no save document on disk to corrupt";

        StatsState state;
        EXPECT_FALSE(kirpich::loadStats(store, state));
        EXPECT_TRUE(state == StatsState{});

        bool stillPresent = false;
        for (const auto& entry : std::filesystem::directory_iterator(store.basePath())) {
            if (entry.is_regular_file()) stillPresent = true;
        }
        EXPECT_TRUE(stillPresent) << "the damaged file was removed";
    }
}

// (6) The schema version is the store's, not the document's, so two documents in one store each have
// to name their own before their own read. Written together and read back together: if either loader
// left the other's version standing, one of these comes back wrong.
TEST(Stats, TwoDocumentsInOneStoreKeepTheirOwnVersions) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_stats_two_documents";
    std::filesystem::remove_all(root);

    auto store = retropp::SaveStore::atPath(root);

    const StatsState savedStats = populated();
    kirpich::HighScoreState savedScores;
    savedScores.typeA[3][0].score = 123456;

    ASSERT_TRUE(kirpich::saveStats(savedStats, store));
    ASSERT_TRUE(kirpich::saveTopScores(savedScores, store));

    // Read in the opposite order to the writes, so a version left standing by one loader would be the
    // one in place when the other reads.
    kirpich::HighScoreState loadedScores;
    ASSERT_TRUE(kirpich::loadTopScores(store, loadedScores));
    StatsState loadedStats;
    ASSERT_TRUE(kirpich::loadStats(store, loadedStats));

    EXPECT_EQ(loadedScores.typeA[3][0].score, 123456u);
    EXPECT_TRUE(loadedStats.typeB == savedStats.typeB);
    EXPECT_EQ(loadedStats.applicationSeconds, savedStats.applicationSeconds);
}

// (7) A game type's totals add the nine running counts and take the LARGER longest round. Summing
// that one would report a length no round ever had.
TEST(Stats, TypeTotalsSumTheCountsAndMaxTheLongestRound) {
    StatsState stats;
    stats.typeA[0] = StatSlice{.rounds = 2, .seconds = 30, .longestRoundSeconds = 20, .drops = 7,
                               .score = 100, .lines = 5, .singles = 3, .doubles = 1,
                               .triples = 0, .tetrises = 1};
    stats.typeA[4] = StatSlice{.rounds = 1, .seconds = 90, .longestRoundSeconds = 90, .drops = 11,
                               .score = 250, .lines = 9, .singles = 1, .doubles = 2,
                               .triples = 1, .tetrises = 0};

    const StatSlice total = kirpich::systems::totalsFor(stats, GameType::TYPE_A);

    EXPECT_EQ(total.rounds, 3u);
    EXPECT_EQ(total.seconds, 120u);
    EXPECT_EQ(total.drops, 18u);
    EXPECT_EQ(total.score, 350u);
    EXPECT_EQ(total.lines, 14u);
    EXPECT_EQ(total.singles, 4u);
    EXPECT_EQ(total.doubles, 3u);
    EXPECT_EQ(total.triples, 1u);
    EXPECT_EQ(total.tetrises, 1u);
    EXPECT_EQ(total.longestRoundSeconds, 90u) << "the longest is the largest, never the sum";
}

// (8) The lifetime totals cover all three tables.
TEST(Stats, LifetimeTotalsCoverAllThreeTypes) {
    StatsState stats;
    stats.typeA[1].rounds              = 1;
    stats.typeA[1].longestRoundSeconds = 10;
    stats.typeB[2][3].rounds              = 2;
    stats.typeB[2][3].longestRoundSeconds = 40;
    stats.typeC[4][5].rounds              = 4;
    stats.typeC[4][5].longestRoundSeconds = 25;

    const StatSlice total = kirpich::systems::lifetimeTotals(stats);
    EXPECT_EQ(total.rounds, 7u);
    EXPECT_EQ(total.longestRoundSeconds, 40u);
}

// (9) The longest round names the slice it was played in, Type A carries no second value, and a tie
// keeps the first slice in the walk.
TEST(Stats, LongestRoundNamesTheSliceItWasPlayedIn) {
    EXPECT_FALSE(kirpich::systems::longestRound(StatsState{}).any)
        << "nothing has been played, so there is no longest round";

    StatsState stats;
    stats.typeA[7].rounds              = 1;
    stats.typeA[7].longestRoundSeconds = 60;
    stats.typeB[2][3].rounds              = 1;
    stats.typeB[2][3].longestRoundSeconds = 120;
    stats.typeC[5][1].rounds              = 1;
    stats.typeC[5][1].longestRoundSeconds = 90;

    const auto best = kirpich::systems::longestRound(stats);
    ASSERT_TRUE(best.any);
    EXPECT_EQ(best.seconds, 120u);
    EXPECT_EQ(best.at.type, GameType::TYPE_B);
    EXPECT_EQ(best.at.level, 2);
    EXPECT_EQ(best.at.variant, 3);
    EXPECT_TRUE(best.at.hasVariant);

    // Type A's label is the level alone.
    StatsState typeAOnly;
    typeAOnly.typeA[9].rounds              = 1;
    typeAOnly.typeA[9].longestRoundSeconds = 15;
    const auto typeABest = kirpich::systems::longestRound(typeAOnly);
    ASSERT_TRUE(typeABest.any);
    EXPECT_EQ(typeABest.at.type, GameType::TYPE_A);
    EXPECT_EQ(typeABest.at.level, 9);
    EXPECT_FALSE(typeABest.at.hasVariant);

    // A tie keeps the first slice the walk reaches: Type A by level, then Type B, then Type C.
    StatsState tied;
    tied.typeB[1][1].rounds              = 1;
    tied.typeB[1][1].longestRoundSeconds = 50;
    tied.typeC[0][0].rounds              = 1;
    tied.typeC[0][0].longestRoundSeconds = 50;
    const auto tiedBest = kirpich::systems::longestRound(tied);
    ASSERT_TRUE(tiedBest.any);
    EXPECT_EQ(tiedBest.at.type, GameType::TYPE_B) << "a tie goes to the earlier slice in the walk";
}

// (10) A duration reads in hours and minutes from an hour up, and in minutes and seconds below one.
TEST(Stats, DurationTextReadsInHoursOrMinutes) {
    const auto text = [](std::uint32_t seconds) {
        return std::string(kirpich::systems::formatDuration(seconds).view());
    };

    EXPECT_EQ(text(0), "0m 00s");
    EXPECT_EQ(text(3), "0m 03s");
    EXPECT_EQ(text(63), "1m 03s");
    EXPECT_EQ(text(3599), "59m 59s");
    EXPECT_EQ(text(3600), "1h 00m");
    EXPECT_EQ(text(7500), "2h 05m");
    EXPECT_EQ(text(360000), "100h 00m");
}
