#include "state/stats_persistence.h"

#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// One 32-bit count, little-endian, at `at`; `at` moves past it.
void putU32(std::array<std::uint8_t, kStatsImageBytes>& image, std::size_t& at,
            std::uint32_t value) {
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

void putSlice(std::array<std::uint8_t, kStatsImageBytes>& image, std::size_t& at,
              const StatSlice& slice) {
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
    return slice;
}

}  // namespace

std::array<std::uint8_t, kStatsImageBytes> encodeStats(const StatsState& state) {
    std::array<std::uint8_t, kStatsImageBytes> image{};
    std::size_t at = 0;

    for (const auto& level : state.typeB) {
        for (const auto& slice : level) putSlice(image, at, slice);
    }
    for (const auto& slice : state.typeA) putSlice(image, at, slice);
    for (const auto& level : state.typeC) {
        for (const auto& slice : level) putSlice(image, at, slice);
    }
    putU32(image, at, state.applicationSeconds);

    return image;
}

bool decodeStats(std::span<const std::uint8_t> image, StatsState& state) {
    if (image.size() != kStatsImageBytes) return false;

    std::size_t at = 0;
    for (auto& level : state.typeB) {
        for (auto& slice : level) slice = takeSlice(image, at);
    }
    for (auto& slice : state.typeA) slice = takeSlice(image, at);
    for (auto& level : state.typeC) {
        for (auto& slice : level) slice = takeSlice(image, at);
    }
    state.applicationSeconds = takeU32(image, at);

    return true;
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

}  // namespace kirpich
