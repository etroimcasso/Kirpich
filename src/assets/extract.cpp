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

// How the one ROM this extractor reads identifies itself. Its size is public (extract.h, where the
// driver span is defined against it); the hash and the display name are only needed here.
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

// Create one output directory under the asset root. Returns a player-facing message when it cannot
// be created, and an empty string when it exists afterwards.
std::string ensureDirectory(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        return "The asset directory (" + path.string() + ") could not be created: " +
               error.message() + "\nNothing was extracted.";
    }
    return {};
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

std::span<const std::uint8_t> soundDriverImage(std::span<const std::uint8_t> rom) {
    return rom.subspan(kSoundDriverImageBase, kSoundDriverImageSize);
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

    // Prepare every output in memory before the first file is written, so a failure partway cannot
    // leave a half-populated install behind. Each path is spelled here, at its use site, matching
    // the literals in presence.cpp.
    struct PendingFile {
        std::string               logical;  // asset-root-relative, as the presence check names it
        std::vector<std::uint8_t> bytes;
    };
    std::vector<PendingFile> pending;
    pending.reserve(kTileGraphics.size() + 1);
    for (const TileGraphic& graphic : kTileGraphics) {
        const DecodedGraphic decoded = decodeTileGraphic(rom, graphic);
        const int bitDepth = (graphic.format == TileGraphicFormat::OneBpp) ? 1 : 2;
        pending.push_back({std::string{"assets/gfx/default/"} + std::string{graphic.fileName},
                           writeGreyscalePng(decoded.indices, decoded.width, decoded.height,
                                             bitDepth)});
    }

    // The sound driver goes out verbatim: the machine that runs it wants the cartridge's own bytes,
    // so there is nothing to decode or re-encode.
    const std::span<const std::uint8_t> driver = soundDriverImage(rom);
    pending.push_back({"assets/audio/default/sound_driver.bin",
                       std::vector<std::uint8_t>{driver.begin(), driver.end()}});

    if (std::string error = ensureDirectory(retropp::assetRoot() / "assets/gfx/default");
        !error.empty()) {
        return {.succeeded = false, .message = std::move(error)};
    }
    if (std::string error = ensureDirectory(retropp::assetRoot() / "assets/audio/default");
        !error.empty()) {
        return {.succeeded = false, .message = std::move(error)};
    }

    std::string written;
    for (const PendingFile& file : pending) {
        const fs::path destination = retropp::assetRoot() / file.logical;
        std::ofstream outFile{destination, std::ios::binary | std::ios::trunc};
        outFile.write(reinterpret_cast<const char*>(file.bytes.data()),
                      static_cast<std::streamsize>(file.bytes.size()));
        if (!outFile) {
            spdlog::error("Extraction failed writing {}", destination.string());
            return {.succeeded = false,
                    .message   = "Extraction failed while writing " + destination.string() +
                                 " - the extracted files are incomplete and Kirpich cannot start. "
                                 "Check that the directory is writable and try again."};
        }
        written += "  ";
        written += file.logical;
        written += '\n';
    }

    return {
        .succeeded = true,
        .message   = "Extracted what Kirpich needs from your ROM into " +
                     retropp::assetRoot().string() + ":\n" + written +
                     "Your ROM was only read - it was not copied, moved, or altered.",
    };
}

}  // namespace kirpich::assets
