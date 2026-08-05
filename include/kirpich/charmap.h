#pragma once

// One row of the character map: a character sequence and the VRAM tile index it renders as.
//
// The disassembly's text system is a translation table (charmap.asm). Each entry pairs a short
// character sequence with the tile index the tile sheet stores that glyph at. The digits are the
// load-bearing case: "0".."9" map to tile indices $00..$09, so a binary-coded-decimal score digit
// IS its own tile index — the score renderer relies on that identity.
//
// The sequence is stored as its exact upstream UTF-8 bytes, not as a decoded character, because
// two properties rule out a per-character table:
//   * six sequences are multi-byte UTF-8 (×, ♥, ⋯, ©, …, ”), and
//   * one entry is a two-code-point ligature — "." followed by a right double quotation mark — that
//     encodes to a single tile ($9D), distinct from the two tiles "." and ”" would encode to
//     separately. RGBDS resolves text by greedy longest match, so the ligature must win over ".".
//
// Matching is therefore byte-wise on the stored sequence. The table is a plain data surface; the
// lookup and text-encoding behaviour live in src/data/charmap.h.

#include <cstdint>
#include <string_view>

namespace kirpich {

struct CharmapEntry {
    std::string_view sequence;  // UTF-8 bytes exactly as upstream; 1..4 bytes, 1..2 code points
    std::uint8_t     tile;      // VRAM tile index this sequence encodes to
};

}  // namespace kirpich
