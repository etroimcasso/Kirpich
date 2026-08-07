#pragma once

#include <cstdint>
#include <vector>

namespace kirpich::assets {

// Encode a greyscale, non-interlaced PNG from row-major index samples.
//
// This is the extractor's save step and nothing else ever includes it: the tile data decoded out
// of the player's ROM has to land on disk as the PNG files the engine loads, and this serializes
// exactly that. Every sample it writes comes from the ROM — nothing here authors content.
//
// `bitDepth` is 1 or 2; each value in `indices` is a sample in [0, 2^bitDepth), and there must be
// exactly `width * height` of them. The written stream carries the sample values unscaled (grey
// value == index), which is how the engine's loader reads them back as palette indices.
//
// The deflate stream uses STORED (uncompressed) blocks: the four files are a few KiB each, so
// compression buys nothing and a stored stream keeps the encoder tiny. The zlib wrapper, the
// Adler-32 over the raw scanlines, and the per-chunk CRC-32 are all correct, so any conformant PNG
// decoder - the engine's included - reads the file back to the same samples.
[[nodiscard]] std::vector<std::uint8_t> writeGreyscalePng(
    const std::vector<std::uint8_t>& indices, int width, int height, int bitDepth);

}  // namespace kirpich::assets
