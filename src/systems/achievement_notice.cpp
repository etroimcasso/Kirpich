#include "systems/achievement_notice.h"

#include <array>
#include <span>

#include <kirpich/action.h>

#include "retropp/input.h"  // actionId
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Step past the badge on screen. Past the last one the queue is empty, and the round is released to
// the destination it was headed for when the notice took the frame.
void advance(GameContext& game) {
    AchievementNoticeState& notice = game.achievementNotice;

    if (static_cast<std::size_t>(notice.shown) + 1 < notice.pending.size()) {
        ++notice.shown;
        return;
    }

    const GameState destination = notice.resume;
    notice.reset();
    game.flow.gameState = destination;
}

// What a button does, as data. The notice has one mode and one thing to do in it, and it accepts the
// two presses that dismiss the game-over screen (gameplay.cpp) - Confirm shares its source with the
// rotation the screen before it read - so a player carrying on pressing A steps through what they
// earned without learning a second button.
using Effect = void (*)(GameContext&);

struct Bind {
    Action action;
    Effect effect;
};

constexpr std::array kNoticeBinds{
    Bind{Action::Confirm, advance},
    Bind{Action::Start, advance},
};

void dispatch(GameContext& game, std::span<const Bind> binds) {
    for (const Bind& bind : binds) {
        if (pressed(game, bind.action)) {
            bind.effect(game);
            return;
        }
    }
}

}  // namespace

GameState achievementNoticeExit(GameContext& game, GameState destination) {
    if (game.achievementNotice.pending.empty()) {
        return destination;
    }

    game.achievementNotice.shown  = 0;
    game.achievementNotice.resume = destination;
    return GameState::ACHIEVEMENT_NOTICE;
}

AchievementId achievementOnNotice(const GameContext& game) noexcept {
    const AchievementNoticeState& notice = game.achievementNotice;
    return notice.pending.empty() ? AchievementId{} : notice.pending[notice.shown];
}

void achievementNotice(GameContext& game) { dispatch(game, kNoticeBinds); }

void installAchievementNotice(GameStateDispatcher& dispatcher) {
    dispatcher.setHandler(GameState::ACHIEVEMENT_NOTICE,
                          [](GameContext& g) { achievementNotice(g); });
}

}  // namespace kirpich::systems
