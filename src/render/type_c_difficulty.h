#pragma once

// The rise values on the Type C difficulty screen, drawn as sprites over the stored box.
//
// The screen is the Type B difficulty screen with two words changed: the heading names Type C and the
// right-hand box is labelled "rise" where Type B labels it "high". Its box is untouched - the same
// frame, the same three compartments a row, the same rules between them. What changes is what the
// compartments hold: a rise is a two-digit number where a starting height is a single digit.
//
// Two digits fit because the box is built for glyphs, not filled by them. A compartment is a cell of
// its own plus the light either side of the thin rule beside it, and a digit's ink is about five
// pixels of the eight its cell spans. Placing the pair by pixel rather than by cell - which is what a
// sprite is for - puts both digits inside the light the compartment already has. Nothing is scaled and
// no art is new: these are the screen's own $90-$99 digits, read through the same regime and palette
// the backdrop is, so they read as part of the box rather than as something laid over it.
//
// The values are drawn through OBJECT palettes, whose lightest entry is see-through, so only the ink
// lands. That is what keeps the box intact: a pair is two 8-pixel tiles where a compartment is one
// cell, so it reaches into the rules either side, and an opaque tile would paint them out and leave
// the box open between its numbers.
//
// The other side of that bargain is that the compartment's stored digit would show through, so the
// screen empties the six cells as it lays itself out (initTypeCDifficultyScreen, systems/menu_screens.h,
// which walks the same kRiseValueRows / kRiseValueCols this reads).
//
// Selection follows the cartridge's own idiom rather than inventing one: every value is drawn, and the
// chosen one is drawn again on top through the object palette, appearing and disappearing on the frame
// timer. That is what a blinking digit cursor does on the Type A and Type B screens - the difference
// here is that the cursor is two glyphs wide because the value is.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // Sprite

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "render/tile_atlas.h"  // TileAtlas

namespace kirpich::render {

// How the two digits of one value sit inside a compartment, in viewport pixels relative to the left
// edge of its cell. The pair is centred on the cell, and the pitch is one pixel tighter than a tile so
// the two glyphs read as one number rather than as two. Both are here rather than buried in the
// drawing code because they are what gets nudged when the pair sits a pixel off on a running build.
inline constexpr int kRiseDigitPitch = 7;
inline constexpr int kRiseTensOffset = -3;

// What the box is showing and which value is current.
//
// `rise` is an index into kTypeCRiseValues (src/systems/rising_floor.h), not the interval itself.
// `selectedVisible` draws the current value's highlight or drops it for a frame, which is how it
// blinks.
struct RiseSelection {
    std::uint8_t rise            = 0;
    bool         selectedVisible = true;
};

// Whether the Type C difficulty screen is the thing on the display, and its values therefore have to
// be drawn.
//
// It is up for longer than the picker that walks it. Name entry paints over whichever difficulty screen
// it was entered from and draws no backdrop of its own, so a Type C round that earns a top score is
// still looking at this box while the player types - and the compartments are blank, because the values
// that fill them are drawn rather than stored.
[[nodiscard]] bool riseValuesShown(kirpich::GameState state, kirpich::GameType type) noexcept;

// The six values' sprites, in back-to-front order: the five that are not current, then the one that
// is, in the ink that says so.
[[nodiscard]] std::vector<retropp::Sprite> riseValueSprites(const RiseSelection& selection,
                                                            const TileAtlas&     atlas,
                                                            std::uint8_t         ramp);

}  // namespace kirpich::render
