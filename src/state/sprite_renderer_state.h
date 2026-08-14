#pragma once

// The sprite-renderer state: the live sprite-object array the original keeps at $C200, expressed as
// one plain C++ struct. Where EngineState (src/state/engine_state.h) holds the $C000 gameplay globals
// and GameFlowState (src/state/game_flow_state.h) holds the main-loop bookkeeping, SpriteRendererState
// holds the high-level sprite descriptors: sixteen $10-byte slots, each a position, a composed-sprite
// id, three unpacked attribute flags, and (for the ending dancers) an animation counter pair. Every
// menu cursor, the active and preview pieces, and the victory / dance / rocket scenes write these
// slots; the renderer walks them each frame and compiles them into the EngineState OAM staging buffer.
//
// The struct is idiomatic, not a byte image of the array. Each slot models only the nine bytes the
// game actually touches ({0,1,2,3,4,5,6,$E,$F}); the seven never-accessed bytes per slot (+7…+$D) are
// omitted. The status byte becomes a bool, the sprite index is the typed SpriteId, and the three
// attribute bytes unpack into named flags rather than raw bits. The renderer itself — the OAM cursor,
// the per-tile working copy, the escape walk over the composed sprite, the flip arithmetic, the
// tile-address lookup — is transient mechanism that lives in HRAM for the length of one render call
// and is re-implemented with locals when the render bridge is built; it is not state and is not
// carried here. The exact byte-by-byte slot map, the HRAM mechanism adjudication, and the boot note
// are written up in docs/contracts/sprite-renderer-state.md.
//
// Every member is zero-initialised, so a default-constructed SpriteRendererState is the boot state
// (the original clears all of work RAM at startup); reset() returns a live instance to it. Filling the
// slots for a scene — placing a cursor, spawning a piece, laying out the dancers — is the job of the
// systems that own those slots, not of this struct. It is a sibling of EngineState and GameFlowState,
// not a member of either; aggregating the state blocks into the running game is later wiring.

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/sprite_id.h>

namespace kirpich {

// One sprite object: a $10-byte slot of the original's $C200 array, reduced to the nine bytes the game
// touches. Byte 0 is the visibility status; bytes 1/2 are the screen position; byte 3 is the composed
// sprite; bytes 4/5/6 each carry part of the object's attribute (OR-composed by the renderer); bytes
// $E/$F are the ending dancers' animation pair. Bytes +7…+$D are never accessed and are not modelled.
struct SpriteSlot {
    bool hidden = false;       // byte 0: status, $80 = hidden / $00 = visible (domain closed to {$00,$80})
    uint8_t y = 0;             // byte 1: screen Y, OAM convention (+16 hardware offset), raw pixels
    uint8_t x = 0;             // byte 2: screen X, OAM convention (+8 hardware offset), raw pixels
    SpriteId spriteId{};       // byte 3: composed-sprite id; for a piece it IS the rotation state
    bool behindBg = false;     // byte 4, bit 7: draw behind background colours 1-3
    bool yflip = false;        // byte 5, bit 6: vertical flip
    bool xflip = false;        // byte 5, bit 5: horizontal flip
    bool palette1 = false;     // byte 6, bit 4: use object palette 1 (OBP1) rather than 0
    uint8_t animCounter = 0;   // byte $E: dancer animation countdown (ticks to zero, then reloads)
    uint8_t animReload = 0;    // byte $F: dancer animation reload value (period)

    friend constexpr bool operator==(const SpriteSlot&, const SpriteSlot&) = default;
};

// The active piece is always slot 0 and the preview piece always slot 1 — the two gameplay-stable
// roles, hardcoded as $C200 / $C210 throughout the original. The scene slots (cursors, victory
// characters, dancers, rockets) have no fixed identity and stay call-site indices; see the contract's
// slot-role inventory.
inline constexpr std::size_t kActivePieceSlot = 0;
inline constexpr std::size_t kPreviewPieceSlot = 1;

struct SpriteRendererState {
    // The sixteen sprite-object slots (ROM $C200, $10-byte stride: ($C300 − $C200) / $10 = 16).
    std::array<SpriteSlot, 16> slots{};

    // Return every slot to its boot (all-zero) value.
    void reset() { *this = SpriteRendererState{}; }

    friend bool operator==(const SpriteRendererState&, const SpriteRendererState&) = default;
};

}  // namespace kirpich
