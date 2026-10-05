#pragma once

// The arrows that say there is another screen or another page in a direction, for the screens drawn
// into the background map: the settings screen's pages, a carousel's options, a list's rows past its
// window, and a statistics branch's pages.
//
// Each is the game's own selector tile turned a quarter turn, at the shared column, one above the
// heading and one below the body (src/systems/settings_screen.h). An arrow is drawn only where there
// is somewhere to go that way, so the ends of a range are visible rather than something a player
// finds by pressing.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // Sprite

#include "render/tile_atlas.h"      // TileAtlas
#include "state/screen_ui_state.h"  // ScreenUiState

namespace kirpich::render {

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

// A list screen's two end indicators, the same sprite again: up when there is list above the window,
// down when there is list below it (src/systems/list_screen.h). A list that fits the window draws
// neither - the ends of a range are visible rather than something a player finds by pressing.
//
// How long the list is comes from ScreenUiState, recorded by the screen as it painted, because an
// instance's row count is a seam the render bridge cannot reach.
[[nodiscard]] std::vector<retropp::Sprite> listArrows(const kirpich::ScreenUiState& ui,
                                                      std::uint8_t ramp, const TileAtlas& atlas);

// A paged screen's two page arrows, the same sprite once more: up when there is a page before the one
// shown, down when there is one after it (src/systems/page_screen.h). A branch with a single page
// draws neither.
//
// How many pages the branch holds comes from ScreenUiState, recorded by the screen as it painted,
// because an instance's page count is a seam the render bridge cannot reach.
[[nodiscard]] std::vector<retropp::Sprite> statsPageArrows(const kirpich::ScreenUiState& ui,
                                                           std::uint8_t     ramp,
                                                           const TileAtlas& atlas);

}  // namespace kirpich::render
