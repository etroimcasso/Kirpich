#include "state/high_score_persistence.h"

#include <optional>

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

    return true;
}

bool saveTopScores(const HighScoreState& state, retropp::SaveStore& store) {
    const auto image = encodeTopScores(state);
    return store.write("topscores", kTopScoresSchemaVersion,
                       std::as_bytes(std::span<const std::uint8_t>(image)));
}

bool loadTopScores(retropp::SaveStore& store, HighScoreState& state) {
    store.setCurrentVersion(kTopScoresSchemaVersion);

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
