#include "systems/achievements.h"

#include <array>
#include <cstddef>

#include <kirpich/music_type.h>

#include "state/game_flow_state.h"  // RoundCombination, combinationOf
#include "state/stats_state.h"      // StatSlice, StatScope
#include "systems/stats.h"          // lifetimeTotals, roundsFor

namespace kirpich::systems {

namespace {

// The set, in AchievementId order. Names are placeholders (data, not identity - the id is stable) to
// be copy-passed later; hidden is true for all but a small visible starter tier so a new player sees
// there is something to chase. Every threshold is a fact about the game, decided against what the port
// already records.
constexpr std::array<AchievementDef, kAchievementCount> kSet{{
    // The Ascent - Type A score
    {.id = AchievementId::GETTING_WARM, .section = AchievementSection::ASCENT, .tier = 1,
     .hidden = false, .name = "getting warm",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 5000}},
    {.id = AchievementId::STACKING_UP, .section = AchievementSection::ASCENT, .tier = 1,
     .name = "stacking up",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 25000}},
    {.id = AchievementId::IN_THE_ZONE, .section = AchievementSection::ASCENT, .tier = 2,
     .name = "in the zone",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 50000}},
    {.id = AchievementId::SIX_FIGURES, .section = AchievementSection::ASCENT, .tier = 3,
     .name = "six figures",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 100000}},
    {.id = AchievementId::DOUBLE_CENTURY, .section = AchievementSection::ASCENT, .tier = 4,
     .name = "double century",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 200000}},
    {.id = AchievementId::KIRPICH_LEGEND, .section = AchievementSection::ASCENT, .tier = 4,
     .name = "kirpich legend",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 500000}},

    // Endurance - lines
    {.id = AchievementId::LINE_WORKER, .section = AchievementSection::ENDURANCE, .tier = 1,
     .name = "line worker",
     .condition = {.kind = ConditionKind::LinesInRoundAtLeast, .type = GameType::TYPE_A,
                   .threshold = 50}},
    {.id = AchievementId::LINE_MACHINE, .section = AchievementSection::ENDURANCE, .tier = 3,
     .name = "line machine",
     .condition = {.kind = ConditionKind::LinesInRoundAtLeast, .type = GameType::TYPE_A,
                   .threshold = 150}},
    {.id = AchievementId::THE_LONG_HAUL, .section = AchievementSection::ENDURANCE, .tier = 4,
     .name = "the long haul",
     .condition = {.kind = ConditionKind::LinesInRoundAtLeast, .type = GameType::TYPE_A,
                   .threshold = 300}},
    {.id = AchievementId::TEN_THOUSAND, .section = AchievementSection::ENDURANCE, .tier = 3,
     .name = "ten thousand",
     .condition = {.kind = ConditionKind::LifetimeLines, .threshold = 10000}},

    // The Tetris - four at once
    {.id = AchievementId::FOUR_AT_ONCE, .section = AchievementSection::THE_TETRIS, .tier = 1,
     .hidden = false, .name = "four at once",
     .condition = {.kind = ConditionKind::LifetimeTetrises, .threshold = 1}},
    {.id = AchievementId::TETRIS_TIMES_TEN, .section = AchievementSection::THE_TETRIS, .tier = 3,
     .name = "tetris times ten",
     .condition = {.kind = ConditionKind::TetrisesInRound, .threshold = 10}},
    {.id = AchievementId::TETRIS_THOUSAND, .section = AchievementSection::THE_TETRIS, .tier = 4,
     .name = "tetris thousand",
     .condition = {.kind = ConditionKind::LifetimeTetrises, .threshold = 1000}},

    // Dig Out - Type B wins at level 9
    {.id = AchievementId::DUG_OUT, .section = AchievementSection::DIG_OUT, .tier = 2,
     .name = "dug out",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 1}},
    {.id = AchievementId::IN_DEEP, .section = AchievementSection::DIG_OUT, .tier = 2,
     .name = "in deep",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 2}},
    {.id = AchievementId::BURIED, .section = AchievementSection::DIG_OUT, .tier = 3,
     .name = "buried",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 3}},
    {.id = AchievementId::ENTOMBED, .section = AchievementSection::DIG_OUT, .tier = 3,
     .name = "entombed",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 4}},
    {.id = AchievementId::MIRACLE_DIG, .section = AchievementSection::DIG_OUT, .tier = 4,
     .name = "miracle dig",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 5}},

    // Rising Floor - Type C
    {.id = AchievementId::HOLD_GROUND, .section = AchievementSection::RISING_FLOOR, .tier = 2,
     .name = "hold ground",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_C, .threshold = 30000}},
    {.id = AchievementId::HIGH_WATER, .section = AchievementSection::RISING_FLOOR, .tier = 3,
     .name = "high water",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_C, .threshold = 75000}},
    // The fastest rise is the highest index (kTypeCRiseValues is easiest-first, so index 5 is the
    // value-6 rise), so BEDROCK asks for a rise index at or above 5.
    {.id = AchievementId::BEDROCK, .section = AchievementSection::RISING_FLOOR, .tier = 4,
     .name = "bedrock",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_C, .threshold = 30000,
                   .minVariant = 5, .variantMatters = true}},

    // Redline - level reached
    {.id = AchievementId::NINTH_GEAR, .section = AchievementSection::REDLINE, .tier = 3,
     .name = "ninth gear", .condition = {.kind = ConditionKind::LevelReached, .threshold = 15}},
    {.id = AchievementId::REDLINE, .section = AchievementSection::REDLINE, .tier = 4,
     .name = "redline", .condition = {.kind = ConditionKind::LevelReached, .threshold = 19}},
    {.id = AchievementId::TERMINAL_VELOCITY, .section = AchievementSection::REDLINE, .tier = 4,
     .name = "terminal velocity",
     .condition = {.kind = ConditionKind::LevelReached, .threshold = 20}},

    // Have A Heart - heart mode
    {.id = AchievementId::HAVE_A_HEART, .section = AchievementSection::HAVE_A_HEART, .tier = 1,
     .hidden = false, .name = "have a heart",
     .condition = {.kind = ConditionKind::HeartRoundFinished}},
    {.id = AchievementId::HEART_OF_STEEL, .section = AchievementSection::HAVE_A_HEART, .tier = 4,
     .name = "heart of steel",
     .condition = {.kind = ConditionKind::ScoreAtLeast, .type = GameType::TYPE_A, .threshold = 100000,
                   .requireHeart = true}},
    {.id = AchievementId::TRUE_BELIEVER, .section = AchievementSection::HAVE_A_HEART, .tier = 4,
     .name = "true believer",
     .condition = {.kind = ConditionKind::TypeBWin, .minLevel = 9, .minVariant = 5,
                   .requireHeart = true}},

    // Liftoff - the bonus scenes
    {.id = AchievementId::LIFTOFF, .section = AchievementSection::LIFTOFF, .tier = 3,
     .name = "liftoff",
     .condition = {.kind = ConditionKind::SceneReached, .scene = Scene::ROCKET}},
    {.id = AchievementId::ESCAPE_VELOCITY, .section = AchievementSection::LIFTOFF, .tier = 4,
     .name = "escape velocity",
     .condition = {.kind = ConditionKind::SceneReached, .scene = Scene::TOP_ROCKET}},
    {.id = AchievementId::COSMONAUT, .section = AchievementSection::LIFTOFF, .tier = 4,
     .name = "cosmonaut",
     .condition = {.kind = ConditionKind::SceneReached, .scene = Scene::BURAN}},

    // The Long Game - dedication
    {.id = AchievementId::REGULAR, .section = AchievementSection::THE_LONG_GAME, .tier = 1,
     .name = "regular", .condition = {.kind = ConditionKind::LifetimeRounds, .threshold = 50}},
    {.id = AchievementId::DEVOTEE, .section = AchievementSection::THE_LONG_GAME, .tier = 3,
     .name = "devotee", .condition = {.kind = ConditionKind::LifetimeRounds, .threshold = 500}},
    {.id = AchievementId::WELL_ROUNDED, .section = AchievementSection::THE_LONG_GAME, .tier = 2,
     .name = "well rounded", .condition = {.kind = ConditionKind::AllGameTypesPlayed}},
    {.id = AchievementId::ALL_EARS, .section = AchievementSection::THE_LONG_GAME, .tier = 2,
     .name = "all ears", .condition = {.kind = ConditionKind::PlayedAllMusic}},
    {.id = AchievementId::PIECE_BY_PIECE, .section = AchievementSection::THE_LONG_GAME, .tier = 3,
     .name = "piece by piece",
     .condition = {.kind = ConditionKind::LifetimeDrops, .threshold = 100000}},
    {.id = AchievementId::TIME_SINK, .section = AchievementSection::THE_LONG_GAME, .tier = 4,
     .name = "time sink",
     .condition = {.kind = ConditionKind::ApplicationSecondsAtLeast, .threshold = 86400}},
}};

// Every row sits at the position its id names, so the persisted array and the set index each other
// exactly.
static_assert([] {
    for (std::size_t i = 0; i < kSet.size(); ++i) {
        if (kSet[i].id != static_cast<AchievementId>(i)) return false;
    }
    return true;
}(), "kSet rows must be in AchievementId order, one row per id");

// The finished round, as the conditions read it. Assembled once when the check runs.
struct RoundView {
    RoundCombination combination{};  // type, start level, start height / rise index, heart
    std::uint32_t    score    = 0;   // engine.score, the round's final score
    std::uint32_t    lines    = 0;   // flow.lines (Type A counts up to this)
    std::uint8_t     level    = 0;   // flow.level, the level the round climbed to
    bool             won      = false;  // a Type B win, not a top-out
    bool             rocket   = false;
    bool             topRocket = false;
    bool             buran    = false;
    std::uint32_t    tetrises = 0;  // four-line clears this round
};

// The lifetime totals a condition folds, computed once. Achievements are per-player, so every fold is
// over both the cartridge and the heart tables (StatScope::ALL).
struct LifetimeView {
    StatSlice     all{};
    std::uint32_t roundsTypeA = 0;
    std::uint32_t roundsTypeB = 0;
    std::uint32_t roundsTypeC = 0;
    bool          allMusic    = false;
};

RoundView makeRoundView(const GameContext& game) {
    const AchievementRound& round = game.achievements.round;
    return {.combination = combinationOf(game.flow),
            .score       = game.engine.score,
            .lines       = game.flow.lines,
            .level       = game.flow.level,
            .won         = round.wonTypeB,
            .rocket      = round.rocketReached,
            .topRocket   = round.topRocketReached,
            .buran       = round.buranReached,
            .tetrises    = round.tetrises};
}

LifetimeView makeLifetimeView(const StatsState& stats) {
    LifetimeView view;
    view.all         = lifetimeTotals(stats, StatScope::ALL);
    view.roundsTypeA = roundsFor(stats, GameType::TYPE_A, StatScope::ALL);
    view.roundsTypeB = roundsFor(stats, GameType::TYPE_B, StatScope::ALL);
    view.roundsTypeC = roundsFor(stats, GameType::TYPE_C, StatScope::ALL);

    view.allMusic = true;
    for (const std::uint32_t rounds : stats.musicRounds) {
        if (rounds == 0) view.allMusic = false;
    }
    return view;
}

bool conditionMet(const Condition& cond, const RoundView& rv, const LifetimeView& life,
                  const StatsState& stats) {
    switch (cond.kind) {
        case ConditionKind::ScoreAtLeast:
            return rv.combination.type == cond.type && rv.score >= cond.threshold &&
                   (!cond.requireHeart || rv.combination.heart) &&
                   (!cond.variantMatters || rv.combination.variant >= cond.minVariant);
        case ConditionKind::LinesInRoundAtLeast:
            return rv.combination.type == cond.type && rv.lines >= cond.threshold;
        case ConditionKind::LifetimeLines:
            return life.all.lines >= cond.threshold;
        case ConditionKind::TetrisesInRound:
            return rv.tetrises >= cond.threshold;
        case ConditionKind::LifetimeTetrises:
            return life.all.tetrises >= cond.threshold;
        case ConditionKind::TypeBWin:
            return rv.won && rv.combination.type == GameType::TYPE_B &&
                   rv.combination.level >= cond.minLevel &&
                   rv.combination.variant >= cond.minVariant &&
                   (!cond.requireHeart || rv.combination.heart);
        case ConditionKind::LevelReached:
            return rv.level >= cond.threshold;
        case ConditionKind::HeartRoundFinished:
            return rv.combination.heart;
        case ConditionKind::SceneReached:
            switch (cond.scene) {
                case Scene::ROCKET:     return rv.rocket;
                case Scene::TOP_ROCKET: return rv.topRocket;
                case Scene::BURAN:      return rv.buran;
            }
            return false;
        case ConditionKind::LifetimeRounds:
            return life.all.rounds >= cond.threshold;
        case ConditionKind::AllGameTypesPlayed:
            return life.roundsTypeA > 0 && life.roundsTypeB > 0 && life.roundsTypeC > 0;
        case ConditionKind::PlayedAllMusic:
            return life.allMusic;
        case ConditionKind::LifetimeDrops:
            return life.all.drops >= cond.threshold;
        case ConditionKind::ApplicationSecondsAtLeast:
            return stats.applicationSeconds >= cond.threshold;
    }
    return false;
}

}  // namespace

std::span<const AchievementDef> achievementSet() { return kSet; }

void evaluateRoundEnd(GameContext& game, const NowDate& now) {
    AchievementState& state = game.achievements;
    if (!state.round.pendingEval) return;  // no concluded round awaiting the check

    const RoundView     rv          = makeRoundView(game);
    const LifetimeView  life        = makeLifetimeView(game.stats);
    const std::uint32_t datePacked  = packAchievementDate(now ? now() : AchievementDate{});
    const std::uint32_t playSeconds = game.stats.applicationSeconds;

    for (const AchievementDef& def : kSet) {
        AchievementUnlock& record = state.unlocked[achievementIndex(def.id)];
        if (record.unlocked) continue;  // first unlock wins; never overwrite an existing stamp
        if (conditionMet(def.condition, rv, life, game.stats)) {
            record = {.unlocked            = true,
                      .datePacked          = datePacked,
                      .playSecondsAtUnlock = playSeconds};
        }
    }

    // Consumed. A second exit's call finds nothing pending, and the next real round arms afresh.
    state.round = AchievementRound{};
}

std::size_t achievementsUnlocked(const GameContext& game) {
    std::size_t count = 0;
    for (const AchievementUnlock& record : game.achievements.unlocked) {
        if (record.unlocked) ++count;
    }
    return count;
}

}  // namespace kirpich::systems
