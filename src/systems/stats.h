#pragma once

// Recording what was played, and reading it back.
//
// A round is timed by stamping rather than counting: it takes the clock when play starts and again
// when it ends, and the difference is what it cost. Pausing banks the stretch just played and
// unpausing re-stamps, so time spent paused - or on a screen opened from a pause - is never part of
// it. Nothing here runs per frame.
//
// Every count goes to the combination the round was STARTED in, latched once at the beginning. A
// Type A round levels up as it runs and still belongs to the level it was picked at, which is the
// same rule its top score follows.
//
// One gate covers the attract demo. A demo runs the same round pipeline as a player does, so
// beginRound refuses while one is running and leaves the round inactive; every other call here does
// nothing while inactive. That is why a title screen left alone all night records nothing.
//
// Recording does not consult any setting - the stats screen can be switched off and the tables still
// fill, so a player who turns it on later finds their whole history there.

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>

#include "state/game_flow_state.h"  // RoundCombination
#include "state/stats_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

// The clock the timed calls below read, in nanoseconds from any fixed origin. It reaches the
// gameplay handlers through GameplayWiring and the host holds it directly; only the engine's
// monotonic Clock is ever behind it, so it never runs backwards.
using NowNanos = std::function<std::uint64_t()>;

// ── Recording ─────────────────────────────────────────────────────────────────────────────────────

// Start recording a round. Closes out one still open first, so a round that was never ended cannot
// leak into the next; refuses while a demo is running, which is what keeps attract play out of the
// tables.
void beginRound(GameContext& game, std::uint64_t nowNanos);

// Finish the round: add its time, its score and its round to the slice it was started in, and let its
// length compete for that slice's longest. Idempotent - calling it when no round is open does
// nothing, so the several ways a round can end may each call it without counting twice.
void endRound(GameContext& game, std::uint64_t nowNanos);

// Bank the stretch just played, and start a new one. Called either side of a pause.
void pauseRound(GameContext& game, std::uint64_t nowNanos);
void resumeRound(GameContext& game, std::uint64_t nowNanos);

// One piece has come to rest.
void recordDrop(GameContext& game);

// `rows` rows have been cleared at once: counts the lines, and the clear under its own kind.
void recordLineClear(GameContext& game, std::uint8_t rows);

// ── The program's own running time ────────────────────────────────────────────────────────────────

// Start the session's clock. The host calls this once, at startup.
void beginSession(GameContext& game, std::uint64_t nowNanos);

// Fold everything played since the last call into the stored total, keeping the part of a second
// that does not yet make a whole one. The host calls this wherever it already saves, so a crash
// costs the time since then rather than the whole session.
void bankApplicationTime(GameContext& game, std::uint64_t nowNanos);

// ── Reading it back ───────────────────────────────────────────────────────────────────────────────

// One game type's totals, and the whole game's. Both are folds over the slices - the nine running
// counts add, and the longest round takes the larger of the two rather than their sum.
[[nodiscard]] StatSlice totalsFor(const StatsState& stats, GameType type);
[[nodiscard]] StatSlice lifetimeTotals(const StatsState& stats);

// The longest single round anywhere, and where it was played. The combination is the slice the round
// was found in rather than a stored field, so the length and the label it is shown under cannot
// disagree. Ties go to the first slice in walk order - Type A by level, then Type B and Type C by
// level and then by their second axis. `any` is false when nothing has been played at all.
struct LongestRound {
    std::uint32_t    seconds = 0;
    RoundCombination at{};
    bool             any = false;
};

[[nodiscard]] LongestRound longestRound(const StatsState& stats);

// ── Showing a duration ────────────────────────────────────────────────────────────────────────────

// A formatted duration, carried by value so it can be handed straight to a text write without
// outliving a call. The font has no colon, so an hour and a minute are spelled with their own
// letters: "2H 05M" from an hour up, and "5M 03S" below one.
struct DurationText {
    std::array<char, 16> chars{};
    std::uint8_t         size = 0;

    [[nodiscard]] std::string_view view() const { return {chars.data(), size}; }
};

[[nodiscard]] DurationText formatDuration(std::uint32_t seconds);

}  // namespace kirpich::systems
