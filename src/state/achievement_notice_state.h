#pragma once

// What the end-of-round notice has left to show, and where the round was going when it was
// interrupted.
//
// A round that earns something is not sent straight back to its difficulty screen: the achievements
// it just earned are shown first, one at a time. The queue below is what the round-end check awarded,
// in the set's own order; `shown` is which of them is on screen; `resume` is the destination the exit
// was carrying, held until the last one has been seen.
//
// The queue is the single record of what was just earned - what is on screen is `pending[shown]`, not
// a second copy of it - and it is written in exactly one place, the round-end check
// (systems/achievements.h). What was earned at all, as opposed to what is still to be shown, lives in
// state/achievement_state.h and persists; this does not. It means nothing after the program stops,
// and a soft reset clears it, so a reset taken mid-round cannot leave badges to be shown after the
// next one.

#include <cstdint>

#include <kirpich/game_state.h>

#include "data/bounded_vec.h"
#include "state/achievement_state.h"  // AchievementId, kAchievementCount

namespace kirpich {

struct AchievementNoticeState {
    // What the round just earned, in the order the set defines. Bounded by the size of the set,
    // because a single extraordinary round on a fresh save could in principle earn all of it.
    BoundedVec<AchievementId, kAchievementCount> pending{};

    // Which of `pending` is on screen. Meaningful only while the queue is non-empty.
    std::uint8_t shown = 0;

    // Where the finished round was headed before the notice took the frame - the difficulty screen for
    // the mode just played. Written when the notice is entered and read when the last badge is
    // dismissed; the default is only the value it holds before a round has ever ended.
    GameState resume = GameState::INIT_TYPE_A_DIFFICULTY;

    void reset() { *this = AchievementNoticeState{}; }

    friend constexpr bool operator==(const AchievementNoticeState&,
                                     const AchievementNoticeState&) = default;
};

}  // namespace kirpich
