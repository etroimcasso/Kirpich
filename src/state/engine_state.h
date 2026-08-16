#pragma once

// The game's mutable global state: the block of work RAM the original keeps at $C000, expressed as
// one plain C++ struct. A single EngineState instance holds everything a running game reads and
// writes frame to frame - the score and its line-clear bookkeeping, the 40-entry sprite staging
// buffer the renderer flushes each frame, and the 256-piece ring the randomizer fills. Nearly every
// gameplay system takes a reference to one of these.
//
// The struct is idiomatic, not a byte image of the ROM: the score is a decimal integer (the ROM's
// packed-decimal shadow is a print-time detail, not stored here), the line-clears list is a bounded
// vector of field-row indices (the ROM stores row addresses ending in a zero word), and the OAM
// attribute byte is unpacked into named flags. The exact field-by-field mapping back to the ROM
// addresses - including the bytes deliberately dropped and the three flags the disassembly never
// labelled - is written up in docs/contracts/engine-state.md.
//
// Every member is zero-initialised, so a default-constructed EngineState is the boot state; reset()
// returns a live instance to it. Filling the state for a new game (seeding the piece ring, etc.) is
// the job of the systems that own those fields, not of this struct.

#include <array>
#include <cstdint>

#include <kirpich/piece.h>

#include "data/bounded_vec.h"

namespace kirpich {

// One sprite in the OAM staging buffer: a screen position, a tile index, and the four DMG object
// attributes unpacked from the ROM's attribute byte (bit 7 priority, bit 6 Y-flip, bit 5 X-flip,
// bit 4 palette select). Live staging can carry any of these, so all four are represented even
// though the static object tables in the data layer only ever set X-flip.
struct OamEntry {
    uint8_t y = 0;             // screen Y (ROM byte 0)
    uint8_t x = 0;             // screen X (ROM byte 1)
    uint8_t tile = 0;          // tile index (ROM byte 2)
    bool behindBg = false;     // attr bit 7: draw behind background colours 1-3
    bool yflip = false;        // attr bit 6: vertical flip
    bool xflip = false;        // attr bit 5: horizontal flip
    bool palette1 = false;     // attr bit 4: use object palette 1 (OBP1) rather than 0

    friend constexpr bool operator==(const OamEntry&, const OamEntry&) = default;
};

// The four line-clear tallies, grouped under the name the ROM gives their shared head label. Each is
// a plain count. The ROM stores these on a five-byte stride; of the four bytes after each count, the
// port carries the first as a separate results-screen display count (see scoreboardDisplayedStats
// below) and re-derives the other three — a per-kind BCD score accumulator — in the render bridge.
// This same struct holds both the running tallies and the display counts. See
// docs/contracts/engine-state.md.
struct LineClearStats {
    uint8_t singles = 0;
    uint8_t doubles = 0;
    uint8_t triples = 0;
    uint8_t tetrises = 0;

    friend constexpr bool operator==(const LineClearStats&, const LineClearStats&) = default;
};

struct EngineState {
    // Sprite staging: 40 objects, flushed to the display each frame (ROM wOAMBuffer, $C000, 160 B).
    std::array<OamEntry, 40> oam{};

    // Score, as a decimal integer with a 999,999 display ceiling (ROM wScore is 3 packed-decimal
    // bytes; the ceiling is enforced by the scoring code, not here).
    uint32_t score = 0;

    // Rows cleared by the current line-clear, as field-row indices 0..17. The ROM stores up to four
    // row addresses terminated by a zero word; size() replaces the terminator.
    BoundedVec<uint8_t, 4> lineClears{};

    // Running line-clear tallies (ROM wLineClearStats / wSinglesCount..wTetrisCount).
    LineClearStats stats{};

    // The four on-screen clear counts shown during the Type B results count-up (ROM byte +1 of each
    // stride-five block: $C0AD/$C0B2/$C0B7/$C0BC). Single-byte BCD on the wire, decimal here. State,
    // not derivable from `stats` (that count drains as this one rises).
    LineClearStats scoreboardDisplayedStats{};

    // Points awarded for the current soft drop (ROM wSoftDropPoints, a 16-bit binary count).
    uint16_t softDropPoints = 0;

    // The count-up display of soft-drop points already drained into the score during the Type B results
    // tally (ROM wSoftDropPointsBCD, $C0C2, 3-byte BCD; decimal here). It rises as softDropPoints falls
    // — the two sum to the pre-tally total — and is not derivable from softDropPoints alone.
    uint16_t softDropPointsTallied = 0;

    // Scoreboard redraw/animation state machine index (ROM wScoreboardState). The value set is
    // owned by the scoreboard code; kept as a raw index here.
    uint8_t scoreboardState = 0;

    // Whether the next-piece preview is hidden (ROM wHidePreviewPiece, written 0/1).
    bool hidePreviewPiece = false;

    // Three flags the disassembly stores in the RAM after wScoreboardState but never labels; named
    // here for their role and anchored to their use sites in docs/contracts/engine-state.md.
    uint8_t scoreboardTallyPhase = 0;     // $C0C6: which phase of the count-up animation is running
    bool blockSoftDropAfterLock = false;  // $C0C7: suppress soft-drop right after a piece locks
    bool scoreRedrawRequested = false;    // $C0CE: the score display needs updating

    // The 256-entry piece ring the randomizer fills; each byte is a packed Piece (ROM wPieceList).
    std::array<Piece, 256> pieceList{};

    // Return every field to its boot (all-zero) value.
    void reset() { *this = EngineState{}; }

    friend bool operator==(const EngineState&, const EngineState&) = default;
};

}  // namespace kirpich
