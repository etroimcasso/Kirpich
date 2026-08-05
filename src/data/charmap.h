#pragma once

// Access to the charmap: the whole table, an exact-sequence tile lookup, and a text encoder.
//
// getCharmap()       — the full 47-row table, for sweeps and consumers that iterate it.
// getCharmapTile()   — the tile for one exact character sequence (a single "×" or ".”"), the shape
//                      in-code character literals want. Whole-sequence match only; no prefix logic.
// encodeCharmapText()— encode a UTF-8 string to tile indices the way RGBDS encodes `db "..."` text:
//                      greedy longest match at each position, all-or-nothing. Any byte with no
//                      charmap entry makes the whole call fail (rgbasm hard-errors the same way),
//                      so the result is either a complete encoding or nothing.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <kirpich/charmap.h>

namespace kirpich {

// The full charmap table in source order (charmap.asm line order).
[[nodiscard]] std::span<const CharmapEntry> getCharmap();

// The tile index for one exact character sequence, or nullopt if the sequence is not mapped.
[[nodiscard]] std::optional<std::uint8_t> getCharmapTile(std::string_view sequence);

// Encode UTF-8 text to tile indices via greedy longest match. Returns nullopt if any position has
// no matching sequence (no partial output). Empty input encodes to an empty vector (success).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> encodeCharmapText(std::string_view utf8);

}  // namespace kirpich
