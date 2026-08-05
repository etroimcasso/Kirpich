#pragma once

// Which single-player mode is running. Type A is the endless marathon; Type B clears a fixed set of
// lines against pre-filled garbage. The two byte values are non-sequential magic constants the
// game compares directly (e.g. `cp a, $37` / `cp a, $77`); the disassembly names neither, so both
// are port-authored, reverse-derived from the comparison sites and pinned in
// docs/contracts/core-enums.md.
//
// Two-player mode is NOT a game type: it is tracked by a separate flag (hIsMultiplayer) and is
// orthogonal to this enum. There are exactly two values here.

#include <cstdint>

namespace kirpich {

enum class GameType : uint8_t {
    TYPE_A = 0x37,  // Marathon
    TYPE_B = 0x77,  // Fixed line target over starting garbage
};

}  // namespace kirpich
