#include "data/charmap.h"

#include <array>
#include <cstddef>

namespace kirpich {
namespace {

// The 47 rows are transcribed from charmap.asm by tools/asm_parser/parse_charmap.py. Non-ASCII
// sequence bytes are \xHH escapes so the table is byte-identical under every CI toolchain.
constexpr std::array<CharmapEntry, 47> kCharmap{{
#include "data/generated/charmap_data.inc"
}};

}  // namespace

std::span<const CharmapEntry> getCharmap() {
    return kCharmap;
}

std::optional<std::uint8_t> getCharmapTile(std::string_view sequence) {
    for (const CharmapEntry& entry : kCharmap) {
        if (entry.sequence == sequence) {
            return entry.tile;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> encodeCharmapText(std::string_view utf8) {
    std::vector<std::uint8_t> tiles;
    std::size_t pos = 0;
    while (pos < utf8.size()) {
        // Greedy longest match: at this position, the longest charmap sequence that is a prefix
        // wins. For this corpus only the ".”" ligature needs the precedence, but the rule is
        // general — RGBDS resolves text the same way.
        const CharmapEntry* best = nullptr;
        for (const CharmapEntry& entry : kCharmap) {
            const std::size_t len = entry.sequence.size();
            if (len > utf8.size() - pos) {
                continue;
            }
            if (utf8.compare(pos, len, entry.sequence) == 0 &&
                (best == nullptr || len > best->sequence.size())) {
                best = &entry;
            }
        }
        if (best == nullptr) {
            return std::nullopt;  // an unmapped character — rgbasm hard-errors here too
        }
        tiles.push_back(best->tile);
        pos += best->sequence.size();
    }
    return tiles;
}

}  // namespace kirpich
