#pragma once

// The backdrop behind the rotation editor's held preview: PreviewBackdrop's brick walls, with the
// Type A panel's level box stamped into it showing which level the preview is drawn for.
//
// The box is the one a player watches in a Type A round - its top edge, the word LEVEL, the number's
// row and the panel's bottom edge - read cell for cell from the Type A playfield's tilemap, so it
// draws exactly as it does beside the well. The number's cell, blank in the tilemap, holds the
// level's digit, as the round's level readout writes it there (systems/readouts.h). Every cell is a
// background cell of the gameplay sheet, through `ramp`'s content palette.

#include <array>
#include <cstddef>
#include <cstdint>

#include "render/tile_atlas.h"
#include "render/types.h"
#include "systems/readouts.h"  // kTypeALevelRow, kTypeALevelCol

namespace kirpich::render {

// The box, in cells.
inline constexpr std::size_t kLevelBoxWidth  = 7;
inline constexpr std::size_t kLevelBoxHeight = 4;

// Where the box comes from in the Type A playfield's tilemap: its first column, and the four rows it
// is made of - the top edge, the word, the number's row and the panel's bottom edge, so the box stands
// closed on its own.
inline constexpr std::size_t kLevelBoxSourceColumn = 13;
inline constexpr std::array<std::size_t, kLevelBoxHeight> kLevelBoxSourceRows{5, 6, 7, 11};

// Where the level's digit stands within the box: the number's row, at the readout's own column.
inline constexpr std::size_t kLevelBoxNumberRow    = 2;
inline constexpr std::size_t kLevelBoxNumberColumn =
    systems::kTypeALevelCol - kLevelBoxSourceColumn;

static_assert(kLevelBoxSourceRows[kLevelBoxNumberRow] == systems::kTypeALevelRow,
              "the box's number row is the readout's own row");
static_assert(kLevelBoxNumberColumn < kLevelBoxWidth, "the number's cell is inside the box");

// PreviewBackdrop's cells through `ramp`, with the box's cells in place of the backdrop's at
// (`column`, `row`) onward and `level % 10`'s digit in the number's cell.
[[nodiscard]] Cells LevelPreviewBackdrop(std::uint8_t level, std::size_t column, std::size_t row,
                                         const TileAtlas& atlas, std::uint8_t ramp);

}  // namespace kirpich::render
