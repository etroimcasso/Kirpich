#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "data/tile_graphics.h"

// The ROM extractor: how the player's own ROM becomes the files under assets/gfx/default/.
//
// Kirpich ships no graphics. On first start the flow in first_start.h asks the player to locate
// their Game Boy Tetris ROM, then calls extractFromRom() — this module. It identifies the ROM
// (exact size + SHA-1; anything else is refused before a byte is written), decodes the four tile
// blocks kTileGraphics names (src/data/tile_graphics.h), and saves each one as a greyscale PNG at
// the paths the presence check requires. The same files the dev-populate script provides, produced
// from the ROM instead — both routes yield identical content at identical paths.
//
// The audio byte spans the virtual machine will consume are extracted by this module too, once the
// audio backend lands and fixes their output path (see tools/rom_extractor/README.md).

namespace kirpich::assets {

// SHA-1 (FIPS 180-4) of a byte buffer, as a lowercase hex string. The ROM identity check needs
// exactly one hash of one 32 KiB file, so it is implemented here rather than pulling a crypto
// dependency into the stack. Drift-proof: pinned against the FIPS test vectors in the test suite.
[[nodiscard]] std::string sha1Hex(std::span<const std::uint8_t> bytes);

// One graphics block decoded out of the ROM: row-major palette indices (0..3 for 2bpp, 0..1 for
// 1bpp), 16 tiles per row, the last row padded with the block's background value — exactly the
// pixel content the corresponding PNG carries.
struct DecodedGraphic {
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> indices;  // one sample per pixel, row-major (width * height)
};

// Decode `graphic`'s tile block from `rom`. Precondition: `rom` is the full 32,768-byte ROM the
// identity gate accepted — every table row's decode window fits inside it (the test suite proves
// this for the whole table).
[[nodiscard]] DecodedGraphic decodeTileGraphic(std::span<const std::uint8_t> rom,
                                               const TileGraphic& graphic);

// What an extraction attempt did. `message` is player-facing and explains the outcome whether it
// succeeded or not.
struct ExtractionResult {
    bool        succeeded = false;
    std::string message;
};

// Extract every required graphic from `romPath` into assets/gfx/default/, at the paths the
// presence check requires (spelled out in src/assets/presence.cpp).
//
// The ROM is identified first — exact size and SHA-1 — and anything else is refused with a message
// naming the expected ROM; nothing is written on refusal. All four blocks are decoded in memory
// before the first file is written, so a decodable-but-wrong file cannot leave a partial install.
// Every run rewrites all four files.
[[nodiscard]] ExtractionResult extractFromRom(const std::filesystem::path& romPath);

}  // namespace kirpich::assets
