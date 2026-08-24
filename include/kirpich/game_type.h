#pragma once

// Which single-player mode is running. Type A is the endless marathon; Type B clears a fixed set of
// lines against pre-filled garbage; Type C is the marathon with a floor that rises under the player.
//
// The first two byte values are non-sequential magic constants the cartridge compares directly (e.g.
// `cp a, $37` / `cp a, $77`); the disassembly names neither, so both are port-authored, reverse-derived
// from the comparison sites and pinned in docs/contracts/core-enums.md. Type C is the port's own and
// takes a value that collides with neither.
//
// Those two cartridge values double as x-coordinates for the config screen's two-choice cursor
// (src/systems/menu_screens.cpp); Type C's byte carries no coordinate meaning, so a screen that
// selects it places its cursor itself. No save format stores a GameType, so the value is free to be
// whatever reads clearly.
//
// Two-player mode is NOT a game type: it is tracked by a separate flag (hIsMultiplayer) and is
// orthogonal to this enum.

#include <cstdint>

namespace kirpich {

enum class GameType : uint8_t {
    TYPE_A = 0x37,  // Marathon
    TYPE_B = 0x77,  // Fixed line target over starting garbage
    TYPE_C = 0xC7,  // Marathon over a floor that rises every few pieces
};

}  // namespace kirpich
