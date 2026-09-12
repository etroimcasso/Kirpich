#pragma once

// What the player has earned, kept across launches: one unlock record per achievement, plus the
// current round's own observations that feed the round-end check.
//
// An achievement is identified by AchievementId, a stable enumerator. The persisted array below is
// indexed by that id, so the save is keyed by identity rather than by table position: the set can
// grow (new ids appended, the array widened, a migration padding the old records forward) without
// disturbing what a player has already earned. Reordering an existing id would move its saved record,
// so the enumerators are fixed once shipped.
//
// The set's meaning - what each id asks the player to do, which section it belongs to, whether it is
// hidden - lives in systems/achievements.h. Only the unlock records and the per-round observations
// live here, because only they are state; the definitions are behaviour.
//
// The unlock records persist in their own save document (src/state/achievement_persistence.h). The
// round block below does not: it is the current round's bookkeeping and means nothing after the
// program stops, the same way StatsState's round-in-progress and HighScoreState's name-entry bytes are
// left out of their own documents.

#include <array>
#include <cstddef>
#include <cstdint>

namespace kirpich {

// Every achievement the game can award. The enumerators are the stable persistence key (see the file
// note); their order is the order the records sit in on the wire. Grouped by the themed section they
// belong to, which systems/achievements.h names - the grouping here is for the reader, not a contract.
enum class AchievementId : std::uint16_t {
    // The Ascent - Type A score
    GETTING_WARM = 0,
    STACKING_UP,
    IN_THE_ZONE,
    SIX_FIGURES,
    DOUBLE_CENTURY,
    KIRPICH_LEGEND,

    // Endurance - lines
    LINE_WORKER,
    LINE_MACHINE,
    THE_LONG_HAUL,
    TEN_THOUSAND,

    // The Tetris - four at once
    FOUR_AT_ONCE,
    TETRIS_TIMES_TEN,
    TETRIS_THOUSAND,

    // Dig Out - Type B wins
    DUG_OUT,
    IN_DEEP,
    BURIED,
    ENTOMBED,
    MIRACLE_DIG,

    // Rising Floor - Type C
    HOLD_GROUND,
    HIGH_WATER,
    BEDROCK,

    // Redline - level reached
    NINTH_GEAR,
    REDLINE,
    TERMINAL_VELOCITY,

    // Have A Heart - heart mode
    HAVE_A_HEART,
    HEART_OF_STEEL,
    TRUE_BELIEVER,

    // Liftoff - the bonus scenes
    LIFTOFF,
    ESCAPE_VELOCITY,
    COSMONAUT,

    // The Long Game - dedication
    REGULAR,
    DEVOTEE,
    WELL_ROUNDED,
    ALL_EARS,
    PIECE_BY_PIECE,
    TIME_SINK,
};

// How many achievements there are. Tied to the last enumerator so the persisted array and the
// definition table cannot drift from the enum they are indexed by.
inline constexpr std::size_t kAchievementCount = static_cast<std::size_t>(AchievementId::TIME_SINK) + 1;

// The array position of an id.
[[nodiscard]] constexpr std::size_t achievementIndex(AchievementId id) noexcept {
    return static_cast<std::size_t>(id);
}

// A calendar date, as the injected wall clock reports it. The stamp keeps it packed (below); this is
// the shape it is read and written in. A default (all-zero) date is what an unset clock yields, which
// is how a test pins an unlock date deterministically.
struct AchievementDate {
    std::uint16_t year  = 0;
    std::uint8_t  month = 0;
    std::uint8_t  day   = 0;

    friend constexpr bool operator==(const AchievementDate&, const AchievementDate&) = default;
};

// The date packed into one value, year-month-day as a decimal number (2026-09-07 becomes 20260907),
// so it stores in a single field and reads back for display without calendar arithmetic. Zero means
// "no date", the value an unset clock packs to.
[[nodiscard]] constexpr std::uint32_t packAchievementDate(const AchievementDate& date) noexcept {
    return static_cast<std::uint32_t>(date.year) * 10000u +
           static_cast<std::uint32_t>(date.month) * 100u + static_cast<std::uint32_t>(date.day);
}

[[nodiscard]] constexpr AchievementDate unpackAchievementDate(std::uint32_t packed) noexcept {
    return {.year  = static_cast<std::uint16_t>(packed / 10000u),
            .month = static_cast<std::uint8_t>((packed / 100u) % 100u),
            .day   = static_cast<std::uint8_t>(packed % 100u)};
}

// One achievement's earned state: whether it is unlocked, and - if it is - the calendar date it was
// earned and the whole-application play time at that moment (StatsState::applicationSeconds, captured
// rather than computed). Both stamps are meaningful only while `unlocked` is true.
struct AchievementUnlock {
    bool          unlocked            = false;
    std::uint32_t datePacked          = 0;  // packAchievementDate at the moment it fired
    std::uint32_t playSecondsAtUnlock = 0;  // StatsState::applicationSeconds at the moment it fired

    friend constexpr bool operator==(const AchievementUnlock&, const AchievementUnlock&) = default;
};

// The current round's own observations, gathered as the round plays out and read once when it ends.
// Not persisted: it is reset when a real round begins and cleared again when the round-end check
// consumes it.
//
// `active` is set only by beginAchievementRound, which the recording layer calls only for a real
// round (an attract demo is turned away before it), so nothing here is ever armed for a demo. The
// round-end check requires `pendingEval`, which noteRoundConcluded sets only while `active`, so a demo
// earns nothing with no separate gate.
struct AchievementRound {
    bool active      = false;  // a real (non-demo) round is in progress
    bool pendingEval = false;  // it has concluded and awaits the round-end check
    bool wonTypeB    = false;  // the concluded round was a Type B win, not a top-out

    // The bonus scenes this round reached, set when each scene's handler runs.
    bool rocketReached    = false;
    bool topRocketReached = false;  // the highest-tier rocket, not just any
    bool buranReached     = false;

    std::uint32_t tetrises = 0;  // four-line clears counted this round

    friend constexpr bool operator==(const AchievementRound&, const AchievementRound&) = default;
};

struct AchievementState {
    // Indexed by AchievementId. Persists in the "achievements" save document.
    std::array<AchievementUnlock, kAchievementCount> unlocked{};

    // The current round's observations. Never written to disk.
    AchievementRound round{};

    // Return every field to its boot (all-zero) value. The startup load fills the records back in from
    // the save document; the reset chord keeps them by hand, because they outlive it.
    void reset() { *this = AchievementState{}; }

    friend bool operator==(const AchievementState&, const AchievementState&) = default;
};

// ── The round's observation seams ───────────────────────────────────────────────────────────────────
//
// The gameplay handlers set the round's observations through these rather than by reaching into the
// fields, so the round bookkeeping reads the same wherever it is written and stays out of the handlers
// themselves. Each takes the state directly (not the whole GameContext) so a handler can call it
// without depending on the achievements system.

// A real round is starting: clear last round's observations and arm this one. Called only for a round
// that actually records (the recording layer refuses a demo before this), which is what keeps the
// round-end check off attract play.
inline void beginAchievementRound(AchievementState& state) noexcept {
    state.round        = AchievementRound{};
    state.round.active = true;
}

// A round has ended. `wonTypeB` is true for a Type B win, false for a top-out. Arms the round-end
// check, but only for an armed (real) round, so a demo that somehow reached a conclusion handler earns
// nothing.
inline void noteRoundConcluded(AchievementState& state, bool wonTypeB) noexcept {
    if (!state.round.active) return;
    state.round.pendingEval = true;
    state.round.wonTypeB    = wonTypeB;
}

// A four-line clear just landed. Counts toward this round's tetris tally, read at round end.
inline void noteRoundTetris(AchievementState& state) noexcept { ++state.round.tetrises; }

// The rocket bonus scene ran this round; `topTier` says it was the highest-scoring rocket rather than
// a lower one.
inline void noteRocketScene(AchievementState& state, bool topTier) noexcept {
    state.round.rocketReached = true;
    if (topTier) state.round.topRocketReached = true;
}

// The Buran bonus scene ran this round.
inline void noteBuranScene(AchievementState& state) noexcept { state.round.buranReached = true; }

}  // namespace kirpich
