#pragma once

// What the achievements screen is currently showing: which section page, where the cursor sits on it,
// and which badge (if any) has its description open.
//
// This is the screen's own state, not the game's - what a player is looking at has no bearing on what
// the game IS, so none of it belongs on GameFlowState. It is a GameContext member of its own rather
// than another set of fields on ScreenUiState, because the achievements screen owns its whole state
// space in one place.
//
// Nothing here persists. A cold boot and the reset chord both return it to the values below; what the
// player has earned lives in state/achievement_state.h and does persist.

#include <cstdint>

#include "state/achievement_state.h"  // AchievementId

namespace kirpich {

struct AchievementScreenState {
    // Which section is on the page, as an index into the sections in their enum order
    // (AchievementSection, systems/achievements.h). One section to a page.
    std::uint8_t section = 0;

    // Which badge of that section the cursor is on, indexed into the section's own badges in id
    // order. A section turn keeps the column and lands in the row it stepped into - the first row of
    // the section below, the last row of the section above - clamped when that row is shorter.
    std::uint8_t cursor = 0;

    // Whether a badge's description is open, and which one. `openId` is meaningful only while `open`
    // is set; nothing reads it otherwise, which is why the closed state needs no sentinel id.
    bool          open = false;
    AchievementId openId{};

    void reset() { *this = AchievementScreenState{}; }

    friend constexpr bool operator==(const AchievementScreenState&,
                                     const AchievementScreenState&) = default;
};

}  // namespace kirpich
