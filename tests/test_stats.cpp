// Statistics: the table, its wire codec, the folds that read it back, and the duration text.
//
// The tables are the port's own - no cartridge address, no fixture to check against - so these cases
// are written against the contract the header states: what a slice holds, what the image looks like,
// how a rollup is computed from the slices, and which slice a longest round names.

#include <filesystem>
#include <fstream>
#include <span>
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
    StatSlice slice{.rounds              = seed + 1,
                    .seconds             = seed + 2,
                    .longestRoundSeconds = seed + 3,
                    .drops               = seed + 4,
                    .score               = seed + 5,
                    .lines               = seed + 6,
                    .singles             = seed + 7,
                    .doubles             = seed + 8,
                    .triples             = seed + 9,
                    .tetrises            = seed + 10};
    for (std::size_t kind = 0; kind < kirpich::kPieceKindCount; ++kind) {
        slice.pieces[kind] = seed + 11 + static_cast<std::uint32_t>(kind);
    }
    return slice;
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
    for (std::size_t music = 0; music < kirpich::kMusicTypeCount; ++music) {
        stats.musicRounds[music] = 4000 + static_cast<std::uint32_t>(music);
    }
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
    EXPECT_EQ(loaded.musicRounds, saved.musicRounds);
}

// (2b) A version 1 document - the format that shipped before the piece counts existed - is migrated
// on the way in rather than read short. Every figure it carried survives in the slice it was in, and
// the counts it predates arrive at zero.
//
// The step is exercised on its own AND through the store, because the two can disagree: a step that
// produced the right bytes but was never registered would pass the first and fail the second.
TEST(Stats, AVersionOneDocumentMigratesForward) {
    // A version 1 image, laid out the way version 1 laid it out: ten counts to a slice, no music
    // block. Written by hand here rather than by an old encoder, because there is no old encoder any
    // more and the bytes are the contract.
    std::vector<std::byte> v1(kirpich::kStatsImageBytesV1, std::byte{0});
    const auto putU32 = [&v1](std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            v1[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
        }
    };
    // The first Type B slice's rounds, the first Type A slice's rounds, and the application total -
    // one figure from each block, so a migration that lost a block's alignment shows up.
    constexpr std::size_t kTypeBBytesV1 =
        kirpich::kStatLevels * kirpich::kStatVariants * kirpich::kStatSliceBytesV1;
    constexpr std::size_t kTypeABytesV1 = kirpich::kStatLevels * kirpich::kStatSliceBytesV1;
    putU32(0, 111u);
    putU32(kTypeBBytesV1, 222u);
    putU32(kTypeBBytesV1 + kTypeABytesV1, 333u);
    putU32(kirpich::kStatsImageBytesV1 - 4, 444u);

    const std::vector<std::byte> v2 = kirpich::migrateStatsV1ToV2(v1);
    ASSERT_EQ(v2.size(), kirpich::kStatsImageBytes);

    StatsState migrated;
    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(v2.data()), v2.size());
    ASSERT_TRUE(kirpich::decodeStats(image, migrated));

    EXPECT_EQ(migrated.typeB[0][0].rounds, 111u);
    EXPECT_EQ(migrated.typeA[0].rounds, 222u);
    EXPECT_EQ(migrated.typeC[0][0].rounds, 333u);
    EXPECT_EQ(migrated.applicationSeconds, 444u);

    // And everything the format predates is zero, not garbage read out of the bytes beside it.
    for (const std::uint32_t count : migrated.typeB[0][0].pieces) EXPECT_EQ(count, 0u);
    for (const std::uint32_t count : migrated.typeA[0].pieces) EXPECT_EQ(count, 0u);
    for (const std::uint32_t count : migrated.musicRounds) EXPECT_EQ(count, 0u);

    // An image that is not version 1's length is not this step's to correct.
    const std::vector<std::byte> wrong(7, std::byte{0xAB});
    EXPECT_EQ(kirpich::migrateStatsV1ToV2(wrong), wrong);

    // Through the store, which is where the registration is proved.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_stats_v1_migration";
    std::filesystem::remove_all(root);
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(store.write("stats", 1, v1));
    }
    {
        auto       store = retropp::SaveStore::atPath(root);
        StatsState loaded;
        ASSERT_TRUE(kirpich::loadStats(store, loaded))
            << "a document from before the piece counts must still load";
        EXPECT_EQ(loaded.typeB[0][0].rounds, 111u);
        EXPECT_EQ(loaded.applicationSeconds, 444u);

        const auto doc = store.read("stats");
        ASSERT_TRUE(doc.has_value());
        EXPECT_EQ(doc->schemaVersion, kirpich::kStatsSchemaVersion);
        EXPECT_EQ(doc->payload.size(), kirpich::kStatsImageBytes)
            << "a version 1 document must reach the decoder at version 2's length";
    }
    std::filesystem::remove_all(root);
}

// (3) The image is the size the header states, and its blocks are in the top-score document's order:
// Type B, then Type A, then Type C, then the application total.
TEST(Stats, WireImageSizeAndBlockOrder) {
    // Seventeen counts to a slice: the ten it started with and the seven per-kind piece counts.
    EXPECT_EQ(kirpich::kStatSliceBytes, 68u);
    EXPECT_EQ(kirpich::kStatsImageBytes, 8860u);
    EXPECT_EQ(kirpich::kStatsImageBytes, kirpich::kStatsTypeBBytes + kirpich::kStatsTypeABytes +
                                             kirpich::kStatsTypeCBytes +
                                             kirpich::kStatsApplicationBytes +
                                             kirpich::kStatsMusicBytes);
    EXPECT_EQ(kirpich::kStatsImageBytesV1, 5204u) << "what the first version wrote";

    StatsState stats;
    stats.typeB[0][0].rounds  = 0x11223344;
    stats.typeA[0].rounds     = 0x55667788;
    stats.typeC[0][0].rounds  = 0x99AABBCC;
    stats.applicationSeconds  = 0x0D0C0B0A;
    stats.musicRounds[kirpich::kMusicTypeCount - 1] = 0x1E1D1C1B;

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
    EXPECT_EQ(readAt(kirpich::kStatsImageBytes - kirpich::kStatsMusicBytes - 4), 0x0D0C0B0Au)
        << "then the application total";
    EXPECT_EQ(readAt(kirpich::kStatsImageBytes - 4), 0x1E1D1C1Bu)
        << "and the music counts last, in selection order";
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

// (11) The favourite game type is the one more rounds have been played in than any other, counted
// over the whole of that type's table, and a tie keeps the earlier type in the walk.
TEST(Stats, FavouriteModeIsAnArgmaxOverRoundsWithTiesToTypeA) {
    StatsState stats;
    stats.typeA[0].rounds    = 3;
    stats.typeB[2][1].rounds = 4;
    stats.typeB[5][0].rounds = 1;
    stats.typeC[9][5].rounds = 2;

    EXPECT_EQ(kirpich::systems::roundsFor(stats, GameType::TYPE_A), 3u);
    EXPECT_EQ(kirpich::systems::roundsFor(stats, GameType::TYPE_B), 5u)
        << "a type's rounds are the sum over its whole table";
    EXPECT_EQ(kirpich::systems::roundsFor(stats, GameType::TYPE_C), 2u);

    const auto best = kirpich::systems::favouriteMode(stats);
    ASSERT_TRUE(best.any);
    EXPECT_EQ(best.type, GameType::TYPE_B);
    EXPECT_EQ(best.rounds, 5u);

    // A tie keeps the first type the walk reaches.
    StatsState tied;
    tied.typeA[0].rounds    = 6;
    tied.typeC[0][0].rounds = 6;
    const auto tiedBest = kirpich::systems::favouriteMode(tied);
    ASSERT_TRUE(tiedBest.any);
    EXPECT_EQ(tiedBest.type, GameType::TYPE_A) << "a tie goes to the earlier type in the walk";
}

// (12) The favourite music is an argmax over the per-selection round counts, which sit outside the
// slice table because a song is not part of a combination. The byte the flow holds before a
// selection has been made has no slot at all, so it can never win.
TEST(Stats, FavouriteMusicIsAnArgmaxOverTheSelectionCounts) {
    StatsState stats;
    stats.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::MUSIC_A)] = 2;
    stats.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::MUSIC_C)] = 9;
    stats.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::OFF)]     = 4;

    const auto best = kirpich::systems::favouriteMusic(stats);
    ASSERT_TRUE(best.any);
    EXPECT_EQ(best.type, kirpich::MusicType::MUSIC_C);
    EXPECT_EQ(best.rounds, 9u);

    // Every count is reachable, and the unset byte has no slot to be counted in.
    EXPECT_EQ(kirpich::musicTypeIndex(static_cast<kirpich::MusicType>(0x00)),
              kirpich::kMusicTypeCount);

    StatsState tied;
    tied.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::MUSIC_B)] = 7;
    tied.musicRounds[kirpich::musicTypeIndex(kirpich::MusicType::OFF)]     = 7;
    const auto tiedBest = kirpich::systems::favouriteMusic(tied);
    ASSERT_TRUE(tiedBest.any);
    EXPECT_EQ(tiedBest.type, kirpich::MusicType::MUSIC_B)
        << "a tie goes to the earlier selection in the walk";
}

// (13) The preferred starting level counts a level across all three game types, since a level is
// picked in every one of them, and a tie keeps the lower level.
TEST(Stats, PreferredLevelCountsALevelAcrossEveryGameType) {
    StatsState stats;
    stats.typeA[6].rounds    = 2;
    stats.typeB[6][3].rounds = 3;
    stats.typeC[6][0].rounds = 1;
    stats.typeA[2].rounds    = 5;

    const auto best = kirpich::systems::preferredLevel(stats);
    ASSERT_TRUE(best.any);
    EXPECT_EQ(best.level, 6) << "a level is the rounds played at it in every type";
    EXPECT_EQ(best.rounds, 6u);

    StatsState tied;
    tied.typeA[3].rounds    = 4;
    tied.typeC[8][2].rounds = 4;
    const auto tiedBest = kirpich::systems::preferredLevel(tied);
    ASSERT_TRUE(tiedBest.any);
    EXPECT_EQ(tiedBest.level, 3) << "a tie goes to the lower level";
}

// (14) Over a table where nothing has been played, all four folds report that rather than a first
// slot that reads as a real answer - and a selection with both axes folded is the game type's own
// total, which is the equality the picker's own aggregate rests on.
TEST(Stats, TheFoldsReportNothingPlayedAndFoldBothAxesToTheTypeTotal) {
    const StatsState empty;

    EXPECT_FALSE(kirpich::systems::favouriteMode(empty).any);
    EXPECT_FALSE(kirpich::systems::favouriteMusic(empty).any);
    EXPECT_FALSE(kirpich::systems::preferredLevel(empty).any);
    EXPECT_FALSE(kirpich::systems::longestRound(empty).any);
    EXPECT_EQ(kirpich::systems::roundsFor(empty, GameType::TYPE_B), 0u);

    const StatsState stats = populated();
    for (const GameType type : {GameType::TYPE_A, GameType::TYPE_B, GameType::TYPE_C}) {
        const kirpich::systems::StatSelection everything{.type    = type,
                                                         .level   = kirpich::kStatAxisAll,
                                                         .variant = kirpich::kStatAxisAll};
        EXPECT_EQ(kirpich::systems::totalsForSelection(stats, everything),
                  kirpich::systems::totalsFor(stats, type));
    }

    // One level folded across its variants: the six slices of that level and no others. Type A is
    // picked by level alone, so its second axis is not consulted whatever it holds.
    std::uint32_t byHand = 0;
    for (std::size_t variant = 0; variant < kirpich::kStatVariants; ++variant) {
        byHand += stats.typeC[7][variant].rounds;
    }
    const kirpich::systems::StatSelection oneLevel{
        .type = GameType::TYPE_C, .level = 7, .variant = kirpich::kStatAxisAll};
    EXPECT_EQ(kirpich::systems::totalsForSelection(stats, oneLevel).rounds, byHand);

    const kirpich::systems::StatSelection typeALevel{
        .type = GameType::TYPE_A, .level = 4, .variant = 2};
    EXPECT_EQ(kirpich::systems::totalsForSelection(stats, typeALevel).rounds,
              stats.typeA[4].rounds);
}
