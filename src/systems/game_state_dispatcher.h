#pragma once

// The game-state dispatcher: the per-state handler table, the frame shape, and the two seams to
// systems that are not ported yet. The engine's run loop calls tick() once per sim tick; each call is
// one frame of the game. The heart of the frame is the dispatch — read the current game state, run
// its handler — wrapped by the fixed beats the original runs around it: sample the joypad before,
// tick the audio driver, check the four-button soft-reset chord, and decrement the two frame timers
// after. Every gameplay, menu, demo, and scene behavior is a handler that hangs off the table; this
// unit fixes the handler signature, the tick ordering, and the chord semantics they are all written
// against, and ships with every slot filled by a not-ported stub. It ports the original's MainLoop
// routine (tetris.asm:386-421); the port carries no loop of its own — pacing and the loop back are
// the engine run loop's. See docs/contracts/dispatcher.md.

#include <array>
#include <cstddef>
#include <functional>

#include <kirpich/game_state.h>

#include "retropp/input.h"  // ActionSet
#include "systems/game_context.h"
#include "systems/input.h"  // InputSystem

namespace kirpich::systems {

// The dispatch table covers every game state, indexed by the state byte itself: the 54 labelled
// cartridge states ($00-$35, contiguous) and the port's own screens from $40 up. The slots between
// them name nothing and keep the not-ported stub, as does $36 — the original's pointer table has a
// 55th entry there holding a raw address rather than a handler, a dispatch over-read and not a state
// (see include/kirpich/game_state.h).
inline constexpr std::size_t kGameStateCount = 0x49;

// The frame dispatcher. Holds one handler per game state, an owned input mechanism (the previous-held
// byte the edge relation needs is mechanism state, not game state, so it lives here rather than on
// GameContext), and two seams for neighbors that land later.
class GameStateDispatcher {
public:
    // A state's per-frame handler: it runs one frame of that state and may write a new value into
    // GameContext::flow.gameState to transition on the next tick.
    using Handler = std::function<void(GameContext&)>;

    // Fills every slot with the not-ported stub and installs the default seams (audioTick a silent
    // no-op; softReset a warning that does nothing).
    GameStateDispatcher();

    // Install a state's handler. Later systems register theirs as they land; tests install probes.
    void setHandler(GameState state, Handler handler);

    // One frame: sample -> dispatch -> audio -> soft-reset chord -> timers. `held` is the caller's
    // held-action set for this tick — live play passes heldActions(inputState); demo playback passes
    // the recorded timeline's held set. The two frame beats the original runs here that have no
    // observable native counterpart (the multiplayer serial-interrupt rewrite and the VBlank
    // busy-wait) are absent; they belong to their owning systems and to the engine run loop.
    void tick(GameContext& game, retropp::ActionSet held);

    // Return the input mechanism to boot (previous-held empty). The handler table and the seams are
    // wiring, not per-run state, so they persist across a reset.
    void reset();

    // Seams for not-yet-ported neighbors. audioTick advances the sound driver (the audio unit
    // installs the real tick); softReset performs the whole-machine reset the chord requests (the
    // boot-path unit installs the real reset). Both carry safe defaults until then.
    std::function<void()> audioTick;
    std::function<void()> softReset;

private:
    std::array<Handler, kGameStateCount> handlers_;
    InputSystem input_;
};

}  // namespace kirpich::systems
