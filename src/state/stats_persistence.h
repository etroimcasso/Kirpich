#pragma once

// Statistics persistence: the wire codec that turns the three tables and the application total into
// one flat byte image (and back), and the two calls that carry that image through the engine's
// durable SaveStore.
//
// The payload is the three tables in the top-score document's own order - Type B, then Type A, then
// Type C - followed by the application total and the per-music round counts. Reusing that order
// rather than minting a second convention means one answer to "which block comes first" across both
// documents.
//
// A slice is its counts as little-endian 32-bit values, in the order StatSlice declares them. Every
// count is 32 bits wide whether or not it needs to be: a uniform slice makes the image's size a
// multiplication rather than a sum, and no count can ever be the one that overflows first.
//
// Only the tables, the application total and the music counts are written. The round in progress is
// the current round's bookkeeping and means nothing after the program stops, so it is left out
// exactly as HighScoreState's name-entry bytes are left out of theirs.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <retropp/save_store.h>

#include "state/stats_state.h"

namespace kirpich {

// The statistics save document: name (spelled as a literal at the call sites too, as the other two
// documents' are), and its schema version.
//
// Version 2 widens a slice from ten counts to seventeen - the seven per-kind piece counts - and adds
// the per-music round counts after the application total. Its migration registers in loadStats
// immediately before its own read, because the store's version and its migrations are the store's,
// not the document's, and this store also carries the settings and the top scores at versions of
// their own.
inline constexpr std::string_view kStatsDocument      = "stats";
inline constexpr std::uint32_t    kStatsSchemaVersion = 2;

// What one slice costs on the wire, and what each block and the whole document cost.
inline constexpr std::size_t kStatSliceCounts = 10 + kPieceKindCount;
inline constexpr std::size_t kStatSliceBytes  = kStatSliceCounts * 4;
inline constexpr std::size_t kStatsTypeABytes = kStatLevels * kStatSliceBytes;
inline constexpr std::size_t kStatsTypeBBytes = kStatLevels * kStatVariants * kStatSliceBytes;
inline constexpr std::size_t kStatsTypeCBytes = kStatsTypeBBytes;
inline constexpr std::size_t kStatsApplicationBytes = 4;
inline constexpr std::size_t kStatsMusicBytes       = kMusicTypeCount * 4;
inline constexpr std::size_t kStatsImageBytes = kStatsTypeBBytes + kStatsTypeABytes +
                                                kStatsTypeCBytes + kStatsApplicationBytes +
                                                kStatsMusicBytes;

// What version 1 wrote: the same three tables and the application total, with ten counts to a slice
// and no music block. Named so the migration and its test say the same numbers.
inline constexpr std::size_t kStatSliceBytesV1  = 10 * 4;
inline constexpr std::size_t kStatsImageBytesV1 =
    kStatLevels * kStatVariants * kStatSliceBytesV1 * 2 + kStatLevels * kStatSliceBytesV1 +
    kStatsApplicationBytes;

// Encode the tables and the application total into the wire image.
[[nodiscard]] std::array<std::uint8_t, kStatsImageBytes> encodeStats(const StatsState& state);

// Decode a wire image into `state`'s tables and application total. Returns false and leaves `state`
// untouched when the image is not exactly kStatsImageBytes; true on success. The round in progress
// is never written.
[[nodiscard]] bool decodeStats(std::span<const std::uint8_t> image, StatsState& state);

// Bring a version 1 image up to version 2.
//
// Not an append: version 2 widened the slice itself, so every one of the 130 slices grows in place -
// its ten counts survive and the seven piece counts arrive at zero behind them - and the music block
// arrives at zero after the application total. A player who has been recorded for months keeps every
// figure they had; the ones the format predates start from nothing, which is what they were.
//
// A payload that is not version 1's length is handed back untouched. Correcting a length is not this
// step's business - decodeStats refuses a wrong one downstream.
//
// Exposed so the step can be tested for what it does rather than only through a store.
[[nodiscard]] std::vector<std::byte> migrateStatsV1ToV2(std::vector<std::byte> payload);

// Persist the tables and the application total as document "stats" at the current schema version.
// Returns whatever the atomic write reports.
bool saveStats(const StatsState& state, retropp::SaveStore& store);

// Load them from the store. Absent document (ordinary first run) -> leave the boot zeros, return
// false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error, leave the
// boot zeros, leave the damaged file in place, return false.
//
// Declares this document's schema version on the store before reading, because the version is the
// store's rather than the document's: every loader sharing a store must name its own version
// immediately before its own read (src/state/settings.h and high_score_persistence.h do the same).
bool loadStats(retropp::SaveStore& store, StatsState& state);

}  // namespace kirpich
