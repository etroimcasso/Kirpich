#pragma once

// The port's top-level object: one instance owns the game's runtime state and drives it
// through the Retro++ run loop.
//
// It is a skeleton. The game's state, systems, and rendering are not written yet, so it
// currently holds nothing and does nothing — `main()` constructs one to prove the wiring.
// Behavior arrives here as those layers land.

namespace kirpich {

// Non-copyable and non-movable: this is a unique runtime owner, not a value. The deleted
// operations make an accidental copy a compile error rather than a duplicated engine.
class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
};

} // namespace kirpich
