#pragma once

// The achievement set and the round-end check that awards it.
//
// Every achievement is a definition - an id, the section it is shown under, a display tier, whether it
// is hidden, a name, and a condition - and the whole set is one table (systems/achievements.cpp). A
// condition is one of a small closed vocabulary, each a cheap function of a finished round and the
// player's lifetime totals, so the evaluator is one switch over that vocabulary rather than a check
// per achievement. New achievements are new rows in the table; they need no new evaluator code unless
// they need a new condition shape.
//
// The check runs once, when a round ends. By then the level has finished climbing and the lines,
// score, drops and tetrises are all accumulated, so a single pass answers every condition. It reads
// the round's final state, the bonus scenes the round reached (observed by the scene handlers as they
// ran), and the player's lifetime slice totals (already updated with this round), and stamps every
// newly met achievement with the date and play time it was earned. First unlock wins: a met condition
// never overwrites an existing stamp.
//
// Nothing here draws anything. The screen that shows the set, and the notice shown when one is earned,
// come later; this is the layer that knows what has been earned and makes it persist.

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include <kirpich/game_type.h>

#include "state/achievement_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

// The wall clock the round-end check stamps an unlock with. The host holds a system clock behind it;
// a default (empty) callable yields the zero date, which is how a test pins an unlock date
// deterministically. It is separate from the statistics' NowNanos because a date is not a duration -
// the engine measures elapsed time and cannot report a calendar date, so the port injects one.
using NowDate = std::function<AchievementDate()>;

// The themed sections the set is grouped into. The achievements screen shows one section to a page;
// the grouping is a display concern, so it lives with the definitions rather than with the saved
// state.
enum class AchievementSection : std::uint8_t {
    ASCENT,        // Type A score
    ENDURANCE,     // lines
    THE_TETRIS,    // four-line clears
    DIG_OUT,       // Type B wins
    RISING_FLOOR,  // Type C
    REDLINE,       // level reached
    HAVE_A_HEART,  // heart mode
    LIFTOFF,       // the bonus scenes
    THE_LONG_GAME, // dedication
};

// Which bonus scene a SceneReached condition asks for.
enum class Scene : std::uint8_t {
    ROCKET,      // any rocket launch fired
    TOP_ROCKET,  // the highest-tier rocket, the one the biggest scores earn
    BURAN,       // the Height-5 Type B ending
};

// The closed vocabulary of conditions. Each achievement names exactly one, and conditionMet() below is
// one branch per kind. Adding a kind is the only reason the evaluator itself changes.
enum class ConditionKind : std::uint8_t {
    ScoreAtLeast,            // final round score >= threshold, in one game type (optionally heart, or a rise floor)
    LinesInRoundAtLeast,     // final round lines >= threshold, in one game type
    LifetimeLines,           // lifetime lines (all slices) >= threshold
    TetrisesInRound,         // four-line clears this round >= threshold
    LifetimeTetrises,        // lifetime tetrises >= threshold
    TypeBWin,                // a Type B win at level >= minLevel and start height >= minVariant (optionally heart)
    LevelReached,            // the round reached level >= threshold, any game type
    HeartRoundFinished,      // the round was played with heart mode on
    SceneReached,            // the round reached a named bonus scene
    LifetimeRounds,          // lifetime rounds (all slices) >= threshold
    AllGameTypesPlayed,      // at least one round recorded in each of Type A, B and C
    PlayedAllMusic,          // at least one round recorded under every music selection
    LifetimeDrops,           // lifetime pieces locked >= threshold
    ApplicationSecondsAtLeast,  // whole-application play time >= threshold seconds
};

// One condition. Only the fields its kind reads are meaningful; the rest keep their defaults. Kept as
// plain data (not a std::function) so the set is a literal table and each shape is tested on its own.
struct Condition {
    ConditionKind kind{};
    GameType      type          = GameType::TYPE_A;  // ScoreAtLeast / LinesInRoundAtLeast: which type
    std::uint32_t threshold     = 0;                 // the score / lines / tetrises / rounds / drops / seconds / level floor
    std::uint8_t  minLevel      = 0;                 // TypeBWin: the level the win must be at
    std::uint8_t  minVariant    = 0;                 // TypeBWin start height, or ScoreAtLeast Type C rise index
    bool          variantMatters = false;            // ScoreAtLeast reads minVariant only when this is set
    bool          requireHeart  = false;             // the round must have been in heart mode
    Scene         scene         = Scene::ROCKET;     // SceneReached: which scene

    friend constexpr bool operator==(const Condition&, const Condition&) = default;
};

// One achievement: what it is called, what it asks for, and the condition that decides it.
//
// `title` names it and `description` states its criterion in words - the two the screen shows when a
// badge is opened. Both are data beside the id, which is the stable identity, so either can be
// rewritten without touching logic or the save format; the condition is what is actually evaluated,
// and the description is the same requirement said in the font's own vocabulary. `hidden` decides only
// what the screen draws for a locked achievement; it changes nothing about how one is earned.
struct AchievementDef {
    AchievementId      id{};
    AchievementSection section{};
    std::uint8_t       tier   = 1;  // 1 Casual .. 4 Mastery; display ordering only, no point value
    bool               hidden = true;
    std::string_view   title{};
    std::string_view   description{};
    Condition          condition{};
};

// The whole set, in AchievementId order. Static, because it is definitions, not state.
[[nodiscard]] std::span<const AchievementDef> achievementSet();

// Run the round-end check: walk the set, and for every condition met by the just-finished round that
// is not already unlocked, stamp it with `now`'s date and the current play time. Does nothing unless a
// round has concluded and is awaiting the check (so a second call from another exit is a no-op, and a
// demo - never armed - earns nothing); clears the round's observations when it returns.
void evaluateRoundEnd(GameContext& game, const NowDate& now);

// How many achievements the player has unlocked, and how many there are. The screen's "Achievements
// Unlocked  #/#" stat, its menu-row count and its hidden-trophy reveal all read these, so the count
// lives in one place rather than being recomputed per surface.
[[nodiscard]] std::size_t achievementsUnlocked(const GameContext& game);
[[nodiscard]] constexpr std::size_t achievementsTotal() noexcept { return kAchievementCount; }

}  // namespace kirpich::systems
