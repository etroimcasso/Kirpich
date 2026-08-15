#pragma once

// The game's input vocabulary: what a press means to the game, independent of which physical button or
// key produces it. The engine's action input system reads state keyed by a game-owned enum (held /
// just-pressed / just-released per action); this is that enum for Kirpich. Physical sources bind to
// these actions in the input system's ActionMap, and the demo recordings replay by feeding a set of
// these actions into the same input path.
//
// These are the piece-control actions plus Start/Select. The gameplay input handler
// (RotateAndShiftPiece, tetris.asm:5910-6028) and the soft-drop path bind A to rotate clockwise, B to
// rotate counter-clockwise, LEFT/RIGHT to shift the piece, and DOWN to soft drop; Start and Select
// drive pausing, menu confirm/cycle, and the soft-reset chord. The enum gains enumerators as more
// input surfaces (menus, name entry) land.

#include <cstdint>

namespace kirpich {

enum class Action : std::uint8_t {
    MoveLeft,                // shift the active piece one column left
    MoveRight,               // shift the active piece one column right
    SoftDrop,                // drop the active piece faster while held
    RotateClockwise,         // rotate the active piece clockwise
    RotateCounterClockwise,  // rotate the active piece counter-clockwise
    Start,                   // START button: pause, menu confirm, the soft-reset chord
    Select,                  // SELECT button: menu cycle, the soft-reset chord
};

}  // namespace kirpich
