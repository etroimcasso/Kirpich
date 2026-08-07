#include "assets/extract.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>

#include "assets/png_writer.h"

#include <retropp/asset_registry.h>

#include <spdlog/spdlog.h>

namespace kirpich::assets {
namespace {

namespace fs = std::filesystem;

// The one ROM this extractor reads. Anything else — other revisions, other regions, headered or
// modified dumps — is refused outright: a near-miss ROM would decode to subtly wrong graphics,
// which is exactly the failure the identity gate exists to prevent.
constexpr std::size_t kRomSize = 32768;
constexpr std::string_view kRomSha1 = "74591cc9501af93873f9a5d3eb12da12c0723bbc";
constexpr std::string_view kRomName = "Tetris (World) (Rev 1)";

std::uint32_t rotl(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

// Decode one 2bpp tile (16 bytes: two interleaved bitplanes per row, low plane first) into `out`
// at pixel position (px, py) of a `width`-wide grid. The sample is inverted — 3 minus the plane
// value — matching the shade order the PNGs carry.
void decode2bppTile(std::span<const std::uint8_t> tile, std::vector<std::uint8_t>& out,
                    int px, int py, int width) {
    for (int row = 0; row < 8; ++row) {
        const std::uint8_t lo = tile[static_cast<std::size_t>(row) * 2];
        const std::uint8_t hi = tile[static_cast<std::size_t>(row) * 2 + 1];
        for (int col = 0; col < 8; ++col) {
            const int bit = 7 - col;
            const int value = ((hi >> bit) & 1) * 2 + ((lo >> bit) & 1);
            out[static_cast<std::size_t>(py + row) * width + px + col] =
                static_cast<std::uint8_t>(3 - value);
        }
    }
}

// Decode one 1bpp tile (8 bytes, one plane) the same way; the sample is 1 minus the bit.
void decode1bppTile(std::span<const std::uint8_t> tile, std::vector<std::uint8_t>& out,
                    int px, int py, int width) {
    for (int row = 0; row < 8; ++row) {
        const std::uint8_t byte = tile[static_cast<std::size_t>(row)];
        for (int col = 0; col < 8; ++col) {
            const int bit = 7 - col;
            out[static_cast<std::size_t>(py + row) * width + px + col] =
                static_cast<std::uint8_t>(1 - ((byte >> bit) & 1));
        }
    }
}

ExtractionResult refused(std::string why) {
    return {
        .succeeded = false,
        .message   = std::move(why) + "\n"
                     "\n"
                     "Kirpich needs " + std::string{kRomName} + " - exactly " +
                     std::to_string(kRomSize) + " bytes, SHA-1 " + std::string{kRomSha1} + ".\n"
                     "Nothing was written.",
    };
}

}  // namespace

std::string sha1Hex(std::span<const std::uint8_t> bytes) {
    // FIPS 180-4 SHA-1. One-shot over a small buffer, so the padded message is simply copied.
    std::array<std::uint32_t, 5> h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                                   0xC3D2E1F0u};

    std::vector<std::uint8_t> msg(bytes.begin(), bytes.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>(bitLength >> (i * 8)));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int t = 0; t < 16; ++t) {
            const std::size_t at = chunk + static_cast<std::size_t>(t) * 4;
            w[static_cast<std::size_t>(t)] =
                (static_cast<std::uint32_t>(msg[at]) << 24) |
                (static_cast<std::uint32_t>(msg[at + 1]) << 16) |
                (static_cast<std::uint32_t>(msg[at + 2]) << 8) |
                static_cast<std::uint32_t>(msg[at + 3]);
        }
        for (int t = 16; t < 80; ++t) {
            const std::size_t i = static_cast<std::size_t>(t);
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        for (int t = 0; t < 80; ++t) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (t < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (t < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rotl(a, 5) + f + e + k + w[static_cast<std::size_t>(t)];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::string hex;
    hex.reserve(40);
    for (const std::uint32_t word : h) {
        for (int i = 28; i >= 0; i -= 4) {
            hex.push_back("0123456789abcdef"[(word >> i) & 0xF]);
        }
    }
    return hex;
}

DecodedGraphic decodeTileGraphic(std::span<const std::uint8_t> rom, const TileGraphic& graphic) {
    constexpr int kTilesPerRow = 16;
    const int perTile = bytesPerTile(graphic.format);
    const int tileRows = (graphic.tileCount + kTilesPerRow - 1) / kTilesPerRow;

    DecodedGraphic out;
    out.width = kTilesPerRow * 8;
    out.height = tileRows * 8;
    // The last tile row pads with the block's background: the value a raw-0 pixel maps to.
    const std::uint8_t fill = (graphic.format == TileGraphicFormat::OneBpp) ? 1 : 3;
    out.indices.assign(static_cast<std::size_t>(out.width) * out.height, fill);

    for (int i = 0; i < graphic.tileCount; ++i) {
        const int px = (i % kTilesPerRow) * 8;
        const int py = (i / kTilesPerRow) * 8;
        const std::span<const std::uint8_t> tile =
            rom.subspan(graphic.romOffset + static_cast<std::size_t>(i) * perTile,
                        static_cast<std::size_t>(perTile));
        if (graphic.format == TileGraphicFormat::OneBpp) {
            decode1bppTile(tile, out.indices, px, py, out.width);
        } else {
            decode2bppTile(tile, out.indices, px, py, out.width);
        }
    }
    return out;
}

ExtractionResult extractFromRom(const std::filesystem::path& romPath) {
    // Identify before anything is decoded, refuse before anything is written.
    std::ifstream in{romPath, std::ios::binary};
    if (!in) {
        return refused("The file you chose (" + romPath.string() + ") could not be read.");
    }
    const std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>{in},
                                        std::istreambuf_iterator<char>{}};
    if (rom.size() != kRomSize) {
        return refused("The file you chose (" + romPath.string() + ") is " +
                       std::to_string(rom.size()) + " bytes, so it is not the expected ROM.");
    }
    const std::string actualSha1 = sha1Hex(rom);
    if (actualSha1 != kRomSha1) {
        return refused("The file you chose (" + romPath.string() +
                       ") is not the expected ROM (its SHA-1 is " + actualSha1 + ").");
    }

    // Decode and encode every block in memory before the first file is written, so a failure
    // partway cannot leave a half-populated install behind.
    struct PendingFile {
        std::string_view          fileName;
        std::vector<std::uint8_t> png;
    };
    std::vector<PendingFile> pending;
    pending.reserve(kTileGraphics.size());
    for (const TileGraphic& graphic : kTileGraphics) {
        const DecodedGraphic decoded = decodeTileGraphic(rom, graphic);
        const int bitDepth = (graphic.format == TileGraphicFormat::OneBpp) ? 1 : 2;
        pending.push_back({graphic.fileName,
                           writeGreyscalePng(decoded.indices, decoded.width, decoded.height,
                                             bitDepth)});
    }

    // Write into the directory the presence check reads. The directory path is spelled here, at
    // its use site, matching the literals in presence.cpp.
    const fs::path directory = retropp::assetRoot() / "assets/gfx/default";
    std::error_code dirError;
    fs::create_directories(directory, dirError);
    if (dirError) {
        return {.succeeded = false,
                .message   = "The asset directory (" + directory.string() +
                             ") could not be created: " + dirError.message() +
                             "\nNo graphics were extracted."};
    }

    std::string written;
    for (const PendingFile& file : pending) {
        const fs::path destination = directory / file.fileName;
        std::ofstream outFile{destination, std::ios::binary | std::ios::trunc};
        outFile.write(reinterpret_cast<const char*>(file.png.data()),
                      static_cast<std::streamsize>(file.png.size()));
        if (!outFile) {
            spdlog::error("Extraction failed writing {}", destination.string());
            return {.succeeded = false,
                    .message   = "Extraction failed while writing " + destination.string() +
                                 " - the graphics are incomplete and Kirpich cannot start. Check "
                                 "that the directory is writable and try again."};
        }
        written += "  ";
        written += file.fileName;
        written += '\n';
    }

    return {
        .succeeded = true,
        .message   = "Extracted the game's graphics from your ROM into " + directory.string() +
                     ":\n" + written +
                     "Your ROM was only read - it was not copied, moved, or altered.",
    };
}

}  // namespace kirpich::assets
