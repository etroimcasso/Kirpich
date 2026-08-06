#pragma once

// The seven tetromino shapes, in the order the game assigns them.
//
// A piece's identity byte packs kind and rotation together (kind * 4 + rotation); the high bits are
// the kind. That kind byte is used directly as the sprite index into the game's sprite list, whose
// first 28 entries are grouped four-to-a-shape in this exact order (L, J, I, O, S, Z, T). There is
// no "no piece" value - every valid piece byte decodes to one of these seven. The derivation and
// its source anchors are recorded in docs/contracts/sprite-grids.md.

#include <cstdint>

namespace kirpich {

enum class PieceKind : std::uint8_t {
    L = 0,
    J = 1,
    I = 2,
    O = 3,
    S = 4,
    Z = 5,
    T = 6,
};

}  // namespace kirpich
