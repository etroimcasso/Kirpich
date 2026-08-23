#pragma once

// The ghost piece: a shadow of the falling piece on the row it would land on.
//
// It is help the original does not give, so it is off until a player asks for it
// (Settings::ghostPiece, the settings screen's second page). Nothing about it reaches the
// simulation: no state, no field on GameContext, no tick of its own. The shadow is derived fresh
// each frame from what the game already holds, and a build with it switched off draws exactly the
// frames a build without it draws.
//
// The shape is not invented here. Each of the falling piece's parts is already a placed sprite -
// the frame submits them every tick - and a sprite can hand back its own image as geometry
// (retropp::Sprite::maskShape). Asked in the layer's space, that shape comes out carrying the
// sprite's flips, its rotation, its transform and its placement, so it is the shape actually on
// screen rather than a rectangle standing in for one. Moving it down by the drop distance is then
// the whole of the drawing: the shadow cannot disagree with the piece about what shape a piece is,
// because it is not told - it asks the same sprite.
//
// The drop distance uses the board and the emptiness test the lock uses (systems/piece.h), so the
// shadow cannot sit anywhere the piece would not come to rest.

#include <cstdint>
#include <vector>

#include <retropp/draw_state.h>  // Region

#include "data/bounded_vec.h"
#include "data/new_pieces.h"   // kMaxNewPieceCells
#include "render/palettes.h"   // kDefaultShadeRamp
#include "render/tile_atlas.h"
#include "systems/game_context.h"
#include "systems/piece.h"  // PieceCell

namespace kirpich::render {

// How many vertices the silhouette trace may spend on one part. The engine's region gate carries up
// to 64 and evaluates the polygon per pixel, so a budget is worth keeping small; a piece part is one
// tile, whose outline needs a handful.
inline constexpr int kGhostShapeVertices = 16;

// How much of the scene the shadow covers. Low enough that the board reads through it, high enough
// that it is not mistaken for part of the backdrop.
inline constexpr float kGhostAlpha = 0.4f;

// How many rows the active piece would fall before it came to rest. Zero when it is already
// resting, which is when there is nothing to show - a shadow under the piece's own feet is noise.
//
// Every cell must be able to fall the whole way, so this is the smallest free run below any of them.
// The test is the lock's: a cell may fall onto an empty space and onto nothing else.
[[nodiscard]] int ghostDropRows(const systems::GameContext& game);

// Whether a shadow belongs on screen at all this frame: a round is being played, the display is on
// the playing field rather than the paused screen, the piece is visible, and the shadow is still
// clear of the piece - which covers a piece already at rest, whose shadow would be entirely under
// it. Device-free - it reads game state and nothing else.
[[nodiscard]] bool ghostVisible(const systems::GameContext& game);

// The board cells the shadow is drawn in: the piece's own, moved down by ghostDropRows.
//
// All of them or none. The shadow is withdrawn whole the moment it would touch the piece casting it,
// which is what ghostVisible answers - the two never share a cell, because a piece block is
// see-through in its middle (an object's lightest colour is transparency rather than a shade,
// render/tile_atlas.h) and a shadow under one shows through the block's own holes and tints it
// whatever order the two are drawn in. Depth decides what draws in front of what; it does not decide
// what shows through. Device-free.
[[nodiscard]] BoundedVec<systems::PieceCell, kMaxNewPieceCells> ghostShadowCells(
    const systems::GameContext& game);

// The shadow, as regions belonging to the BACKGROUND layer.
//
// Empty whenever ghostVisible says so. Otherwise one region per part of the falling piece, each
// carrying the part's own silhouette moved down by ghostDropRows, filled with the darkest colour of
// the ramp in effect and blended at kGhostAlpha.
//
// Assign them to the background layer's `regions`, not to the frame's. A frame region grades the
// finished picture, the objects included, so a shadow attached there grades the piece as well. On the
// background layer the shadow grades the board and the objects composite over it, and the piece
// passes in front of its own shadow.
//
// The shapes are in the sprite layer's space, which is the background layer's space too: both are the
// viewport, parked at the origin, unscrolled (render/sprites.h, render/background.h). Nothing maps
// between them because there is nothing to map.
//
// `tick` is composeSprites' - it reaches the sprites this asks, and through them their keys, by the
// same route. Requires the tile art to have been uploaded, since a sprite resolves its coverage
// against its uploaded sheet.
[[nodiscard]] std::vector<retropp::Region> ghostPieceRegions(
    const systems::GameContext& game, const TileAtlas& atlas, std::uint16_t tick,
    std::uint8_t ramp = kDefaultShadeRamp);

}  // namespace kirpich::render
