#pragma once

// What drew each object-buffer entry.
//
// The buffer records where objects are, not which object is which. Its entries are a shared
// resource: a descriptor's parts occupy as many consecutive entries as it has tiles, so a screen
// with wider art pushes everything after it along, and a screen change refills the whole buffer with
// unrelated objects. Entry 3 is a place, not a thing.
//
// The renderer therefore records, as it writes, which descriptor and which part of that descriptor's
// sprite each entry holds. That is the thing's identity, and it is what the bridge names a drawn
// object by — so an object keeps its name while it merely moves, and gets a new one the moment a
// descriptor starts drawing something else. Names are what tie an object to its own past, and the
// alternative — naming an object after the entry it happens to occupy — makes two unrelated objects
// look like one that travelled.
//
// Entries the renderer did not write (the handful the game fills in directly) carry no source; the
// bridge names those after the entry, which is stable because a direct write always targets the same
// fixed place.
//
// This is bookkeeping about the port's own drawing, not machine state the original keeps. It lives
// beside the game state because it has to persist between frames and be readable from both sides of
// the render bridge, and it is cleared whenever the object buffer is.

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/sprite_id.h>

#include "state/engine_state.h"

namespace kirpich {

// How many entries the object buffer holds. Tied to the buffer itself so the two cannot drift.
inline constexpr std::size_t kOamEntryCount = std::tuple_size_v<decltype(EngineState::oam)>;

// The object one buffer entry holds: which descriptor drew it, which composed sprite that descriptor
// was drawing, and which part of that sprite this entry is. Sprite and part together survive an
// object moving; either changing means the descriptor is drawing something else now.
struct OamSource {
    bool         drawn = false;  // false: nothing the renderer drew currently occupies this entry
    std::uint8_t slot  = 0;      // the descriptor that drew it
    SpriteId     sprite{};       // the composed sprite it was drawing
    std::uint8_t part  = 0;      // which part of that sprite

    friend constexpr bool operator==(const OamSource&, const OamSource&) = default;
};

// One source per buffer entry.
struct OamSourceTable {
    std::array<OamSource, kOamEntryCount> entries{};

    // Forget every entry's source. Paired with clearing the buffer itself: an emptied buffer holds
    // nothing, so nothing in it has an identity to carry forward.
    void reset() { *this = OamSourceTable{}; }

    friend bool operator==(const OamSourceTable&, const OamSourceTable&) = default;
};

}  // namespace kirpich
