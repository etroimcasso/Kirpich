#pragma once

// The heart-mode indicator: a heart beside a difficulty screen's heading, saying that the round about
// to be picked will fall at the shifted gravity.
//
// It is the glyph the panel draws beside the level digit during a heart-mode round (printLevel,
// systems/readouts.h), so a player sees before choosing a round what they see while playing one. No
// art of its own.
//
// It is a sprite rather than a write into the background map, for three reasons:
//
//   - A gated declaration cannot strand. The frame declares it while the gate is true and declares
//     nothing when it is false, and the reconciler removes what is no longer declared. A map write has
//     to be undone by whoever wrote it, on every path out of every screen that could be showing it.
//   - An object palette's lightest shade is see-through, so only the ink lands and the backdrop under
//     it survives. A background palette is opaque and would paint out the cell.
//   - Name entry paints over whichever difficulty screen it was entered from and draws no backdrop of
//     its own, so a heart written into the map would follow the player onto that screen and stay
//     there. A gate can say plainly whether it belongs there; a leftover cell cannot.
//
// The Type C rise values are drawn this way for the same reasons (render/type_c_difficulty.h).

#include <cstdint>
#include <optional>

#include <retropp/draw_state.h>  // Sprite

#include <kirpich/game_state.h>

#include "render/tile_atlas.h"  // TileAtlas

namespace kirpich::render {

// How far the heart sits from the corner of the cell after the heading, in viewport pixels. It is here
// rather than buried in the drawing code because it is what gets nudged when the glyph sits a pixel
// off on a running build.
inline constexpr int kHeartIndicatorXOffset = 2;
inline constexpr int kHeartIndicatorYOffset = 0;

// Whether a difficulty screen is on the display, and the indicator therefore has to be drawn. Heart
// mode being off draws none, whatever the state.
//
// Name entry counts, because it has no backdrop of its own: it paints over the difficulty screen it
// was entered from, whose heading is still on the display underneath. That is settled here rather than
// inherited by accident, which is the shape riseValuesShown already has (render/type_c_difficulty.h).
[[nodiscard]] bool heartIndicatorShown(kirpich::GameState state, std::uint8_t heartMode) noexcept;

// The indicator's sprite, or nothing when the gate says it does not belong on screen. Append the
// result to the sprites the object buffer produced.
[[nodiscard]] std::optional<retropp::Sprite> heartIndicatorSprite(kirpich::GameState state,
                                                                  std::uint8_t       heartMode,
                                                                  const TileAtlas&   atlas,
                                                                  std::uint8_t       ramp);

}  // namespace kirpich::render
