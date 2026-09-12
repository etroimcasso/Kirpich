#pragma once

// The end-of-round notice's logic: whether a finished round has anything to announce, and what a press
// does while it is announcing.
//
// A round that earned something does not go straight back to its difficulty screen. The exit below
// takes the destination the round was headed for, and - if the round-end check queued anything - holds
// it and sends the player to the notice instead; the notice shows one badge at a time and hands the
// destination back once the last has been seen. Both places a round can end route through it, so the
// rule is written once and neither handler knows what a notice is.
//
// It draws nothing and names no drawing type: the picture is a function of the queue, built by the
// components under src/render/achievements/. Nothing is cleared on the way in or out, either - the
// notice's frame is its own layers, so whatever the previous screen left in the object buffer and the
// background map is simply not drawn, and the difficulty screen it hands to paints itself.

#include <kirpich/game_state.h>

#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// Where a finished round actually goes. Returns `destination` when the round earned nothing; otherwise
// holds `destination` for later and returns the notice's state. Written to be used as the RoundExit
// seam, which is how both round exits reach it.
[[nodiscard]] GameState achievementNoticeExit(GameContext& game, GameState destination);

// Which badge the notice is showing. Meaningful only while there is something queued.
[[nodiscard]] AchievementId achievementOnNotice(const GameContext& game) noexcept;

// ── State handler ─────────────────────────────────────────────────────────────────────────────────

// One frame: a press steps to the next earned badge, and a press past the last one empties the queue
// and releases the round to the destination it was headed for. Mutates only state.
void achievementNotice(GameContext& game);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

void installAchievementNotice(GameStateDispatcher& dispatcher);

}  // namespace kirpich::systems
