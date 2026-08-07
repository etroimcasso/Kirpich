#pragma once

// The playing field: its fixed extent, and the schedule that redraws it a row at a time.
//
// The field is kPlayingFieldRows by kPlayingFieldCols cells (18 x 10). After a line clear compacts
// the stack, and again for the game-over curtain, the field is copied back to the screen one row per
// frame from the bottom up - a "wipe" - driven by a counter the original steps from
// kPlayingFieldWipeCounterFirst (2) to kPlayingFieldWipeCounterLast (19). Each counter value redraws
// one row; the last value redraws the top row and clears the counter, ending the wipe. The whole
// walk is 18 frames, one per field row.
//
// playingFieldRowForWipeCounter maps a counter value to the field row it redraws (0 = top,
// kPlayingFieldRows - 1 = bottom). That mapping is the whole of this unit's behavior. The row copy
// itself, the counter's lifecycle, and the side effects a few particular rows trigger belong to the
// gameplay and presentation loops and are specified, with source line anchors, in
// docs/contracts/playing-field.md.
//
// The four constants are generated from the disassembly by tools/asm_parser/parse_playing_field.py;
// edit the source and regenerate, not here.

#include <cstdint>

namespace kirpich {

#include "generated/playing_field_data.inc"

// The field row (0 = top .. kPlayingFieldRows - 1 = bottom) the wipe redraws at this counter value.
// The counter runs from kPlayingFieldWipeCounterFirst at the bottom row to
// kPlayingFieldWipeCounterLast at the top, so a higher counter names a higher row on screen (a lower
// row index).
//
// Precondition: counter is in [kPlayingFieldWipeCounterFirst, kPlayingFieldWipeCounterLast]. Outside
// that range the original never reaches a row copy - every wipe routine's gate fails - so the query
// has no defined answer there and none is invented.
constexpr std::uint8_t playingFieldRowForWipeCounter(std::uint8_t counter) {
    return static_cast<std::uint8_t>(kPlayingFieldWipeCounterLast - counter);
}

}  // namespace kirpich
