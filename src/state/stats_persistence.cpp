#include "state/stats_persistence.h"

#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// The two table shapes a slice block comes in: Type A is one slice per level, Type B and Type C are
// one slice per level and second-axis value.
using LevelTable   = std::array<StatSlice, kStatLevels>;
using VariantTable = std::array<std::array<StatSlice, kStatVariants>, kStatLevels>;

// One 32-bit count, little-endian, at `at`; `at` moves past it. The image is a span so one set of
// helpers serves both the full document and the smaller heart document.
void putU32(std::span<std::uint8_t> image, std::size_t& at, std::uint32_t value) {
    image[at++] = static_cast<std::uint8_t>(value & 0xFFu);
    image[at++] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    image[at++] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    image[at++] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t takeU32(std::span<const std::uint8_t> image, std::size_t& at) {
    const std::uint32_t value = static_cast<std::uint32_t>(image[at]) |
                                (static_cast<std::uint32_t>(image[at + 1]) << 8) |
                                (static_cast<std::uint32_t>(image[at + 2]) << 16) |
                                (static_cast<std::uint32_t>(image[at + 3]) << 24);
    at += 4;
    return value;
}

void putSlice(std::span<std::uint8_t> image, std::size_t& at, const StatSlice& slice) {
    putU32(image, at, slice.rounds);
    putU32(image, at, slice.seconds);
    putU32(image, at, slice.longestRoundSeconds);
    putU32(image, at, slice.drops);
    putU32(image, at, slice.score);
    putU32(image, at, slice.lines);
    putU32(image, at, slice.singles);
    putU32(image, at, slice.doubles);
    putU32(image, at, slice.triples);
    putU32(image, at, slice.tetrises);
    for (const std::uint32_t count : slice.pieces) putU32(image, at, count);
}

StatSlice takeSlice(std::span<const std::uint8_t> image, std::size_t& at) {
    StatSlice slice;
    slice.rounds              = takeU32(image, at);
    slice.seconds             = takeU32(image, at);
    slice.longestRoundSeconds = takeU32(image, at);
    slice.drops               = takeU32(image, at);
    slice.score               = takeU32(image, at);
    slice.lines               = takeU32(image, at);
    slice.singles             = takeU32(image, at);
    slice.doubles             = takeU32(image, at);
    slice.triples             = takeU32(image, at);
    slice.tetrises            = takeU32(image, at);
    for (std::uint32_t& count : slice.pieces) count = takeU32(image, at);
    return slice;
}

// The three slice tables, in the document's order (Type B, Type A, Type C). Factored so the
// cartridge-shaped statistics and the parallel heart statistics serialise through one walk, which is
// what keeps the heart image a strict prefix of the full one's slice blocks.
void writeSliceTables(std::span<std::uint8_t> image, std::size_t& at, const VariantTable& typeB,
                      const LevelTable& typeA, const VariantTable& typeC) {
    for (const auto& level : typeB) {
        for (const auto& slice : level) putSlice(image, at, slice);
    }
    for (const auto& slice : typeA) putSlice(image, at, slice);
    for (const auto& level : typeC) {
        for (const auto& slice : level) putSlice(image, at, slice);
    }
}

void readSliceTables(std::span<const std::uint8_t> image, std::size_t& at, VariantTable& typeB,
                     LevelTable& typeA, VariantTable& typeC) {
    for (auto& level : typeB) {
        for (auto& slice : level) slice = takeSlice(image, at);
    }
    for (auto& slice : typeA) slice = takeSlice(image, at);
    for (auto& level : typeC) {
        for (auto& slice : level) slice = takeSlice(image, at);
    }
}

}  // namespace

std::array<std::uint8_t, kStatsImageBytes> encodeStats(const StatsState& state) {
    std::array<std::uint8_t, kStatsImageBytes> image{};
    std::size_t at = 0;

    writeSliceTables(image, at, state.typeB, state.typeA, state.typeC);
    putU32(image, at, state.applicationSeconds);
    for (const std::uint32_t count : state.musicRounds) putU32(image, at, count);

    return image;
}

bool decodeStats(std::span<const std::uint8_t> image, StatsState& state) {
    if (image.size() != kStatsImageBytes) return false;

    std::size_t at = 0;
    readSliceTables(image, at, state.typeB, state.typeA, state.typeC);
    state.applicationSeconds = takeU32(image, at);
    for (std::uint32_t& count : state.musicRounds) count = takeU32(image, at);

    return true;
}

std::array<std::uint8_t, kStatsHeartImageBytes> encodeStatsHeart(const StatsState& state) {
    std::array<std::uint8_t, kStatsHeartImageBytes> image{};
    std::size_t at = 0;

    // Only the three slice tables: the application total and the music counts are global and stay in
    // the main document, so the heart image is the slice blocks alone.
    writeSliceTables(image, at, state.typeBHeart, state.typeAHeart, state.typeCHeart);

    return image;
}

bool decodeStatsHeart(std::span<const std::uint8_t> image, StatsState& state) {
    if (image.size() != kStatsHeartImageBytes) return false;

    std::size_t at = 0;
    readSliceTables(image, at, state.typeBHeart, state.typeAHeart, state.typeCHeart);

    return true;
}

std::vector<std::byte> migrateStatsV1ToV2(std::vector<std::byte> payload) {
    if (payload.size() != kStatsImageBytesV1) return payload;

    std::vector<std::byte> grown;
    grown.reserve(kStatsImageBytes);

    // Every slice in place: its ten counts, then the seven that version 2 added, at zero.
    constexpr std::size_t kSlices = kStatLevels * kStatVariants * 2 + kStatLevels;
    std::size_t           at      = 0;
    for (std::size_t slice = 0; slice < kSlices; ++slice) {
        grown.insert(grown.end(), payload.begin() + static_cast<std::ptrdiff_t>(at),
                     payload.begin() + static_cast<std::ptrdiff_t>(at + kStatSliceBytesV1));
        at += kStatSliceBytesV1;
        grown.insert(grown.end(), kStatSliceBytes - kStatSliceBytesV1, std::byte{0});
    }

    // The application total, which version 1 already carried, and then the music block behind it.
    grown.insert(grown.end(), payload.begin() + static_cast<std::ptrdiff_t>(at), payload.end());
    grown.insert(grown.end(), kStatsMusicBytes, std::byte{0});

    return grown;
}

bool saveStats(const StatsState& state, retropp::SaveStore& store) {
    const auto image = encodeStats(state);
    return store.write("stats", kStatsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadStats(retropp::SaveStore& store, StatsState& state) {
    // The store's version, not the document's, so it is declared here rather than once at startup:
    // the same store also carries the settings and the top scores at versions of their own, and
    // whichever loader is about to read has to be the one that last said which version it means.
    store.setCurrentVersion(kStatsSchemaVersion);
    store.registerMigration(1, migrateStatsV1ToV2);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("stats");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("statistics save is corrupt, starting the tables empty: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the boot zeros

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeStats(image, state)) {
        spdlog::error("statistics save has wrong length {} (expected {}), starting the tables empty",
                      doc->payload.size(), kStatsImageBytes);
        return false;
    }
    return true;
}

bool saveStatsHeart(const StatsState& state, retropp::SaveStore& store) {
    const auto image = encodeStatsHeart(state);
    return store.write("stats-heart", kStatsHeartSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadStatsHeart(retropp::SaveStore& store, StatsState& state) {
    // The heart document is born at version 1 and has no older format to migrate from. Its version is
    // still declared here, immediately before its own read, for the reason every loader sharing this
    // store does: the version is the store's, and whichever loader is about to read has to be the one
    // that last set it - here to 1, which leaves the main document's migration unreachable for a
    // v1-stored read.
    store.setCurrentVersion(kStatsHeartSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("stats-heart");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("heart statistics save is corrupt, starting the heart tables empty: {}",
                      error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary until the first heart round; leave the boot zeros

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeStatsHeart(image, state)) {
        spdlog::error(
            "heart statistics save has wrong length {} (expected {}), starting the heart tables empty",
            doc->payload.size(), kStatsHeartImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
