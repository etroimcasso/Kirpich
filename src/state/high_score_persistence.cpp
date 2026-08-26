#include "state/high_score_persistence.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

namespace kirpich {
namespace {

// One decimal score -> three packed-decimal bytes, low pair first (byte 0 = the two least
// significant digits, byte 2 = the two most significant). The ceiling is 999999 - six digits, the
// same limit the scoring code enforces on wScore.
std::array<std::uint8_t, 3> encodeBcd(std::uint32_t score) {
    if (score > 999999u) score = 999999u;
    std::array<std::uint8_t, 3> out{};
    for (auto& byte : out) {
        const std::uint8_t lo = static_cast<std::uint8_t>(score % 10u);
        score /= 10u;
        const std::uint8_t hi = static_cast<std::uint8_t>(score % 10u);
        score /= 10u;
        byte = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

// Three packed-decimal bytes (low pair first) -> a decimal score. Each nibble is a decimal digit.
std::uint32_t decodeBcd(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    std::uint32_t place = 1;
    for (int i = 0; i < 3; ++i) {
        value += place * static_cast<std::uint32_t>(bytes[i] & 0x0F);
        place *= 10;
        value += place * static_cast<std::uint32_t>((bytes[i] >> 4) & 0x0F);
        place *= 10;
    }
    return value;
}

}  // namespace

std::array<std::uint8_t, kTopScoresImageBytes> encodeTopScores(const HighScoreState& state) {
    std::array<std::uint8_t, kTopScoresImageBytes> image{};
    std::size_t p = 0;

    // A 27-byte slice: three scores (9 bytes, rank 0/1/2, each low-pair-first) then three names
    // (18 bytes, rank 0/1/2, each six glyphs first-glyph-lowest).
    const auto writeSlice = [&](const std::array<TopScoreEntry, 3>& slice) {
        for (const auto& entry : slice) {
            const auto bcd = encodeBcd(entry.score);
            image[p++] = bcd[0];
            image[p++] = bcd[1];
            image[p++] = bcd[2];
        }
        for (const auto& entry : slice)
            for (const CharTile glyph : entry.name)
                image[p++] = static_cast<std::uint8_t>(glyph);
    };

    for (const auto& level : state.typeB)      // 10 levels x 6 heights = 60 slices = 1620 bytes
        for (const auto& height : level)
            writeSlice(height);
    for (const auto& level : state.typeA)      // 10 levels = 10 slices = 270 bytes
        writeSlice(level);
    for (const auto& level : state.typeC)      // 10 levels x 6 rises, Type B's shape
        for (const auto& rise : level)
            writeSlice(rise);

    return image;
}

bool decodeTopScores(std::span<const std::uint8_t> image, HighScoreState& state) {
    if (image.size() != kTopScoresImageBytes) return false;

    std::size_t p = 0;
    const auto readSlice = [&](std::array<TopScoreEntry, 3>& slice) {
        for (auto& entry : slice) {
            entry.score = decodeBcd(&image[p]);
            p += 3;
        }
        for (auto& entry : slice)
            for (CharTile& glyph : entry.name)
                glyph = static_cast<CharTile>(image[p++]);
    };

    for (auto& level : state.typeB)
        for (auto& height : level)
            readSlice(height);
    for (auto& level : state.typeA)
        readSlice(level);
    for (auto& level : state.typeC)
        for (auto& rise : level)
            readSlice(rise);

    return true;
}

std::vector<std::byte> migrateTopScoresV1ToV2(std::vector<std::byte> payload) {
    // One empty Type C block appended, not a resize to the version 2 length: the step's whole content
    // is that version 2 carries a third table. A payload of any other length is not this step's to
    // correct - decodeTopScores refuses a wrong length downstream.
    payload.insert(payload.end(), kTopScoresTypeCBytesV2, std::byte{0});
    return payload;
}

std::vector<std::byte> migrateTopScoresV2ToV3(std::vector<std::byte> payload) {
    // The Type C block goes from one slice per level to six. Each level's existing slice is the one
    // that level's rounds were played at - a rise of 10, the only one there was - so it lands at that
    // rise and the level's other five start empty.
    //
    // The two cartridge blocks in front of it are carried through byte for byte: this step widens one
    // table and touches nothing else. A payload that is not a version 2 image is left as it is, the
    // shape the step before this one has - decodeTopScores refuses a wrong length downstream.
    if (payload.size() != kTopScoresImageBytesV2) {
        return payload;
    }

    std::vector<std::byte> widened(kTopScoresImageBytes, std::byte{0});
    std::copy(payload.begin(), payload.begin() + kTopScoresImageBytesV1, widened.begin());

    for (std::size_t level = 0; level < kTopScoresTypeCLevels; ++level) {
        const std::size_t from = kTopScoresImageBytesV1 + level * kTopScoresSliceBytes;
        const std::size_t to   = kTopScoresImageBytesV1 +
                                 (level * kTopScoresTypeCRises + kTopScoresMigratedRiseIndex) *
                                     kTopScoresSliceBytes;
        std::copy(payload.begin() + from, payload.begin() + from + kTopScoresSliceBytes,
                  widened.begin() + to);
    }

    return widened;
}

bool saveTopScores(const HighScoreState& state, retropp::SaveStore& store) {
    const auto image = encodeTopScores(state);
    return store.write("topscores", kTopScoresSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadTopScores(retropp::SaveStore& store, HighScoreState& state) {
    // Both of these are the store's, not the document's, so they are declared here rather than once at
    // startup: the same store also carries the settings at their own version, and whichever loader is
    // about to read has to be the one that last said which version it means.
    store.setCurrentVersion(kTopScoresSchemaVersion);
    store.registerMigration(1, migrateTopScoresV1ToV2);
    store.registerMigration(2, migrateTopScoresV2ToV3);

    std::optional<retropp::SaveStore::Document> doc;
    try {
        doc = store.read("topscores");
    } catch (const retropp::SaveStoreError& error) {
        spdlog::error("top-score save is corrupt, running with no saved scores: {}", error.what());
        return false;
    }
    if (!doc) return false;  // absent - ordinary first run; leave the boot zeros

    const std::span<const std::uint8_t> image(
        reinterpret_cast<const std::uint8_t*>(doc->payload.data()), doc->payload.size());
    if (!decodeTopScores(image, state)) {
        spdlog::error("top-score save has wrong length {} (expected {}), running with no saved scores",
                      doc->payload.size(), kTopScoresImageBytes);
        return false;
    }
    return true;
}

}  // namespace kirpich
