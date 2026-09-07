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
// them in range, and a table this size is not worth leaving open to a stray value. A heart round
// routes to the parallel heart tables, the whole of heart's effect on recording.
StatSlice& sliceFor(StatsState& stats, const RoundInProgress& round) {
    const std::size_t level   = std::min<std::size_t>(round.level, kStatLevels - 1);
    const std::size_t variant = std::min<std::size_t>(round.variant, kStatVariants - 1);
    if (round.heart) {
        switch (round.type) {
            case GameType::TYPE_B: return stats.typeBHeart[level][variant];
            case GameType::TYPE_C: return stats.typeCHeart[level][variant];
            case GameType::TYPE_A: break;
        }
        return stats.typeAHeart[level];
    }
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
    for (std::size_t kind = 0; kind < kPieceKindCount; ++kind) {
        addSaturating(into.pieces[kind], from.pieces[kind]);
    }
}

// The table set a scope selects. Each accessor picks the cartridge or the heart table for one game
// type by a bool, so every fold below reads the right tables by naming that bool once.
const std::array<StatSlice, kStatLevels>& typeATable(const StatsState& stats, bool heart) {
    return heart ? stats.typeAHeart : stats.typeA;
}
const std::array<std::array<StatSlice, kStatVariants>, kStatLevels>& typeBTable(
    const StatsState& stats, bool heart) {
    return heart ? stats.typeBHeart : stats.typeB;
}
const std::array<std::array<StatSlice, kStatVariants>, kStatLevels>& typeCTable(
    const StatsState& stats, bool heart) {
    return heart ? stats.typeCHeart : stats.typeC;
}

// Run `body(heart)` over the table sets a scope covers: the cartridge set, the heart set, or both.
// Every scoped fold is this walk over one or two sets, so the scope-to-sets mapping lives in one
// place.
template <typename Body>
void forEachSet(StatScope scope, Body&& body) {
    switch (scope) {
        case StatScope::NORMAL: body(false); return;
        case StatScope::HEART:  body(true); return;
        case StatScope::ALL:    body(false); body(true); return;
    }
}

// Fold one game type's table (cartridge or heart) into `total`, taking only the levels and variants a
// selection wants. `everyLevel` / `everyVariant` fold that axis away; otherwise only the one value is
// taken. Type A is picked by level alone, so its second axis is never consulted. This is the shared
// body under both totalsFor (which folds every level and variant) and totalsForSelection (which
// filters them).
void foldTypeSelection(StatSlice& total, const StatsState& stats, GameType type, bool heart,
                       bool everyLevel, std::size_t wantLevel, bool everyVariant,
                       std::size_t wantVariant) {
    const auto wantedLevel   = [&](std::size_t level) { return everyLevel || level == wantLevel; };
    const auto wantedVariant = [&](std::size_t v) { return everyVariant || v == wantVariant; };

    switch (type) {
        case GameType::TYPE_B:
        case GameType::TYPE_C: {
            const auto& table =
                type == GameType::TYPE_B ? typeBTable(stats, heart) : typeCTable(stats, heart);
            for (std::size_t level = 0; level < kStatLevels; ++level) {
                if (!wantedLevel(level)) continue;
                for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
                    if (wantedVariant(variant)) fold(total, table[level][variant]);
                }
            }
            return;
        }
        case GameType::TYPE_A:
            break;
    }
    const auto& table = typeATable(stats, heart);
    for (std::size_t level = 0; level < kStatLevels; ++level) {
        if (wantedLevel(level)) fold(total, table[level]);
    }
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
    round.heart       = at.heart;
    round.stampNanos  = nowNanos;
    round.bankedNanos = 0;

    // The song this round is being played under. Counted at the start rather than at the end because
    // it is a property of the round as it was set up, exactly as the combination above is - and
    // counted here rather than in a slice because music is not part of a combination.
    const std::size_t music = musicTypeIndex(game.flow.musicType);
    if (music < kMusicTypeCount) {
        addSaturating(game.stats.musicRounds[music], 1);
    }
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

void recordPiece(GameContext& game, PieceKind kind) {
    if (!game.stats.round.active) return;

    const auto index = static_cast<std::size_t>(kind);
    if (index >= kPieceKindCount) return;  // not one of the seven; nowhere to put it

    addSaturating(sliceFor(game.stats, game.stats.round).pieces[index], 1);
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

StatSlice totalsFor(const StatsState& stats, GameType type, StatScope scope) {
    StatSlice total;
    forEachSet(scope, [&](bool heart) {
        foldTypeSelection(total, stats, type, heart, /*everyLevel=*/true, 0, /*everyVariant=*/true, 0);
    });
    return total;
}

StatSlice lifetimeTotals(const StatsState& stats, StatScope scope) {
    StatSlice total;
    fold(total, totalsFor(stats, GameType::TYPE_A, scope));
    fold(total, totalsFor(stats, GameType::TYPE_B, scope));
    fold(total, totalsFor(stats, GameType::TYPE_C, scope));
    return total;
}

LongestRound longestRound(const StatsState& stats, StatScope scope) {
    LongestRound best;

    // Strictly greater, so the first slice in this walk keeps a tie.
    const auto consider = [&best](const StatSlice& slice, const RoundCombination& at) {
        if (slice.rounds == 0) return;
        if (best.any && slice.longestRoundSeconds <= best.seconds) return;
        best.seconds = slice.longestRoundSeconds;
        best.at      = at;
        best.any     = true;
    };

    // One walk over one table set, carrying the set's heart-ness in the combination so the winner
    // remembers which set it came from. Under ALL this runs for the cartridge set first and the heart
    // set second, which is what keeps a cross-set tie on the cartridge slice.
    const auto walk = [&](bool heart) {
        for (std::size_t level = 0; level < kStatLevels; ++level) {
            consider(typeATable(stats, heart)[level],
                     {.type       = GameType::TYPE_A,
                      .level      = static_cast<std::uint8_t>(level),
                      .variant    = 0,
                      .hasVariant = false,
                      .heart      = heart});
        }
        for (std::size_t level = 0; level < kStatLevels; ++level) {
            for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
                consider(typeBTable(stats, heart)[level][variant],
                         {.type       = GameType::TYPE_B,
                          .level      = static_cast<std::uint8_t>(level),
                          .variant    = static_cast<std::uint8_t>(variant),
                          .hasVariant = true,
                          .heart      = heart});
            }
        }
        for (std::size_t level = 0; level < kStatLevels; ++level) {
            for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
                consider(typeCTable(stats, heart)[level][variant],
                         {.type       = GameType::TYPE_C,
                          .level      = static_cast<std::uint8_t>(level),
                          .variant    = static_cast<std::uint8_t>(variant),
                          .hasVariant = true,
                          .heart      = heart});
            }
        }
    };
    forEachSet(scope, walk);

    return best;
}

std::uint32_t roundsFor(const StatsState& stats, GameType type, StatScope scope) {
    return totalsFor(stats, type, scope).rounds;
}

bool heartEverRecorded(const StatsState& stats) {
    for (const auto& slice : stats.typeAHeart) {
        if (slice.rounds != 0) return true;
    }
    for (const auto& level : stats.typeBHeart) {
        for (const auto& slice : level) {
            if (slice.rounds != 0) return true;
        }
    }
    for (const auto& level : stats.typeCHeart) {
        for (const auto& slice : level) {
            if (slice.rounds != 0) return true;
        }
    }
    return false;
}

FavouriteMode favouriteMode(const StatsState& stats, StatScope scope) {
    FavouriteMode best;

    // Strictly greater again, so the first type in this walk keeps a tie.
    const auto consider = [&best](GameType type, std::uint32_t rounds) {
        if (rounds == 0) return;
        if (best.any && rounds <= best.rounds) return;
        best = FavouriteMode{.type = type, .rounds = rounds, .any = true};
    };

    consider(GameType::TYPE_A, roundsFor(stats, GameType::TYPE_A, scope));
    consider(GameType::TYPE_B, roundsFor(stats, GameType::TYPE_B, scope));
    consider(GameType::TYPE_C, roundsFor(stats, GameType::TYPE_C, scope));
    return best;
}

FavouriteMusic favouriteMusic(const StatsState& stats) {
    FavouriteMusic best;

    for (std::size_t music = 0; music < kMusicTypeCount; ++music) {
        const std::uint32_t rounds = stats.musicRounds[music];
        if (rounds == 0) continue;
        if (best.any && rounds <= best.rounds) continue;

        // The four selections are contiguous from MUSIC_A, which is the arithmetic musicTypeIndex
        // performs in the other direction.
        const auto first = static_cast<std::uint8_t>(MusicType::MUSIC_A);
        best             = FavouriteMusic{
                        .type   = static_cast<MusicType>(first + static_cast<std::uint8_t>(music)),
                        .rounds = rounds,
                        .any    = true};
    }
    return best;
}

PreferredLevel preferredLevel(const StatsState& stats, StatScope scope) {
    PreferredLevel best;

    // One walk over one table set: for each level, the rounds played at it across the set's three
    // game types. Under ALL this runs for the cartridge set first and the heart set second, so a
    // cartridge level and the heart level of the same number are separate candidates and a tie keeps
    // the cartridge one. `best.heart` remembers which set the winner came from.
    const auto walk = [&](bool heart) {
        for (std::size_t level = 0; level < kStatLevels; ++level) {
            // A starting level is picked in all three game types, so the count for one is the rounds
            // played at that level across every one of them.
            std::uint32_t rounds = 0;
            addSaturating(rounds, typeATable(stats, heart)[level].rounds);
            for (std::size_t variant = 0; variant < kStatVariants; ++variant) {
                addSaturating(rounds, typeBTable(stats, heart)[level][variant].rounds);
                addSaturating(rounds, typeCTable(stats, heart)[level][variant].rounds);
            }

            if (rounds == 0) continue;
            if (best.any && rounds <= best.rounds) continue;
            best = PreferredLevel{.level  = static_cast<std::uint8_t>(level),
                                  .rounds = rounds,
                                  .any    = true,
                                  .heart  = heart};
        }
    };
    forEachSet(scope, walk);

    return best;
}

StatSlice totalsForSelection(const StatsState& stats, const StatSelection& selection) {
    // The per-mode pages carry their scope on the level axis rather than in a field, because the
    // level picker is where the player chooses it: positions 0-9 are the cartridge levels, 10-19 are
    // the same numbers played in heart mode, and anything past them - kStatAxisAll, or a value left
    // over from a narrower table - folds every level of both. Decode that here into a scope and a
    // level within the chosen set. The variant axis is not overloaded: a heart Type B round is still
    // (level, height), and its heart-ness is the level half.
    StatScope   scope      = StatScope::ALL;
    bool        everyLevel = true;
    std::size_t level      = 0;
    if (selection.level < kStatLevels) {
        scope      = StatScope::NORMAL;
        everyLevel = false;
        level      = selection.level;
    } else if (selection.level < 2 * kStatLevels) {
        scope      = StatScope::HEART;
        everyLevel = false;
        level      = selection.level - kStatLevels;
    }

    const bool everyVariant = selection.variant >= kStatVariants;

    StatSlice total;
    forEachSet(scope, [&](bool heart) {
        foldTypeSelection(total, stats, selection.type, heart, everyLevel, level, everyVariant,
                          selection.variant);
    });
    return total;
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
