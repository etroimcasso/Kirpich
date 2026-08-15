#include "systems/game_state_dispatcher.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <spdlog/spdlog.h>

#include <kirpich/action.h>

namespace kirpich::systems {

namespace {

// The dispatch slot for a game state — its numeric value, which is its table index (the 54 states
// are contiguous $00-$35).
std::size_t slotOf(GameState state) {
    return static_cast<std::size_t>(static_cast<std::uint8_t>(state));
}

}  // namespace

GameStateDispatcher::GameStateDispatcher() {
    // Every state starts as a stub that logs its numeric value and leaves the context untouched, so
    // an unported state sits in place rather than crashing or transitioning — the behavior a later
    // unit replaces slot by slot, and exactly what one already-bare handler in the original does.
    for (std::size_t i = 0; i < kGameStateCount; ++i) {
        handlers_[i] = [i](GameContext&) {
            spdlog::debug("dispatcher: game state ${:02X} has no handler yet", i);
        };
    }

    // A silent no-op until the sound driver lands (an audio tick every frame would flood the log).
    audioTick = [] {};
    // A fired chord with no reset installed is surprising enough to surface once.
    softReset = [] { spdlog::warn("soft-reset chord fired with no reset handler installed"); };
}

void GameStateDispatcher::setHandler(GameState state, Handler handler) {
    const std::size_t slot = slotOf(state);
    assert(slot < kGameStateCount && "game state outside the dispatch table");
    handlers_[slot] = std::move(handler);
}

void GameStateDispatcher::tick(GameContext& game, retropp::ActionSet held) {
    // 1. Sample the joypad (ReadJoypad). The snapshot the handler and the chord below both read.
    game.joypad = input_.sample(held);

    // 2. Dispatch. The index is read once, before the handler runs — a handler that writes a new
    //    gameState transitions on the *next* tick, matching the original's read-then-jump.
    const std::size_t slot = slotOf(game.flow.gameState);
    assert(slot < kGameStateCount && "game state outside the dispatch table");
    handlers_[slot](game);

    // 3. Advance the audio driver (UpdateAudio).
    audioTick();

    // 4. Soft-reset chord: Start + Select + A + B (rotate CW / CCW) all held this tick. Held levels,
    //    not edges — a sustained chord fires every tick it is held. Extra held actions do not block
    //    (only these four are tested). On a fire the frame ends here: dispatch and audio already ran,
    //    but the timer decrements below do not.
    const retropp::ActionSet& h = game.joypad.held;
    const bool chord = h.test(retropp::actionId(Action::Start)) &&
                       h.test(retropp::actionId(Action::Select)) &&
                       h.test(retropp::actionId(Action::RotateClockwise)) &&
                       h.test(retropp::actionId(Action::RotateCounterClockwise));
    if (chord) {
        softReset();
        return;
    }

    // 5. Decrement the two frame timers, each saturating at zero (no wrap).
    if (game.flow.timer1 != 0) {
        --game.flow.timer1;
    }
    if (game.flow.timer2 != 0) {
        --game.flow.timer2;
    }
}

void GameStateDispatcher::reset() { input_.reset(); }

}  // namespace kirpich::systems
