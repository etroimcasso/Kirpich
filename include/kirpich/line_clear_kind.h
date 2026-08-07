#pragma once

// The four kinds of line clear, in the order the game discriminates them.
//
// The values are the game's own: the Type-B scoreboard state machine walks states 0-3 in this
// order when tallying each kind's clears, and the per-kind clear counters sit in RAM at a 5-byte
// stride in the same order (singles, doubles, triples, tetrises at offsets 0/5/10/15). There is no
// "none" value - every award resolves to one of the four, since clearing lines always clears one
// to four of them. The derivation and its source anchors are recorded in
// docs/contracts/scoring.md.

#include <cstdint>

namespace kirpich {

enum class LineClearKind : std::uint8_t {
    SINGLE = 0,
    DOUBLE = 1,
    TRIPLE = 2,
    TETRIS = 3,
};

}  // namespace kirpich
