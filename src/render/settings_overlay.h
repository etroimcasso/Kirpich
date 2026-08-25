#pragma once

// The parts of the settings screen that are colour and shape rather than text: the palette row's two
// scroll arrows and the preview strip of the ramp they scroll through.
//
// They are drawn as regions over the composited frame rather than as cells, for two reasons. The
// game's art has neither a solid-colour tile nor an arrow to put in one, and a region is placed per
// pixel and shaped by its own points - so an arrow is a triangle and the preview's four squares can
// touch each other with no gap, which is what makes them read as one palette rather than as four
// blocks.
//
// Each shape carries a single ColorFill. The cell coordinates they line up with are the settings
// screen's own (src/systems/settings_screen.h), so the drawn parts and the written ones cannot drift.

#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // Region, Sprite

#include "render/tile_atlas.h"      // TileAtlas
#include "state/screen_ui_state.h"  // ScreenUiState

namespace kirpich::render {

// One preview square's side, in viewport pixels - a whole cell, so the strip lines up with the row
// above it.
inline constexpr int kSwatchSquare  = 8;
inline constexpr int kSwatchSquares = 4;
inline constexpr int kSwatchWidth   = kSwatchSquare * kSwatchSquares;

// How far an arrow's point reaches in from its cell's edges. A larger inset makes a narrower arrow.
inline constexpr int kArrowInset = 2;

// The settings screen's drawn parts, for the page `ui` is on: the palette scroller's two arrows and
// its preview strip on the first page, and on either page the arrow that says another page is there.
//
// An arrow is drawn only where there is somewhere to go: at the first ramp there is no left arrow and
// at the last there is no right one, the first page has no arrow above it and the last none below.
// The ends of a range are visible rather than something a player finds by pressing.
[[nodiscard]] std::vector<retropp::Region> settingsOverlay(const kirpich::ScreenUiState& ui,
                                                           std::uint8_t ramp, int viewportWidth);

// The page arrow, as a sprite: the game's own selector tile turned a quarter turn, so the arrow that
// says "there is another page" is the same arrow that points at everything else.
//
// It is a sprite rather than an object because an object carries only the two flips the hardware has,
// and a flip cannot stand a sideways triangle upright — a quarter turn can. Append the result to the
// sprites the object buffer produced, before they are wrapped as the frame's sprite layer.
//
// Empty on a page with nothing past it in that direction.
[[nodiscard]] std::vector<retropp::Sprite> settingsPageArrows(const kirpich::ScreenUiState& ui,
                                                              std::uint8_t ramp,
                                                              const TileAtlas& atlas);

// A carousel screen's two option arrows, the same sprite the page arrows are: up above the title
// when an option precedes the shown one, down below the body when one follows it
// (src/systems/carousel_screen.h says why the up arrow must sit above the title). With one option
// neither is drawn - the ends of a range are visible rather than something a player finds by
// pressing.
[[nodiscard]] std::vector<retropp::Sprite> carouselArrows(const kirpich::ScreenUiState& ui,
                                                          std::uint8_t ramp, const TileAtlas& atlas,
                                                          std::size_t optionCount);

}  // namespace kirpich::render
