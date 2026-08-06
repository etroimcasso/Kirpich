#pragma once

// The gravity table: how many frames a piece waits between automatic drops, per level.
//
// Gravity is a countdown. Every frame the drop timer ticks down; when it reaches zero the piece
// falls one cell and the timer reloads from this table. Higher levels reload a smaller number, so
// the piece falls sooner - 52 frames at level 0 (just under a second) down to 2 frames at level 20.
// The table is non-increasing and its sharpest step is level 9 to 10, where the wait nearly halves.
//
// A row's array position IS the level it applies to, which is why .level is a stored field checked
// at compile time rather than a comment: the lookup indexes the array directly.
//
// framesPerDrop() returns the reload value; storing it into the countdown is the caller's job, as
// it is in the original. The 21 rows are generated from the disassembly by
// tools/asm_parser/parse_gravity.py; edit the values there and regenerate, not here. The behavioral
// spec - level bounds, the heart-mode shift, and what reads the result - lives in
// docs/contracts/gravity.md.

#include <array>
#include <cstddef>
#include <cstdint>

namespace kirpich {

// One row of the gravity table: the level it applies to and the drop interval at that level.
struct FramesPerDropEntry {
    std::uint8_t level;   // identity - the level this row is for; equals the row's array position
    std::uint8_t frames;  // frames the piece waits before gravity pulls it down one cell

    friend constexpr bool operator==(const FramesPerDropEntry&,
                                     const FramesPerDropEntry&) = default;
};
static_assert(sizeof(FramesPerDropEntry) == 2, "FramesPerDropEntry must be two ROM-equivalent bytes");

// The highest level the game reaches. Levelling up stops incrementing here, so the table covers the
// whole reachable range rather than a prefix of a larger one.
inline constexpr std::uint8_t kMaxLevel = 20;

// Heart mode shifts the lookup this many levels up, capping at kMaxLevel - a faster game at the
// same displayed level. It is armed by holding Down while starting a game from the title menu.
inline constexpr std::uint8_t kHeartModeLevelBoost = 10;

// The gravity table, indexed by level.
inline constexpr std::array<FramesPerDropEntry, std::size_t{kMaxLevel} + 1> kFramesPerDrop{{
#include "generated/gravity_data.inc"
}};

// Every row sits at the position its .level names, so indexing by level is exact.
static_assert([] {
    for (std::size_t i = 0; i < kFramesPerDrop.size(); ++i) {
        if (kFramesPerDrop[i].level != static_cast<std::uint8_t>(i)) {
            return false;
        }
    }
    return true;
}(), "kFramesPerDrop rows must be in level order, one row per level");

// The frames between gravity drops at this level. In heart mode the lookup shifts up by
// kHeartModeLevelBoost levels and stops at kMaxLevel, so the fastest gravity is reached early and
// held. `level` must not exceed kMaxLevel.
[[nodiscard]] std::uint8_t framesPerDrop(std::uint8_t level, bool heartMode);

}  // namespace kirpich
