// Achievements state: the unlock records, their wire codec, the durable store round trip, and the
// boot / reset / load behaviour that keeps them across launches.
//
// The records are the port's own - no cartridge address, no fixture to check against - so these cases
// are written against the contract the headers state: what a record holds, what the image looks like,
// and that the records survive a reset and load back at startup.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <retropp/save_store.h>

#include "state/achievement_persistence.h"
#include "state/achievement_state.h"
#include "systems/boot.h"
#include "systems/game_context.h"

using kirpich::AchievementDate;
using kirpich::AchievementState;
using kirpich::AchievementUnlock;
using kirpich::kAchievementCount;

namespace {

// Every record filled with distinct values derived from its position, so a codec that transposes two
// records or two fields cannot round-trip by accident.
AchievementState populated() {
    AchievementState state;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        state.unlocked[i] = {.unlocked            = (i % 2) == 0,
                             .datePacked          = 20260000u + static_cast<std::uint32_t>(i),
                             .playSecondsAtUnlock = 1000u + static_cast<std::uint32_t>(i)};
    }
    return state;
}

}  // namespace

// (1) A default AchievementState has nothing unlocked, and reset() returns a populated one to that.
TEST(AchievementState, BootStateIsEmptyAndResetReturnsToIt) {
    AchievementState boot;
    for (const AchievementUnlock& record : boot.unlocked) {
        EXPECT_FALSE(record.unlocked);
        EXPECT_EQ(record.datePacked, 0u);
        EXPECT_EQ(record.playSecondsAtUnlock, 0u);
    }
    EXPECT_EQ(boot.round, kirpich::AchievementRound{});

    AchievementState live = populated();
    live.round.pendingEval = true;
    live.round.tetrises    = 7;
    live.reset();
    EXPECT_EQ(live, boot);
}

// (2) The date packs and unpacks losslessly, and an unset date is zero.
TEST(AchievementState, DatedPacksAndUnpacksLosslessly) {
    const AchievementDate date{.year = 2026, .month = 9, .day = 7};
    EXPECT_EQ(kirpich::packAchievementDate(date), 20260907u);
    EXPECT_EQ(kirpich::unpackAchievementDate(20260907u), date);
    EXPECT_EQ(kirpich::packAchievementDate(AchievementDate{}), 0u);
    EXPECT_EQ(kirpich::unpackAchievementDate(0u), AchievementDate{});
}

// (3) The image is one fixed-size record per achievement, and it round-trips over every field.
TEST(AchievementState, CodecRoundTripsEveryField) {
    EXPECT_EQ(kirpich::kAchievementsImageBytes, kAchievementCount * (1u + 4u + 4u));

    const AchievementState original = populated();
    const auto             image    = kirpich::encodeAchievements(original);
    EXPECT_EQ(image.size(), kirpich::kAchievementsImageBytes);

    AchievementState decoded;
    ASSERT_TRUE(kirpich::decodeAchievements(image, decoded));
    EXPECT_EQ(decoded, original);
}

// (4) A wrong-length image is refused and leaves the state untouched.
TEST(AchievementState, DecodeRefusesWrongLength) {
    const AchievementState before = populated();
    AchievementState       state  = before;

    std::vector<std::uint8_t> shortImage(kirpich::kAchievementsImageBytes - 1, 0);
    EXPECT_FALSE(kirpich::decodeAchievements(shortImage, state));
    EXPECT_EQ(state, before);

    std::vector<std::uint8_t> longImage(kirpich::kAchievementsImageBytes + 1, 0);
    EXPECT_FALSE(kirpich::decodeAchievements(longImage, state));
    EXPECT_EQ(state, before);
}

// (5) Through the durable store: absent is a first run, a written document reads back, and a corrupt
// one is refused without losing the file.
TEST(AchievementState, StoreRoundTripAbsentValidAndCorrupt) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_achievements_store";
    std::filesystem::remove_all(root);

    // Absent: ordinary first run, boot zeros left in place.
    {
        auto             store = retropp::SaveStore::atPath(root);
        AchievementState state;
        EXPECT_FALSE(kirpich::loadAchievements(store, state));
        EXPECT_EQ(state, AchievementState{});
    }

    // Valid: what was saved reads back.
    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveAchievements(populated(), store));

        AchievementState state;
        ASSERT_TRUE(kirpich::loadAchievements(store, state));
        EXPECT_EQ(state, populated());
    }

    // Corrupt: refused, boot zeros left, the damaged file kept in place.
    {
        std::ofstream out(root / "achievements", std::ios::binary | std::ios::trunc);
        out << "not a valid envelope";
        out.close();

        auto             store = retropp::SaveStore::atPath(root);
        AchievementState state;
        EXPECT_FALSE(kirpich::loadAchievements(store, state));
        EXPECT_EQ(state, AchievementState{});
        EXPECT_TRUE(store.exists("achievements"));
    }

    std::filesystem::remove_all(root);
}

// (6) The achievements document coexists with the other three in one store, each keeping its own bytes.
TEST(AchievementState, CoexistsWithTheOtherDocuments) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_achievements_coexist";
    std::filesystem::remove_all(root);

    auto store = retropp::SaveStore::atPath(root);
    ASSERT_TRUE(kirpich::saveAchievements(populated(), store));

    // A second document written to the same store does not disturb the achievements.
    const std::vector<std::byte> other(16, std::byte{0xAB});
    ASSERT_TRUE(store.write("stats", 2, other));

    AchievementState state;
    ASSERT_TRUE(kirpich::loadAchievements(store, state));
    EXPECT_EQ(state, populated());

    std::filesystem::remove_all(root);
}

// (7) The reset chord keeps the unlocked records but ends any round in progress; a cold boot clears
// both.
TEST(AchievementState, SoftResetKeepsRecordsAndEndsTheRound) {
    kirpich::systems::GameContext game;
    game.achievements.unlocked = populated().unlocked;
    game.achievements.round.active      = true;
    game.achievements.round.pendingEval = true;
    game.achievements.round.tetrises    = 5;

    kirpich::systems::softReset(game);

    // The records survive the chord.
    EXPECT_EQ(game.achievements.unlocked, populated().unlocked);
    // The round block returns to boot: a reset ends any round in progress.
    EXPECT_EQ(game.achievements.round, kirpich::AchievementRound{});
}

// (8) bootGame loads the achievements document over the boot zeros the cold boot just wrote.
TEST(AchievementState, BootGameLoadsTheRecords) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kirpich_achievements_boot";
    std::filesystem::remove_all(root);

    {
        auto store = retropp::SaveStore::atPath(root);
        ASSERT_TRUE(kirpich::saveAchievements(populated(), store));
    }

    auto                          store = retropp::SaveStore::atPath(root);
    kirpich::systems::GameContext game;
    kirpich::systems::bootGame(game, store);
    EXPECT_EQ(game.achievements.unlocked, populated().unlocked);

    std::filesystem::remove_all(root);
}
