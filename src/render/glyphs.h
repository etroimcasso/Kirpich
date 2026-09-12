#pragma once

// A run of text, as sprites - one per character.
//
// Sprites rather than cells, so a run sits where it reads best instead of on the background's
// eight-pixel grid, and so it draws over whatever is behind it: through an object palette only the
// ink lands. It is the same way the Type C rise picker puts its digits inside the box's compartments.
//
// The font has the letters, the digits, a period and a hyphen and nothing else. A character it cannot
// spell takes its place in the run and draws nothing. Each glyph is named for where it sits, so a run
// keeps its identity between frames without being handed a name.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "render/tile_atlas.h"
#include "render/types.h"

namespace kirpich::render {

[[nodiscard]] Sprites Glyphs(std::string_view text, int x, int y, int pitch, const TileAtlas& atlas,
                             std::uint8_t ramp);

// Break a passage into lines of at most `width` characters, on word boundaries. A word longer than
// the width takes a line to itself and runs past the edge rather than being cut in the middle. The
// lines point into `text`, so it must outlive them.
[[nodiscard]] std::vector<std::string_view> wrapText(std::string_view text, std::size_t width);

}  // namespace kirpich::render
