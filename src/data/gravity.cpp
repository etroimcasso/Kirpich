#include "data/gravity.h"

#include <cassert>

namespace kirpich {

std::uint8_t framesPerDrop(std::uint8_t level, bool heartMode) {
    // Nothing in the game produces a higher level: the menus offer 0-9, a two-player game forces 1,
    // and levelling up stops at kMaxLevel. The original has no bounds check because it cannot be
    // reached; the port asserts rather than inventing a result for an input the game never asks for.
    assert(level <= kMaxLevel && "level is out of range");

    std::size_t index = level;
    if (heartMode) {
        // The shift is computed wide and then capped, matching the original's add-then-compare: a
        // boosted level of exactly kMaxLevel passes through untouched, anything past it caps.
        index += kHeartModeLevelBoost;
        if (index > kMaxLevel) {
            index = kMaxLevel;
        }
    }
    return kFramesPerDrop[index].frames;
}

}  // namespace kirpich
