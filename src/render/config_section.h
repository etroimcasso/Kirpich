#pragma once

// An extra choice section on the config screen, drawn as sprites over the background.
//
// The config screen ships with two sections — game type and music type — and room between them for
// one more. This draws that third one in the screen's own hand: a title plate over a framed box with
// two choices either side of a divider, the selected one inked and the other grey, exactly as the two
// sections above and below say which of their choices is current.
//
// It is a sprite layer rather than background cells for one reason: a box in this style is FIVE rows
// of art — plate top, plate text, the frame's top rule, the choices, the frame's bottom rule — and
// moving the two shipped boxes a row apart opens FOUR. Placed by pixel rather than by cell, the
// section sits half a row high and eases four pixels into the empty space above and below the gap, so
// no row of art is lost. A cell cannot do that: it sits on the grid and holds one tile.
//
// Nothing writes the section into a map, so there is no picture to put back and no map state that can
// disagree with the choice it is showing.
//
// The tiles are the screen's own — the same plate, rule, corner and divider art the two shipped boxes
// are built from — read through the gameplay regime the config screen selects. They are drawn through
// the BACKGROUND palette rather than an object one: an object palette's last entry is see-through,
// and these are opaque plates whose lightest shade is the inside of the box.
//
// The section is generic over what it is choosing between. Give it a title and two labels; the
// caller owns what the choice means, where it is stored, and which handler walks it.

#include <cstdint>
#include <string_view>
#include <vector>

#include <retropp/draw_state.h>  // Sprite

#include "render/tile_atlas.h"  // TileAtlas

namespace kirpich::render {

// What the section says. The title goes on the plate and has to fit it exactly; the two labels sit
// either side of the divider, each within its own half of the box.
struct ConfigSectionLabels {
    std::string_view title;
    std::string_view left;
    std::string_view right;
};

// The section's sprites, in back-to-front order.
//
// `rightChosen` says which of the two labels is current. `selectedVisible` inks it or drops it back to
// grey, which is how it blinks — the shipped sections blink by uncovering the grey label under their
// cursor, and this is the same effect with one fewer layer.
[[nodiscard]] std::vector<retropp::Sprite> configSectionSprites(const ConfigSectionLabels& labels,
                                                                bool             rightChosen,
                                                                bool             selectedVisible,
                                                                const TileAtlas& atlas,
                                                                std::uint8_t     ramp);

}  // namespace kirpich::render
