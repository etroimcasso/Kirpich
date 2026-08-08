#include "data/scoring.h"

#include <cassert>

namespace kirpich {

std::uint32_t lineClearAward(LineClearKind kind, std::uint8_t level) {
    // Levelling stops at kLevelCap, so no award site can see a higher level. The original has no
    // bounds check because the input cannot occur; the port asserts rather than inventing a result.
    assert(level <= kLevelCap && "level is out of range");

    const LineClearScoreEntry& entry = kLineClearScores[static_cast<std::size_t>(kind)];
    // The original multiplies by repeated addition, level + 1 times. The product stays exact here;
    // clamping to kScoreSaturation happens where the award lands in the running score.
    return std::uint32_t{entry.points} * (std::uint32_t{level} + 1);
}

std::uint32_t softDropAward(std::uint8_t rows) {
    // Both original paths pre-decrement the row counter before converting it to points, so a
    // soft drop of N rows scores N - 1 - upstream's own "Why one point less? TODO". The original
    // never awards with a zero counter (its caller skips the award entirely), so zero maps to the
    // same no-op here.
    return rows == 0 ? 0 : std::uint32_t{rows} - 1;
}

std::optional<SpriteId> rocketSpriteForScore(std::uint32_t score) {
    // The score accumulates through a saturating add, so nothing above the cap can exist.
    assert(score <= kScoreSaturation && "score is out of range");

    // Highest tier first, exactly as the game compares the score's top two digits. Every
    // threshold is a multiple of 10 000, which is why the original's two-digit compare and this
    // full-score compare pick the same tier.
    for (const BonusEndingEntry& tier : kBonusEndings) {
        if (score >= tier.min_score) {
            return tier.rocket_sprite;
        }
    }
    return std::nullopt;
}

bool shouldLevelUp(std::uint16_t lines, std::uint8_t level) {
    assert(lines <= 9999 && "lines exceed the four-digit counter");
    assert(level <= kLevelCap && "level is out of range");

    // Mirrors the original's digit-shift compare: it drops the ones digit of the line counter and
    // compares the result against the level, both as BCD - an exact floor(lines / 10) > level in
    // decimal. Two gates guard it: the cap check, and the original's quirk that a nonzero
    // thousands digit stops all levelling forever.
    return lines < 1000 && level < kLevelCap && lines / kLinesPerLevel > level;
}

}  // namespace kirpich
