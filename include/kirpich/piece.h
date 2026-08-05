#pragma once

// A falling piece's identity byte: the low two bits are the rotation, the rest is the piece kind.
// The game packs both into one byte and separates them by masking (`and a, ~%11` isolates the
// kind; the low two bits are the orientation). PickRandomPiece rerolls until the kind is below
// 7 * 4, so there are 7 kinds (0..6) x 4 rotations (0..3), a valid raw range of 0..27. There is no
// "no piece" sentinel in this byte.
//
// This is the piece-logic representation (what the RNG and piece routines manipulate), distinct
// from the tile indices the renderer later uses. The mapping from kind to a specific tetromino
// (I/O/T/S/Z/J/L) is fixed by the rotation matrices and is introduced with them, so kind() returns
// the raw index rather than a named enum here.

#include <cstdint>

namespace kirpich {

struct Piece {
    uint8_t raw;  // kind * 4 + rotation, valid 0..27

    [[nodiscard]] constexpr uint8_t kind() const { return static_cast<uint8_t>(raw >> 2); }
    [[nodiscard]] constexpr uint8_t rotation() const { return static_cast<uint8_t>(raw & 0x03); }

    // Build an identity byte from a kind (0..6) and rotation (0..3).
    [[nodiscard]] static constexpr Piece of(uint8_t kind, uint8_t rotation) {
        return Piece{static_cast<uint8_t>((kind << 2) | (rotation & 0x03))};
    }

    friend constexpr bool operator==(Piece, Piece) = default;
};

static_assert(sizeof(Piece) == 1, "Piece must be a single ROM-equivalent byte");

}  // namespace kirpich
