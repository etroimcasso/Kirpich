#include "systems/stats.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace kirpich::systems {

namespace {

constexpr std::uint64_t kNanosPerSecond = 1'000'000'000ULL;

// Add without wrapping. A count that reaches its ceiling stays there, which is wrong by less than a
// count that starts again from zero.
void addSaturating(std::uint32_t& into, std::uint32_t amount) {
    const std::uint32_t room = std::numeric_limits<std::uint32_t>::max() - into;
    into += amount < room ? amount : room;
}

// The slice a round belongs to. Both indices are held inside the tables: the difficulty screens keep
// them in range, and a table this size is not worth leaving open to a stray value.
StatSlice& sliceFor(StatsState& stats, const RoundInProgress& round) {
    const std::size_t level   = std::min<std::size_t>(round.level, kStatLevels - 1);
    const std::size_t variant = std::min<std::size_t>(round.variant, kStatVariants - 1);
    switch (round.type) {
        case GameType::TYPE_B: return stats.typeB[level][variant];
        case GameType::TYPE_C: return stats.typeC[level][variant];
        case GameType::TYPE_A: break;
    }
    return stats.typeA[level];
}

// Everything played in this round: what was banked before the current stretch, plus the stretch
// itself. A clock that has not moved contributes nothing, and one that appears to run backwards
// contributes nothing rather than an enormous number.
std::uint64_t elapsedNanos(const RoundInProgress& round, std::uint64_t nowNanos) {
    const std::uint64_t sinceStamp = nowNanos > round.stampNanos ? nowNanos - round.stampNanos : 0;
    return round.bankedNanos + sinceStamp;
}

std::uint32_t wholeSeconds(std::uint64_t nanos) {
    const std::uint64_t seconds = nanos / kNanosPerSecond;
    const std::uint64_t ceiling = std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(std::min(seconds, ceiling));
}

// Fold one slice into a running total: the nine counts add, and the longest round takes the larger
// of the two. Summing that one would report a number no round ever lasted.
void fold(StatSlice& into, const StatSlice& from) {
    addSaturating(into.rounds, from.rounds);
    addSaturating(into.seconds, from.seconds);
    into.longestRoundSeconds = std::max(into.longestRoundSeconds, from.longestRoundSeconds);
    addSaturating(into.drops, from.drops);
    addSaturating(into.score, from.score);
    addSaturating(into.lines, from.lines);
    addSaturating(into.singles, from.singles);
    addSaturating(into.doubles, from.doubles);
    addSaturating(into.triples, from.triples);
    addSaturating(into.tetrises, from.tetrises);
}

}  // namespace

void beginRound(GameContext& game, std::uint64_t nowNanos) {
    // A round that was never ended closes here rather than leaking its counts into this one.
    endRound(game, nowNanos);

    // An attract demo plays through the same round pipeline a player does. Leaving the round
    // inactive is the whole of the exclusion: every other call below does nothing while inactive.
    if (game.demo.activeDemo != ActiveDemo::NONE) return;

    const RoundCombination at    = combinationOf(game.flow);
    RoundInProgress&       round = game.stats.round;

    round.active      = true;
    round.type        = at.type;
    round.level       = at.level;
    round.variant     = at.variant;
    round.hasVariant  = at.hasVariant;
    round.stampNanos  = nowNanos;
    round.bankedNanos = 0;
}

void endRound(GameContext& game, std::uint64_t nowNanos) {
    RoundInProgress& round = game.stats.round;
    if (!round.active) return;

    const std::uint32_t seconds = wholeSeconds(elapsedNanos(round, nowNanos));

    StatSlice& slice = sliceFor(game.stats, round);
    addSaturating(slice.rounds, 1);
    addSaturating(slice.seconds, seconds);
    slice.longestRoundSeconds = std::max(slice.longestRoundSeconds, seconds);

    // The round's final score, taken once at the end. The score has its own per-round ceiling, and
    // adding each award as it lands would compound that ceiling across a lifetime.
    addSaturating(slice.score, game.engine.score);

    round = RoundInProgress{};
}

void pauseRound(GameContext& game, std::uint64_t nowNanos) {
    RoundInProgress& round = game.stats.round;
    if (!round.active) return;
    round.bankedNanos = elapsedNanos(round, nowNanos);
    round.stampNanos  = nowNanos;
}

void resumeRound(GameContext& game, std::uint64_t nowNanos) {
    RoundInProgress& round = game.stats.round;
    if (!round.active) return;
    round.stampNanos = nowNanos;
}

void recordDrop(GameContext& game) {
    if (!game.stats.round.active) return;
    addSaturating(sliceFor(game.stats, game.stats.round).drops, 1);
}

void recordLineClear(GameContext& game, std::uint8_t rows) {
    if (!game.stats.round.active || rows == 0) return;

    StatSlice& slice = sliceFor(game.stats, game.stats.round);

    // Counted here rather than read from the flow at the end: Type A counts its lines up and Type B
    // counts them down to zero, so only the clears themselves say the same thing in every mode.
    addSaturating(slice.lines, rows);

    switch (rows) {
        case 1:  addSaturating(slice.singles, 1); break;
        case 2:  addSaturating(slice.doubles, 1); break;
        case 3:  addSaturating(slice.triples, 1); break;
        default: addSaturating(slice.tetrises, 1); break;
    }
}

void beginSession(GameContext& game, std::uint64_t nowNanos) {
    game.stats.applicationStampNanos  = nowNanos;
    game.stats.applicationBankedNanos = 0;
}

void bankApplicationTime(GameContext& game, std::uint64_t nowNanos) {
    StatsState& stats = game.stats;

    const std::uint64_t since =
        nowNanos > stats.applicationStampNanos ? nowNanos - stats.applicationStampNanos : 0;
    stats.applicationStampNanos = nowNanos;
    stats.applicationBankedNanos += since;

    // Whole seconds go to the stored total and the remainder stays banked, so a run of short banks
    // adds up to the same total one long one would.
    const std::uint64_t whole = stats.applicationBankedNanos / kNanosPerSecond;
    stats.applicationBankedNanos -= whole * kNanosPerSecond;
    addSaturating(stats.applicationSeconds, wholeSeconds(whole * kNanosPerSecond));
}

StatSlice totalsFor(const StatsState& stats, GameType type) {
    StatSlice total;
    switch (type) {
        case GameType::TYPE_B:
            for (const auto& level : stats.typeB) {
                for (const auto& slice : level) fold(total, slice);
            }
            return total;
        case GameType::TYPE_C:
            for (const auto& level : stats.typeC) {
                for (const auto& slice : level) fold(total, slice);
            }
            return total;
        case GameType::TYPE_A:
            break;
    }
    for (const auto& slice : stats.typeA) fold(total, slice);
    return total;
}

StatSlice lifetimeTotals(const StatsState& stats) {
    StatSlice total;
    fold(total, totalsFor(stats, GameType::TYPE_A));
    fold(total, totalsFor(stats, GameType::TYPE_B));
    fold(total, totalsFor(stats, GameType::TYPE_C));
    return total;
}

LongestRound longestRound(const StatsState& stats) {
    LongestRound best;

    // Strictly greater, so the first slice in this walk keeps a tie.
    const auto consider = [&best](const StatSlice& slice, const RoundCombination& at) {
        if (slice.rounds == 0) return;
        if (best.any && slice.longestRoundSeconds <= best.seconds) return;
        best.seconds = slice.longestRoundSeconds;
        best.at      = at;
        best.any     = true;
    };

    for (std::size_t level = 0; level < kStatLevels; ++level) {
        consider(stats.typeA[level], {.type       = GameType::TYPE_A,
                                      .level      = static_cast<std::uint8_t>(level),
                                      .variant    = 0,
                                      .hasVariant = false});
    }
    for (std::size_t level = 0; level < kStatLevels; ++level) {
        for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
            consider(stats.typeB[level][variant], {.type       = GameType::TYPE_B,
                                                   .level      = static_cast<std::uint8_t>(level),
                                                   .variant    = static_cast<std::uint8_t>(variant),
                                                   .hasVariant = true});
        }
    }
    for (std::size_t level = 0; level < kStatLevels; ++level) {
        for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
            consider(stats.typeC[level][variant], {.type       = GameType::TYPE_C,
                                                   .level      = static_cast<std::uint8_t>(level),
                                                   .variant    = static_cast<std::uint8_t>(variant),
                                                   .hasVariant = true});
        }
    }

    return best;
}

DurationText formatDuration(std::uint32_t seconds) {
    DurationText text;

    const unsigned hours   = static_cast<unsigned>(seconds / 3600);
    const unsigned minutes = static_cast<unsigned>((seconds / 60) % 60);
    const unsigned rest    = static_cast<unsigned>(seconds % 60);

    // Lowercase, as every other string these screens draw is: the font has one case, and the source
    // reads the way the screen does.
    const int written =
        hours > 0
            ? std::snprintf(text.chars.data(), text.chars.size(), "%uh %02um", hours, minutes)
            : std::snprintf(text.chars.data(), text.chars.size(), "%um %02us", minutes, rest);

    text.size = written > 0 ? static_cast<std::uint8_t>(written) : 0;
    return text;
}

}  // namespace kirpich::systems
