#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "data/sfx.h"
#include "data/tile_graphics.h"

// The ROM extractor: how the player's own ROM becomes the files Kirpich needs to run.
//
// Kirpich ships no graphics and no sound. On first start the flow in first_start.h asks the player
// to locate their Game Boy Tetris ROM, then calls extractFromRom() — this module. It identifies the
// ROM (exact size + SHA-1; anything else is refused before a byte is written), then produces two
// kinds of output:
//
//   * the four tile blocks kTileGraphics names (src/data/tile_graphics.h), decoded and saved as
//     greyscale PNGs under assets/gfx/default/;
//   * the sound driver's image, copied out verbatim as raw bytes to
//     assets/audio/default/sound_driver.bin, which the audio system places into the machine that
//     runs the game's original sound engine.
//
// These are the same files the dev-populate script provides — both routes yield identical content
// at identical paths, so the presence check covers either.

namespace kirpich::assets {

// The size of the one ROM this extractor accepts. Anything else — other revisions, other regions,
// headered or modified dumps — is refused outright: a near-miss ROM would decode to subtly wrong
// graphics and a sound driver that runs but plays the wrong thing.
inline constexpr std::size_t kRomSize = 32768;

// The sound driver's image inside the ROM: one span running from the audio section's base (named by
// the SFX data, src/data/sfx.h) to the end of the ROM.
//
// It is one span rather than two because the driver's code and data sit in the audio section while
// its two entry trampolines sit near the very top of the ROM, with unused padding between. Taking
// everything from the section base to the end carries both, and the padding is inert — the driver
// never executes or reads it.
inline constexpr std::size_t kSoundDriverImageBase = kAudioSectionBase;
inline constexpr std::size_t kSoundDriverImageEnd  = kRomSize;
inline constexpr std::size_t kSoundDriverImageSize = kSoundDriverImageEnd - kSoundDriverImageBase;

// The driver's two entry points, both inside the image: the tick the audio system runs once per
// frame, and the init it runs once when the driver starts. Each is a three-byte jump into the audio
// section; the test suite checks both against the ROM rather than taking them on trust.
inline constexpr std::size_t kSoundDriverTickEntry = 0x7FF0;
inline constexpr std::size_t kSoundDriverInitEntry = 0x7FF3;

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

// The sound driver's image within `rom`, as a view over the span above — no copy and no transform,
// because the machine that runs the driver wants exactly the bytes the cartridge held.
// Precondition: `rom` is the full 32,768-byte ROM the identity gate accepted.
[[nodiscard]] std::span<const std::uint8_t> soundDriverImage(std::span<const std::uint8_t> rom);

// What an extraction attempt did. `message` is player-facing and explains the outcome whether it
// succeeded or not.
struct ExtractionResult {
    bool        succeeded = false;
    std::string message;
};

// Extract everything Kirpich requires from `romPath` — the four graphics and the sound driver
// image — to the paths the presence check requires (spelled out in src/assets/presence.cpp).
//
// The ROM is identified first — exact size and SHA-1 — and anything else is refused with a message
// naming the expected ROM; nothing is written on refusal. Every output is prepared in memory before
// the first file is written, so a failure partway cannot leave a half-populated install. Every run
// rewrites every file.
[[nodiscard]] ExtractionResult extractFromRom(const std::filesystem::path& romPath);

}  // namespace kirpich::assets
