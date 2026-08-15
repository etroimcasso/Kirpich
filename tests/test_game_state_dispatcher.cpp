// Game-state dispatcher — behavioral tests against docs/contracts/dispatcher.md.
//
// Device-free: the dispatcher is pure logic over a handler table and the game-state aggregate. Every
// asserted ordering and value is traced to the tetris.asm lines named in the contract (the MainLoop
// frame shape at :386-417 and the jump-table dispatch at :419-421). Handlers are test probes; the
// engine run loop that will drive tick() at startup is not exercised here.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/game_state.h>

#include "retropp/input.h"
#include "retropp/input_actions.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"

namespace {

using kirpich::Action;
using kirpich::GameState;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;
using kirpich::systems::kGameStateCount;

// Build an ActionSet from a list of game actions.
retropp::ActionSet actions(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// The four buttons of the soft-reset chord: Start + Select + A (rotate CW) + B (rotate CCW).
retropp::ActionSet chord() {
    return actions({Action::Start, Action::Select, Action::RotateClockwise,
                    Action::RotateCounterClockwise});
}

}  // namespace

// DispatchSweep — the jump-table dispatch (tetris.asm:419-421). Install a recording probe in every
// one of the 54 slots; for each state, ticking must fire exactly that state's probe exactly once.
// Plus the stub path: a fresh dispatcher (every slot the not-ported stub) leaves the context
// unchanged except for the joypad snapshot and the timer law.
TEST(GameStateDispatcher, DispatchSweep) {
    GameStateDispatcher dispatcher;
    std::array<int, kGameStateCount> fired{};
    for (std::size_t i = 0; i < kGameStateCount; ++i) {
        dispatcher.setHandler(static_cast<GameState>(i), [i, &fired](GameContext&) { ++fired[i]; });
    }

    for (std::size_t i = 0; i < kGameStateCount; ++i) {
        fired.fill(0);
        GameContext game;
        game.flow.gameState = static_cast<GameState>(i);
        dispatcher.tick(game, retropp::ActionSet{});
        for (std::size_t j = 0; j < kGameStateCount; ++j) {
            EXPECT_EQ(fired[j], j == i ? 1 : 0)
                << "dispatching state " << i << " fired probe " << j;
        }
    }

    // Stub path: nothing but the joypad snapshot and the (zero-saturating) timer law changes.
    GameStateDispatcher stubs;
    GameContext expected;
    expected.flow.gameState = GameState::TITLE_SCREEN;
    GameContext game = expected;
    stubs.tick(game, retropp::ActionSet{});
    expected.joypad = game.joypad;  // absorb the one expected delta; everything else must match
    EXPECT_EQ(game, expected);
}

// TransitionSemantics — the dispatch index is read once, before the handler runs (tetris.asm:420
// reads the value before the jump). A handler that writes a new state transitions on the NEXT tick.
TEST(GameStateDispatcher, TransitionSemantics) {
    GameStateDispatcher dispatcher;
    int aRuns = 0;
    int bRuns = 0;
    const GameState kA = GameState::TITLE_SCREEN;
    const GameState kB = GameState::NORMAL_GAMEPLAY;
    dispatcher.setHandler(kA, [&](GameContext& g) {
        ++aRuns;
        g.flow.gameState = kB;
    });
    dispatcher.setHandler(kB, [&](GameContext&) { ++bRuns; });

    GameContext game;
    game.flow.gameState = kA;
    dispatcher.tick(game, retropp::ActionSet{});  // A runs and writes B; B must not run this tick
    EXPECT_EQ(aRuns, 1);
    EXPECT_EQ(bRuns, 0);
    EXPECT_EQ(game.flow.gameState, kB);

    dispatcher.tick(game, retropp::ActionSet{});  // the next tick dispatches B
    EXPECT_EQ(aRuns, 1);
    EXPECT_EQ(bRuns, 1);
}

// BeatOrdering — the five beats run in ROM order (tetris.asm:387-394): sample -> dispatch -> audio
// -> chord. A chord-held tick appends handler, audio, softReset in that order; and the handler
// observes THIS tick's joypad (the sample ran first), so a fresh press reads as pressed inside it.
TEST(GameStateDispatcher, BeatOrdering) {
    GameStateDispatcher dispatcher;
    std::vector<std::string> seq;
    bool handlerSawFreshPress = false;

    dispatcher.setHandler(GameState::NORMAL_GAMEPLAY, [&](GameContext& g) {
        seq.push_back("handler");
        handlerSawFreshPress = g.joypad.pressed.test(retropp::actionId(Action::Start));
    });
    dispatcher.audioTick = [&] { seq.push_back("audio"); };
    dispatcher.softReset = [&] { seq.push_back("softReset"); };

    GameContext game;  // gameState boots NORMAL_GAMEPLAY
    dispatcher.tick(game, chord());

    ASSERT_EQ(seq.size(), 3u);
    EXPECT_EQ(seq[0], "handler");
    EXPECT_EQ(seq[1], "audio");
    EXPECT_EQ(seq[2], "softReset");
    EXPECT_TRUE(handlerSawFreshPress);  // sample preceded dispatch
}

// ChordVectors — the soft-reset chord (tetris.asm:391-394): all four held fires softReset and skips
// the timer decrements while dispatch and audio still run; any three-of-four subset does not fire;
// extra held actions do not block (the mask law); a sustained chord fires every tick it is held.
TEST(GameStateDispatcher, ChordVectors) {
    GameStateDispatcher dispatcher;
    int handlerRuns = 0;
    int audioRuns = 0;
    int resetRuns = 0;
    dispatcher.setHandler(GameState::NORMAL_GAMEPLAY, [&](GameContext&) { ++handlerRuns; });
    dispatcher.audioTick = [&] { ++audioRuns; };
    dispatcher.softReset = [&] { ++resetRuns; };

    // All four held: softReset fires; dispatch + audio ran; the timers did NOT decrement (the chord
    // returns above them at :394).
    {
        GameContext game;
        game.flow.timer1 = 5;
        game.flow.timer2 = 7;
        dispatcher.tick(game, chord());
        EXPECT_EQ(resetRuns, 1);
        EXPECT_EQ(handlerRuns, 1);
        EXPECT_EQ(audioRuns, 1);
        EXPECT_EQ(game.flow.timer1, 5);
        EXPECT_EQ(game.flow.timer2, 7);
    }

    // Each three-of-four subset does not fire.
    const Action four[] = {Action::Start, Action::Select, Action::RotateClockwise,
                           Action::RotateCounterClockwise};
    for (int drop = 0; drop < 4; ++drop) {
        retropp::ActionSet subset;
        for (int k = 0; k < 4; ++k) {
            if (k != drop) {
                subset.set(retropp::actionId(four[k]), true);
            }
        }
        resetRuns = 0;
        GameContext game;
        dispatcher.tick(game, subset);
        EXPECT_EQ(resetRuns, 0) << "three-of-four subset (dropped index " << drop << ") must not fire";
    }

    // All four plus an extra held action still fires (the ROM masks to the four bits).
    {
        resetRuns = 0;
        retropp::ActionSet extra = chord();
        extra.set(retropp::actionId(Action::SoftDrop), true);
        GameContext game;
        dispatcher.tick(game, extra);
        EXPECT_EQ(resetRuns, 1) << "extra held actions must not block the chord";
    }

    // A sustained chord fires again the next tick — held levels, not edges.
    {
        resetRuns = 0;
        GameContext game;
        dispatcher.tick(game, chord());
        dispatcher.tick(game, chord());
        EXPECT_EQ(resetRuns, 2) << "a sustained chord fires every tick it is held";
    }
}

// TimerLaw — the two frame timers (tetris.asm:395-405): each non-zero timer decrements one per tick,
// zero saturates (no wrap), and a handler-written value decrements the same tick it was written
// (the dispatch at :388 precedes the decrement at :395).
TEST(GameStateDispatcher, TimerLaw) {
    GameStateDispatcher dispatcher;  // all stubs; no chord held, so the timers run

    // Non-zero timers each decrement by one.
    {
        GameContext game;
        game.flow.timer1 = 3;
        game.flow.timer2 = 1;
        dispatcher.tick(game, retropp::ActionSet{});
        EXPECT_EQ(game.flow.timer1, 2);
        EXPECT_EQ(game.flow.timer2, 0);
    }

    // Zero saturates — no wrap to 255.
    {
        GameContext game;  // timers boot at 0
        dispatcher.tick(game, retropp::ActionSet{});
        EXPECT_EQ(game.flow.timer1, 0);
        EXPECT_EQ(game.flow.timer2, 0);
    }

    // A handler storing 2 reads back 1 after the tick — dispatch precedes the decrement.
    {
        GameStateDispatcher withHandler;
        withHandler.setHandler(GameState::NORMAL_GAMEPLAY,
                               [](GameContext& g) { g.flow.timer1 = 2; });
        GameContext game;  // boots NORMAL_GAMEPLAY
        withHandler.tick(game, retropp::ActionSet{});
        EXPECT_EQ(game.flow.timer1, 1);
    }
}

// NewActionRows — the chord depends on Start / Select being bound and sampled: defaultActionMap
// carries the two rows, and heldActions walks both enumerators.
TEST(GameStateDispatcher, NewActionRows) {
    const auto map = kirpich::systems::defaultActionMap();  // rows() spans into map; keep it alive
    const auto rows = map.rows();
    auto hasPad = [&](Action a, retropp::PadButton b) {
        return std::any_of(rows.begin(), rows.end(), [&](const retropp::ActionBinding& r) {
            return r.action == retropp::actionId(a) && r.source.kind == retropp::Source::Kind::Pad &&
                   r.source.pad == b;
        });
    };
    EXPECT_TRUE(hasPad(Action::Start, retropp::PadButton::Start));
    EXPECT_TRUE(hasPad(Action::Select, retropp::PadButton::Select));

    retropp::InputState state;
    retropp::InputSample sample;
    sample.players[0].held = actions({Action::Start, Action::Select});
    std::array<retropp::ActionSet, retropp::kMaxPlayers> pressed{};
    pressed[0] = sample.players[0].held;
    state.sampleTick(sample, pressed);
    EXPECT_EQ(kirpich::systems::heldActions(state), actions({Action::Start, Action::Select}));
}
