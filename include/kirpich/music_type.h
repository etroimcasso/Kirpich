#pragma once

// The background-music selection made on the music-type screen. The stored byte is the on-screen
// cursor's sprite tile number, not a clean 0..3 index: the selection screen moves the cursor across
// tiles $1C..$1F, and SwitchMusic maps the byte to a song by subtracting $17 (so $1F, i.e. offset
// 8, means "music off"). The disassembly names none of these, so the enumerators are port-authored,
// reverse-derived from the cursor bounds and SwitchMusic, and pinned in docs/contracts/core-enums.md.

#include <cstdint>

namespace kirpich {

enum class MusicType : uint8_t {
    MUSIC_A = 0x1C,  // Song A  (SwitchMusic ID 5)
    MUSIC_B = 0x1D,  // Song B  (SwitchMusic ID 6)
    MUSIC_C = 0x1E,  // Song C  (SwitchMusic ID 7)
    OFF     = 0x1F,  // No music (SwitchMusic maps offset 8 -> $FF)
};

}  // namespace kirpich
