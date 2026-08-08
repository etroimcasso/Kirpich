#pragma once

// Scoring: the line-clear award table, the bonus-ending tiers, and the pure scoring math.
//
// Every score in the original is a BCD number - the hex digits are the decimal digits - and the
// running score lives in three such bytes, saturating at 999 999. This surface stores everything
// decoded: base scores as the decimal numbers 40/100/300/1200, thresholds as 200 000/150 000/
// 100 000. BCD is wire format only, and runtime BCD behavior (saturation on accumulation, digit
// rendering, byte-wise top-score comparison) belongs to the consumers that hold score state.
//
// The four functions mirror the original's award and selection math exactly, quirks included: a
// line clear awards base x (level + 1); a soft drop awards one point per row MINUS ONE (the
// original's own unexplained off-by-one, preserved); the game-over screen picks a rocket ending
// by walking the tiers highest-first; and Type-A levels up when floor(lines / 10) passes the
// level - unless the line counter has reached 1000, after which it never levels again.
//
// The tables and constants are generated from the disassembly by tools/asm_parser/parse_scoring.py;
// edit the values there and regenerate, not here. The behavioral spec - the award call sites, both
// soft-drop paths, the BCD equivalence arguments - lives in docs/contracts/scoring.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "kirpich/line_clear_kind.h"
#include "kirpich/sprite_id.h"

namespace kirpich {

// One row of the award table: the clear kind and its base score before the level multiplier.
struct LineClearScoreEntry {
    LineClearKind kind;    // identity - the kind this row is for; equals the row's array position
    std::uint16_t points;  // base award, multiplied by (level + 1) at award time

    friend constexpr bool operator==(const LineClearScoreEntry&,
                                     const LineClearScoreEntry&) = default;
};
static_assert(sizeof(LineClearScoreEntry) == 4, "kind pads to the 16-bit points field");

// One bonus-ending tier: the score it takes and the rocket sprite it shows.
struct BonusEndingEntry {
    std::uint32_t min_score;      // decoded decimal threshold, inclusive
    SpriteId      rocket_sprite;  // the rocket sprite this tier shows

    friend constexpr bool operator==(const BonusEndingEntry&, const BonusEndingEntry&) = default;
};

// kLineClearScores + kBonusEndings + kLevelCap / kTypeBLineGoal / kSoftDropPointsPerRow /
// kScoreSaturation, generated at namespace scope.
#include "generated/scoring_data.inc"

// A level advances every 10 lines; the divisor is implicit in the original's digit-shift compare
// (it drops the ones digit), so it is derived here rather than transcribed.
inline constexpr std::uint8_t kLinesPerLevel = 10;

// Every row sits at the position its .kind names, so indexing by kind is exact.
static_assert([] {
    for (std::size_t i = 0; i < kLineClearScores.size(); ++i) {
        if (kLineClearScores[i].kind != static_cast<LineClearKind>(i)) {
            return false;
        }
    }
    return true;
}(), "kLineClearScores rows must be in kind order, one row per kind");

// The tiers descend strictly, as the game checks them - first match wins in the lookup.
static_assert([] {
    for (std::size_t i = 1; i < kBonusEndings.size(); ++i) {
        if (kBonusEndings[i].min_score >= kBonusEndings[i - 1].min_score) {
            return false;
        }
    }
    return true;
}(), "kBonusEndings must be ordered by strictly descending threshold");

// The base award for a line clear at a level: base x (level + 1). Saturation is not applied here -
// it happens where the award accumulates into the score. `level` must not exceed kLevelCap.
[[nodiscard]] std::uint32_t lineClearAward(LineClearKind kind, std::uint8_t level);

// The points a piece earns for the rows it was soft-dropped: one per row, minus one - the
// original's own off-by-one, preserved from both of its soft-drop paths. Zero rows award zero.
[[nodiscard]] std::uint32_t softDropAward(std::uint8_t rows);

// The rocket sprite the Type-A game-over screen shows for this score, or nullopt below the
// lowest tier. `score` must not exceed kScoreSaturation.
[[nodiscard]] std::optional<SpriteId> rocketSpriteForScore(std::uint32_t score);

// Whether a Type-A game at this line count and level levels up now: true when
// floor(lines / 10) has passed the level, the level is below kLevelCap, and the line counter has
// not reached 1000 (past 999 the original stops levelling forever). `lines` must not exceed 9999
// (the original's four-digit counter), `level` must not exceed kLevelCap.
[[nodiscard]] bool shouldLevelUp(std::uint16_t lines, std::uint8_t level);

}  // namespace kirpich
