// Input layer — behavioral tests against docs/contracts/input.md.
//
// Device-free: the edge relation and the key-repeat core are pure, and the InputState adapter is
// exercised by synthesizing an InputSample and feeding it through the engine's documented test seam
// (InputState::sampleTick) — no window, no device. Every asserted value is traced to the tetris.asm
// lines named in the contract.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>

#include <SDL3/SDL_scancode.h>

#include <kirpich/action.h>

#include "retropp/input.h"
#include "retropp/input_actions.h"
#include "systems/input.h"

namespace {

using kirpich::Action;

// Build an ActionSet from a list of game actions.
retropp::ActionSet actions(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

}  // namespace

// EdgeRelation — pressed = held & ~previouslyHeld (ReadJoypad, tetris.asm:6546-6549), plus the
// once-per-frame drop of a sub-tick tap and reset()'s first-tick semantics.
TEST(Input, EdgeRelation) {
    kirpich::systems::InputSystem in;

    // Fresh press: the edge fires once.
    auto s = in.sample(actions({Action::MoveLeft}));
    EXPECT_EQ(s.held, actions({Action::MoveLeft}));
    EXPECT_EQ(s.pressed, actions({Action::MoveLeft}));

    // Sustained hold: no edge on the second tick.
    s = in.sample(actions({Action::MoveLeft}));
    EXPECT_EQ(s.held, actions({Action::MoveLeft}));
    EXPECT_EQ(s.pressed, retropp::ActionSet{});

    // Release then re-press: the edge fires again.
    s = in.sample(retropp::ActionSet{});
    EXPECT_EQ(s.pressed, retropp::ActionSet{});
    s = in.sample(actions({Action::MoveLeft}));
    EXPECT_EQ(s.pressed, actions({Action::MoveLeft}));

    // Simultaneous multi-action set: each newly-down action edges; an already-held one does not.
    in.reset();
    in.sample(actions({Action::MoveLeft}));  // establish MoveLeft as held
    s = in.sample(actions({Action::MoveLeft, Action::RotateClockwise, Action::SoftDrop}));
    EXPECT_EQ(s.held, actions({Action::MoveLeft, Action::RotateClockwise, Action::SoftDrop}));
    EXPECT_EQ(s.pressed, actions({Action::RotateClockwise, Action::SoftDrop}));

    // Sub-tick tap: an action never present in any held sample produces no edge — the
    // once-per-frame poll drops a press released before the tick sampled it.
    in.reset();
    EXPECT_EQ(in.sample(retropp::ActionSet{}).pressed, retropp::ActionSet{});
    EXPECT_EQ(in.sample(retropp::ActionSet{}).pressed, retropp::ActionSet{});

    // reset() clears the previous-held: the next sample treats every held action as newly pressed.
    in.sample(actions({Action::MoveRight}));  // establish a held level
    in.reset();
    EXPECT_EQ(in.sample(actions({Action::MoveRight})).pressed, actions({Action::MoveRight}));
}

// KeyRepeatTimeline — the full DAS timeline through keyRepeatFire (RotateAndShiftPiece,
// tetris.asm:5965-6028): press fires and arms 23, the 23rd held tick fires and reloads 9, then every
// 9th held tick fires.
TEST(Input, KeyRepeatTimeline) {
    using namespace kirpich::systems;
    std::uint8_t timer = 0;

    // Press fires immediately and arms the initial delay.
    EXPECT_TRUE(keyRepeatFire(timer, /*pressed=*/true, /*held=*/false));
    EXPECT_EQ(timer, kKeyRepeatInitialDelay);  // 23

    // 22 held ticks are silent (timer 22..1).
    for (int i = 0; i < 22; ++i) {
        EXPECT_FALSE(keyRepeatFire(timer, false, true)) << "held tick " << i;
    }
    EXPECT_EQ(timer, 1);

    // The 23rd held tick fires and reloads the repeat rate.
    EXPECT_TRUE(keyRepeatFire(timer, false, true));
    EXPECT_EQ(timer, kKeyRepeatRate);  // 9

    // Thereafter it fires every 9th held tick (pin several periods).
    for (int period = 0; period < 3; ++period) {
        for (int i = 0; i < 8; ++i) {
            EXPECT_FALSE(keyRepeatFire(timer, false, true)) << "period " << period << " tick " << i;
        }
        EXPECT_TRUE(keyRepeatFire(timer, false, true)) << "period " << period << " fire";
        EXPECT_EQ(timer, kKeyRepeatRate);
    }
}

// KeyRepeatEdges — the not-held path leaves the timer untouched (name entry, tetris.asm:4003), the
// wall-charge blocked retry (tetris.asm:6001), and the stale-zero-timer wraparound to 255.
TEST(Input, KeyRepeatEdges) {
    using namespace kirpich::systems;

    // Not held and not pressed: no fire, timer untouched.
    std::uint8_t timer = 5;
    EXPECT_FALSE(keyRepeatFire(timer, false, false));
    EXPECT_EQ(timer, 5);

    // Wall-charge retry: the caller parks the timer at 1; the next held tick fires and reloads 9.
    timer = kKeyRepeatBlockedRetry;  // 1
    EXPECT_TRUE(keyRepeatFire(timer, false, true));
    EXPECT_EQ(timer, kKeyRepeatRate);  // 9

    // Hold with a stale zero timer wraps to 255 — a 255-frame delay, not an immediate fire.
    timer = 0;
    EXPECT_FALSE(keyRepeatFire(timer, false, true));
    EXPECT_EQ(timer, 255);
}

// ConstantsPins — the three key-repeat scalars against their cited values.
TEST(Input, ConstantsPins) {
    using namespace kirpich::systems;
    EXPECT_EQ(kKeyRepeatInitialDelay, 23);  // :5974 / :6008 / :3989 ($17)
    EXPECT_EQ(kKeyRepeatRate, 9);           // :5982 / :6016 / :4024 / :4056
    EXPECT_EQ(kKeyRepeatBlockedRetry, 1);   // :6001
}

// DefaultMapRows — the default bindings: every (action, source) pair present, and no rows for any
// action beyond the seven bound actions (the five piece actions plus Start / Select).
TEST(Input, DefaultMapRows) {
    const auto map = kirpich::systems::defaultActionMap();
    const auto rows = map.rows();
    ASSERT_EQ(rows.size(), 14u);  // seven actions, keyboard + gamepad each

    auto hasKey = [&](Action a, SDL_Scancode k) {
        return std::any_of(rows.begin(), rows.end(), [&](const retropp::ActionBinding& r) {
            return r.action == retropp::actionId(a) &&
                   r.source.kind == retropp::Source::Kind::Key && r.source.key == k;
        });
    };
    auto hasPad = [&](Action a, retropp::PadButton b) {
        return std::any_of(rows.begin(), rows.end(), [&](const retropp::ActionBinding& r) {
            return r.action == retropp::actionId(a) &&
                   r.source.kind == retropp::Source::Kind::Pad && r.source.pad == b;
        });
    };

    EXPECT_TRUE(hasKey(Action::MoveLeft, SDL_SCANCODE_LEFT));
    EXPECT_TRUE(hasPad(Action::MoveLeft, retropp::PadButton::DpadLeft));
    EXPECT_TRUE(hasKey(Action::MoveRight, SDL_SCANCODE_RIGHT));
    EXPECT_TRUE(hasPad(Action::MoveRight, retropp::PadButton::DpadRight));
    EXPECT_TRUE(hasKey(Action::SoftDrop, SDL_SCANCODE_DOWN));
    EXPECT_TRUE(hasPad(Action::SoftDrop, retropp::PadButton::DpadDown));
    EXPECT_TRUE(hasKey(Action::RotateClockwise, SDL_SCANCODE_X));
    EXPECT_TRUE(hasPad(Action::RotateClockwise, retropp::PadButton::FaceLabelA));
    EXPECT_TRUE(hasKey(Action::RotateCounterClockwise, SDL_SCANCODE_Z));
    EXPECT_TRUE(hasPad(Action::RotateCounterClockwise, retropp::PadButton::FaceLabelB));
    EXPECT_TRUE(hasKey(Action::Start, SDL_SCANCODE_RETURN));
    EXPECT_TRUE(hasPad(Action::Start, retropp::PadButton::Start));
    EXPECT_TRUE(hasKey(Action::Select, SDL_SCANCODE_RSHIFT));
    EXPECT_TRUE(hasPad(Action::Select, retropp::PadButton::Select));

    for (const retropp::ActionBinding& r : rows) {
        EXPECT_LT(r.action, 7u) << "unexpected row for action id " << int(r.action);
    }
}

// HeldActionsAdapter — heldActions reads the per-tick InputState held levels. Synthesize an
// InputSample, feed it through sampleTick, and assert the adapter returns exactly the sampled set;
// a second tick flips the levels and the adapter follows.
TEST(Input, HeldActionsAdapter) {
    retropp::InputState state;

    retropp::InputSample sample;
    sample.players[0].held = actions({Action::MoveLeft, Action::SoftDrop});
    std::array<retropp::ActionSet, retropp::kMaxPlayers> pressed{};
    pressed[0] = sample.players[0].held;
    state.sampleTick(sample, pressed);
    EXPECT_EQ(kirpich::systems::heldActions(state), actions({Action::MoveLeft, Action::SoftDrop}));

    retropp::InputSample sample2;
    sample2.players[0].held =
        actions({Action::RotateCounterClockwise, Action::Start, Action::Select});
    std::array<retropp::ActionSet, retropp::kMaxPlayers> pressed2{};
    pressed2[0] = sample2.players[0].held;
    state.sampleTick(sample2, pressed2);
    EXPECT_EQ(kirpich::systems::heldActions(state),
              actions({Action::RotateCounterClockwise, Action::Start, Action::Select}));
}
