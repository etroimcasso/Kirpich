#pragma once

// Achievements persistence: the wire codec that turns the unlock records into one flat byte image
// (and back), and the two calls that carry that image through the engine's durable SaveStore.
//
// The payload is the unlock records in AchievementId order: for each achievement, its unlocked flag,
// the packed unlock date, and the play time at unlock. The round-in-progress block on AchievementState
// is the current round's bookkeeping and means nothing after the program stops, so it is left out
// exactly as StatsState's round is left out of the statistics document.
//
// Born at version 1, this document has no older format to migrate from; the migration hook is where a
// later version's step registers, following the pattern the other documents in this store already use.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <retropp/save_store.h>

#include "state/achievement_state.h"

namespace kirpich {

// The achievements save document: name (spelled as a literal at the call sites too, as the other
// documents' are), and its schema version.
inline constexpr std::string_view kAchievementsDocument      = "achievements";
inline constexpr std::uint32_t    kAchievementsSchemaVersion = 1;

// What one unlock record costs on the wire, and what the whole document costs. A record is its
// unlocked flag (one byte) followed by the packed date and the play seconds (a 32-bit value each).
inline constexpr std::size_t kAchievementRecordBytes = 1 + 4 + 4;
inline constexpr std::size_t kAchievementsImageBytes = kAchievementCount * kAchievementRecordBytes;

// Encode the unlock records into the wire image.
[[nodiscard]] std::array<std::uint8_t, kAchievementsImageBytes> encodeAchievements(
    const AchievementState& state);

// Decode a wire image into `state`'s unlock records. Returns false and leaves `state` untouched when
// the image is not exactly kAchievementsImageBytes; true on success. The round in progress is never
// written.
[[nodiscard]] bool decodeAchievements(std::span<const std::uint8_t> image, AchievementState& state);

// Persist the unlock records as document "achievements" at the current schema version. Returns
// whatever the atomic write reports.
bool saveAchievements(const AchievementState& state, retropp::SaveStore& store);

// Load them from the store. Absent document (ordinary first run) -> leave the boot zeros, return
// false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error, leave the
// boot zeros, leave the damaged file in place, return false.
//
// Declares this document's schema version on the store before reading, because the version is the
// store's rather than the document's: every loader sharing this store must name its own version
// immediately before its own read, as the settings, top-score and statistics loaders do.
bool loadAchievements(retropp::SaveStore& store, AchievementState& state);

}  // namespace kirpich
