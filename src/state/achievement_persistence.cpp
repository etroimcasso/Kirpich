#include "state/achievement_persistence.h"

#include <optional>

#include <spdlog/spdlog.h>

namespace kirpich {

namespace {

// One 32-bit value, little-endian, at `at`; `at` moves past it. The image is written and read in the
// same order the records declare, so one pair of helpers serves both directions.
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

}  // namespace

std::array<std::uint8_t, kAchievementsImageBytes> encodeAchievements(const AchievementState& state) {
    std::array<std::uint8_t, kAchievementsImageBytes> image{};
    std::size_t at = 0;

    for (const AchievementUnlock& record : state.unlocked) {
        image[at++] = record.unlocked ? 1u : 0u;
        putU32(image, at, record.datePacked);
        putU32(image, at, record.playSecondsAtUnlock);
    }

    return image;
}

bool decodeAchievements(std::span<const std::uint8_t> image, AchievementState& state) {
    if (image.size() != kAchievementsImageBytes) return false;

    std::size_t at = 0;
    for (AchievementUnlock& record : state.unlocked) {
        record.unlocked            = image[at++] != 0;
        record.datePacked          = takeU32(image, at);
        record.playSecondsAtUnlock = takeU32(image, at);
    }

    return true;
}

bool saveAchievements(const AchievementState& state, retropp::SaveStore& store) {
    const auto image = encodeAchievements(state);
    return store.write("achievements", kAchievementsSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadAchievements(retropp::SaveStore& store, AchievementState& state) {
    // The store's version, not the document's, so it is declared here rather than once at startup: the
    // same store also carries the settings, the top scores and the statistics at versions of their
    // own, and whichever loader is about to read has to be the one that last said which version it
    // means. Born at version 1, this document has no older format to migrate from.
    store.setCurrentVersion(kAchievementsSchemaVersion);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("achievements");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("achievements save is corrupt, starting the records empty: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the boot zeros

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeAchievements(image, state)) {
        spdlog::error("achievements save has wrong length {} (expected {}), starting the records empty",
                      doc->payload.size(), kAchievementsImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
