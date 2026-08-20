#pragma once

// The input layer: the per-tick joypad snapshot, the shared key-repeat (DAS) mechanism, and the
// default button bindings.
//
// The original polls the joypad once per frame (ReadJoypad, tetris.asm:6527-6554): it reads the
// hardware register, packs the eight buttons into one byte (hJoyHeld), and derives the rising edge
// hJoyPressed = held & ~previouslyHeld. Every input the game consumes reads that held/pressed pair.
// This layer reproduces the shape of that snapshot and the edge relation over the engine's
// action-input system, which owns the physical polling. See docs/contracts/input.md.
//
// The engine samples devices into action state keyed by the game's own Action enum
// (include/kirpich/action.h); a per-tick InputState exposes held levels per action. InputSystem
// turns the held level into the game's held/pressed pair by deriving the edge itself — the same
// relation the original uses — rather than reading the engine's own justPressed, so a sub-tick tap
// is dropped exactly as the once-per-frame poll drops it (contract §"Edges").

#include <cstdint>

#include <kirpich/action.h>

#include "retropp/input.h"          // ActionSet, ActionId, InputState, kMaxActions
#include "retropp/input_actions.h"  // ActionMap, PadButton, Source

namespace kirpich::systems {

// One frame's joypad snapshot: the buttons held this tick and the subset newly pressed since the
// previous tick. Held and pressed are the two bytes every input consumer reads (hJoyHeld /
// hJoyPressed in the original); here they are action sets rather than raw button bytes.
struct JoypadState {
    retropp::ActionSet held;
    retropp::ActionSet pressed;

    friend bool operator==(const JoypadState&, const JoypadState&) = default;
};

// Derives the per-tick snapshot from a held-action set. This is the live input path: the engine
// samples the devices, and sample() turns the held set into the held/pressed pair the game reads.
// The previous-held is this system's own mechanism state, not game state (the original's hJoyHeld
// is engine mechanism, not part of GameFlowState).
//
// Demo playback does not go through here. It derives the same relation against its own baseline —
// the previous step of the recording rather than the previous tick's real buttons — and overwrites
// the snapshot mid-frame (DemoSimulateJoypad, tetris.asm:794-796; see systems/demo.h). Routing it
// through sample() would report presses the recording never made and would leave this system's
// history holding the demo's input for the next tick.
class InputSystem {
public:
    // Compute pressed = heldNow & ~previousHeld (ReadJoypad, tetris.asm:6546-6549), store heldNow as
    // the new previous, and return the pair. On the first call after reset() the previous is empty,
    // so every held action reads as newly pressed.
    JoypadState sample(retropp::ActionSet heldNow);

    // Return to boot state: previous-held empty.
    void reset();

private:
    retropp::ActionSet prevHeld_{};
};

// ── Key repeat (DAS) ────────────────────────────────────────────────────────────────────────────
//
// A press fires immediately and arms a long initial delay; while the button is held the delay
// counts down and, on reaching zero, fires and reloads a shorter repeat interval. The original runs
// this discipline at two sites — the piece shift (RotateAndShiftPiece, tetris.asm:5965-6028) and
// name entry (tetris.asm:3984-4058) — sharing one countdown byte (hKeyRepeatTimer, i.e.
// GameFlowState::keyRepeatTimer). Both sites' shared core is keyRepeatFire; the site-specific parts
// (the shift's idle re-arm and wall-charge retry, each site's direction priority) stay with those
// systems. See docs/contracts/input.md §"Key repeat".

inline constexpr std::uint8_t kKeyRepeatInitialDelay = 23;  // tetris.asm:5974, :6008, :3989 ($17)
inline constexpr std::uint8_t kKeyRepeatRate         = 9;   // tetris.asm:5982, :6016, :4024, :4056
inline constexpr std::uint8_t kKeyRepeatBlockedRetry = 1;   // tetris.asm:6001 (shift wall-charge)

// The shared repeat core. `timer` is the caller's countdown byte (passed by reference; callers own
// the storage). Returns true on the ticks the action fires:
//   pressed        → timer = kKeyRepeatInitialDelay; fire.
//   else held      → --timer (uint8 wraparound preserved: 0 decrements to 255, a 255-frame delay);
//                    on reaching zero → timer = kKeyRepeatRate; fire. Otherwise no fire.
//   else           → no fire, timer untouched.
bool keyRepeatFire(std::uint8_t& timer, bool pressed, bool held);

// ── Bindings ────────────────────────────────────────────────────────────────────────────────────

// Sample the held level of every game action from the engine's per-tick InputState into an
// ActionSet — the held set InputSystem::sample consumes on the live path. The one place this layer
// reads InputState. The action walk is spelled explicitly; a new action is added to the walk by the
// system that introduces it.
retropp::ActionSet heldActions(const retropp::InputState& in);

// The default keyboard + gamepad bindings for the piece-control actions. Keyboard follows the
// emulator convention (Z / X for the two rotations, arrows for movement); the two rotation buttons
// bind to the pad's printed A / B so the glyph matches the original Game Boy button on every pad
// family. A rebind surface is future work; the map is a value a settings screen can edit and
// resubmit.
retropp::ActionMap defaultActionMap();

}  // namespace kirpich::systems
