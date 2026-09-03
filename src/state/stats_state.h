#pragma once

// What the player has done, kept across launches: one tally per difficulty combination, and the
// program's own running time.
//
// A round belongs to the combination it was started in — a Type A level, a Type B level and start
// height, a Type C level and rise — which is the same slice its top score would land in. Nothing
// above that level is stored: a game type's totals are the sum of its slices, the whole game's are
// the sum of all of them, and the longest round anywhere is the largest of them, whose identity is
// the slice it was found in. Storing those rollups as well would be three levels that can disagree.
//
// Recording does not depend on any setting. The stats screen can be switched off, and then the
// tables simply go unread — a player who turns it on later finds their whole history there.
//
// The tables persist in their own save document, beside the settings and the top scores
// (src/state/stats_persistence.h). The round-in-progress block below does not: it is the current
// round's bookkeeping and means nothing after the program stops, the same way HighScoreState's
// name-entry bytes are left out of its own document.

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/piece_kind.h>

namespace kirpich {

// What one difficulty combination accumulates. Every field is a plain running count except
// longestRoundSeconds, which is the largest single round played there — the one field that folds by
// taking the larger of two rather than by adding them.
struct StatSlice {
    std::uint32_t rounds              = 0;
    std::uint32_t seconds             = 0;  // time actually played here, pauses excluded
    std::uint32_t longestRoundSeconds = 0;
    std::uint32_t drops               = 0;
    std::uint32_t score               = 0;
    std::uint32_t lines               = 0;
    std::uint32_t singles             = 0;
    std::uint32_t doubles             = 0;
    std::uint32_t triples             = 0;
    std::uint32_t tetrises            = 0;

    // How many of each shape have come to rest here, indexed by PieceKind. Counted at the same moment
    // as `drops`, so the seven of them sum to exactly that - two figures a player can see on the same
    // screen, agreeing by construction rather than by arithmetic that could drift.
    //
    // Thirty-two bits each, like every other count in this slice, and for that reason: a narrower
    // field would stop climbing while `drops` kept going, and the two would visibly disagree on the
    // screens that show both. Seven more counts cost 28 bytes a slice.
    std::array<std::uint32_t, kPieceKindCount> pieces{};

    friend constexpr bool operator==(const StatSlice&, const StatSlice&) = default;
};

// How many starting levels every game type offers, and how many values the second axis carries -
// Type B's six start heights and Type C's six rises, which are the same axis in both tables. Type A
// is picked by level alone and so has no second axis at all.
inline constexpr std::size_t kStatLevels   = 10;
inline constexpr std::size_t kStatVariants = 6;

// The round being played right now, so that its counts can be attributed to the slice it started in
// rather than to whatever the flow state says when it ends - a Type A round levels up as it runs,
// and it still belongs to the level it was picked at.
//
// The two nanosecond fields are how a round is timed without a timer: `stampNanos` is the clock
// reading when play last resumed, `bankedNanos` is everything played before that. A pause banks the
// stretch just played and an unpause re-stamps, so paused time is simply never added. Nanoseconds
// rather than seconds because the sum is converted once, at the end: banking whole seconds would
// throw away a fraction at every pause.
struct RoundInProgress {
    bool          active      = false;
    GameType      type{};
    std::uint8_t  level       = 0;
    std::uint8_t  variant     = 0;      // start height or rise index; meaningless when hasVariant is false
    bool          hasVariant  = false;
    std::uint64_t stampNanos  = 0;
    std::uint64_t bankedNanos = 0;

    friend constexpr bool operator==(const RoundInProgress&, const RoundInProgress&) = default;
};

struct StatsState {
    // Indexed [level], and [level][height] / [level][rise] for the two that have a second axis - the
    // shape the top-score tables use, for the same reason.
    std::array<StatSlice, kStatLevels>                              typeA{};
    std::array<std::array<StatSlice, kStatVariants>, kStatLevels>    typeB{};
    std::array<std::array<StatSlice, kStatVariants>, kStatLevels>    typeC{};

    // How long the program itself has run, across every launch. The one figure that is not a fold
    // over the tables: it counts the title screen and the menus, which belong to no round.
    std::uint32_t applicationSeconds = 0;

    // How many rounds have been played under each music selection, indexed by musicTypeIndex.
    //
    // The second figure that cannot be a fold, and for a different reason: music is not part of a
    // combination. A round belongs to a game type, a level and a variant, and the song playing over
    // it is none of those - so there is nowhere in the slice table for this to live, and it is
    // counted alongside them instead.
    std::array<std::uint32_t, kMusicTypeCount> musicRounds{};

    // The current round, and the current session's own timing. Neither is written to disk.
    RoundInProgress round{};
    std::uint64_t   applicationStampNanos  = 0;
    std::uint64_t   applicationBankedNanos = 0;

    // Return every field to its boot (all-zero) value. The startup load fills the tables back in
    // from the save document; the reset chord puts them back by hand, because they outlive it.
    void reset() { *this = StatsState{}; }

    friend bool operator==(const StatsState&, const StatsState&) = default;
};

}  // namespace kirpich
