#include "systems/achievement_notice.h"

#include <array>
#include <span>

#include <kirpich/action.h>

#include "data/sfx.h"       // SquareSfxId
#include "retropp/input.h"  // actionId
#include "systems/game_state_dispatcher.h"

namespace kirpich::systems {

namespace {

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Every banner arrives on the sound a level-up arrives on. It is the game's own reward cue, and an
// achievement is the same kind of event: something gained rather than something that merely happened.
void badgeCue(GameContext& game) { game.audioCues.square = SquareSfxId::LEVEL_UP; }

// Step past the badge on screen. Past the last one the queue is empty, and the round is released to
// the destination it was headed for when the notice took the frame - no cue there, because no banner
// arrives.
void advance(GameContext& game) {
    AchievementNoticeState& notice = game.achievementNotice;

    if (static_cast<std::size_t>(notice.shown) + 1 < notice.pending.size()) {
        ++notice.shown;
        badgeCue(game);
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
    badgeCue(game);  // the first banner arrives on the same cue every later one does
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
