#pragma once

// The background-music selection made on the music-type screen. The stored byte is the on-screen
// cursor's sprite tile number, not a clean 0..3 index: the selection screen moves the cursor across
// tiles $1C..$1F, and SwitchMusic maps the byte to a song by subtracting $17 (so $1F, i.e. offset
// 8, means "music off"). The disassembly names none of these, so the enumerators are port-authored,
// reverse-derived from the cursor bounds and SwitchMusic, and pinned in docs/contracts/core-enums.md.

#include <cstddef>
#include <cstdint>

namespace kirpich {

enum class MusicType : uint8_t {
    MUSIC_A = 0x1C,  // Song A  (SwitchMusic ID 5)
    MUSIC_B = 0x1D,  // Song B  (SwitchMusic ID 6)
    MUSIC_C = 0x1E,  // Song C  (SwitchMusic ID 7)
    OFF     = 0x1F,  // No music (SwitchMusic maps offset 8 -> $FF)
};

// How many choices the music screen offers. The four are contiguous, which is what lets a table be
// indexed by the selection.
inline constexpr std::size_t kMusicTypeCount = 4;

// Where a selection sits in a table of that size: the stored byte less the first of them, the same
// arithmetic the cursor-coordinate lookup does (src/data/misc.h).
//
// A byte outside the four - the value the flow state holds before a selection has been made - has no
// slot, and this returns kMusicTypeCount to say so. A caller indexing a table with it would run off
// the end, so callers test for it rather than trusting the byte.
[[nodiscard]] constexpr std::size_t musicTypeIndex(MusicType type) noexcept {
    const auto raw   = static_cast<std::uint8_t>(type);
    const auto first = static_cast<std::uint8_t>(MusicType::MUSIC_A);
    return raw >= first && raw < first + kMusicTypeCount
               ? static_cast<std::size_t>(raw - first)
               : kMusicTypeCount;
}

}  // namespace kirpich
