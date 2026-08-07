#include "assets/png_writer.h"

#include <array>
#include <cstddef>

namespace kirpich::assets {
namespace {

// CRC-32 (PNG's, the standard reflected polynomial 0xEDB88320) over a byte range.
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();

    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Adler-32 over a byte range (the zlib stream's trailer checksum).
std::uint32_t adler32(const std::uint8_t* data, std::size_t len) {
    constexpr std::uint32_t kMod = 65521;  // largest prime below 2^16
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

void appendBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

// Append a PNG chunk: length, four-byte type, body, and CRC-32 over (type + body).
void appendChunk(std::vector<std::uint8_t>& out, const char (&type)[5],
                 const std::vector<std::uint8_t>& body) {
    appendBE32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t typeStart = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(type[i]));
    }
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t crc = crc32(out.data() + typeStart, 4 + body.size());
    appendBE32(out, crc);
}

// The raw (pre-deflate) image: each scanline is one filter byte (0 = None) followed by the row's
// samples packed MSB-first at `bitDepth` bits per sample.
std::vector<std::uint8_t> packScanlines(const std::vector<std::uint8_t>& indices,
                                        int width, int height, int bitDepth) {
    const int stride = (width * bitDepth + 7) / 8;
    const std::uint8_t maxVal = static_cast<std::uint8_t>((1 << bitDepth) - 1);
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + stride));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type None
        std::vector<std::uint8_t> row(static_cast<std::size_t>(stride), 0);
        int bitPos = 0;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t v = static_cast<std::uint8_t>(
                indices[static_cast<std::size_t>(y) * width + x] & maxVal);
            const int shift = 8 - bitDepth - (bitPos & 7);
            row[static_cast<std::size_t>(bitPos >> 3)] |= static_cast<std::uint8_t>(v << shift);
            bitPos += bitDepth;
        }
        raw.insert(raw.end(), row.begin(), row.end());
    }
    return raw;
}

// Wrap `raw` in a zlib stream built entirely from stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> out;
    out.push_back(0x78);  // CMF: deflate, 32 KiB window
    out.push_back(0x01);  // FLG: no dict; check bits make 0x7801 a multiple of 31

    constexpr std::size_t kMaxBlock = 65535;
    std::size_t pos = 0;
    if (raw.empty()) {
        out.push_back(0x01);  // BFINAL=1, BTYPE=00
        out.push_back(0);     // LEN = 0
        out.push_back(0);
        out.push_back(0xFF);  // NLEN = ~0
        out.push_back(0xFF);
    }
    while (pos < raw.size()) {
        const std::size_t len = (raw.size() - pos < kMaxBlock) ? (raw.size() - pos) : kMaxBlock;
        const bool final = (pos + len >= raw.size());
        out.push_back(final ? 0x01 : 0x00);  // BFINAL bit, BTYPE=00 (stored)
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                   raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    }

    appendBE32(out, adler32(raw.data(), raw.size()));
    return out;
}

}  // namespace

std::vector<std::uint8_t> writeGreyscalePng(const std::vector<std::uint8_t>& indices,
                                            int width, int height, int bitDepth) {
    std::vector<std::uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> ihdr;
    appendBE32(ihdr, static_cast<std::uint32_t>(width));
    appendBE32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(static_cast<std::uint8_t>(bitDepth));
    ihdr.push_back(0);  // colour type 0: greyscale
    ihdr.push_back(0);  // compression: deflate
    ihdr.push_back(0);  // filter method 0
    ihdr.push_back(0);  // no interlace
    appendChunk(png, "IHDR", ihdr);

    appendChunk(png, "IDAT", zlibStored(packScanlines(indices, width, height, bitDepth)));
    appendChunk(png, "IEND", {});
    return png;
}

}  // namespace kirpich::assets
