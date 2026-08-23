#include "render/ghost_piece.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

#include <retropp/geometry.h>   // Space
#include <retropp/transform.h>  // Transform

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "render/sprites.h"  // oamEntrySprite, kSpriteSizePx
#include "state/display_state.h"
#include "state/playing_field_state.h"
#include "state/sprite_renderer_state.h"  // kActivePieceSlot
#include "systems/piece.h"                // activePieceCells

namespace kirpich::render {

namespace {

constexpr auto kEmptyCell = static_cast<std::uint8_t>(CharTile::SPACE);

// Whether the piece would collide with the board if it were `rows` further down.
//
// The row steps around the board rather than off the end of it, matching the arithmetic the row came
// from: a cell's row is an eight-bit subtraction shifted right three (systems/piece.cpp), so a cell
// above the top of the playing field is row 29, 30 or 31 rather than anything negative. Stepping such
// a row down is stepping it around the same thirty-two, which tracks the piece's real position - a
// cell three rows above the field is row 29, and seventeen rows further down it is row 14, where it
// physically is. Three quarters of an upright piece sits above the field at spawn, so this is the
// ordinary case rather than an edge one.
bool collidesBelow(const systems::GameContext& game,
                   const BoundedVec<systems::PieceCell, kMaxNewPieceCells>& cells, int rows) {
    for (const systems::PieceCell& cell : cells) {
        const std::size_t row = (static_cast<std::size_t>(cell.row) +
                                 static_cast<std::size_t>(rows)) % kBoardRows;
        if (game.field.board[row][cell.col] != kEmptyCell) {
            return true;
        }
    }
    return false;
}

// Build the shadow's cell list from a filled prefix. BoundedVec is constructed rather than appended
// to - it has no mutating operations - the same way the line-clear list is built.
BoundedVec<systems::PieceCell, kMaxNewPieceCells> makeShadowCells(
    const std::array<systems::PieceCell, kMaxNewPieceCells>& cells, std::size_t count) {
    switch (count) {
        case 1: return {cells[0]};
        case 2: return {cells[0], cells[1]};
        case 3: return {cells[0], cells[1], cells[2]};
        case 4: return {cells[0], cells[1], cells[2], cells[3]};
        case 5: return {cells[0], cells[1], cells[2], cells[3], cells[4]};
        default: return {};
    }
}

// Where one cell of the piece would be after falling `rows`. The row steps around the board, as the
// landing walk's does, so a cell still above the playing field is placed where it physically is.
systems::PieceCell landingOf(const systems::PieceCell& from, int rows) {
    return systems::PieceCell{
        .row = static_cast<std::uint8_t>(
            (static_cast<std::size_t>(from.row) + static_cast<std::size_t>(rows)) % kBoardRows),
        .col  = from.col,
        .tile = from.tile,
    };
}

// Whether the shadow would fall on the piece casting it: any one of the landing cells sharing a
// cell with any one of the cells the piece is on now.
//
// It is a question about the whole shadow, not about one part of it: the shadow is drawn entire or
// not at all, and this is what decides which.
//
// The shadow and the piece cannot share a cell. A piece block is SEE-THROUGH in its middle - an
// object's lightest colour is transparency rather than a shade (render/tile_atlas.h) - so a shadow
// under a block shows through the block's own holes and tints it, whatever order the two are drawn
// in. Depth decides what draws in front of what; it does not decide what shows through.
bool shadowWouldTouchThePiece(const BoundedVec<systems::PieceCell, kMaxNewPieceCells>& cells, int rows) {
    for (const systems::PieceCell& from : cells) {
        const systems::PieceCell landing = landingOf(from, rows);
        for (const systems::PieceCell& over : cells) {
            if (over.row == landing.row && over.col == landing.col) return true;
        }
    }
    return false;
}

// The board cell one object-buffer entry covers, by the arithmetic the lock's cell map uses
// (systems/piece.cpp): the hardware's object offsets removed, then the eight-pixel tile size divided
// out, all of it eight-bit. A cell above the playing field is a high row rather than a negative one,
// the same wrap collidesBelow steps around.
systems::PieceCell cellOfEntry(const OamEntry& entry) {
    return systems::PieceCell{
        .row  = static_cast<std::uint8_t>(static_cast<std::uint8_t>(entry.y - 0x10) >> 3),
        .col  = static_cast<std::uint8_t>(static_cast<std::uint8_t>(entry.x - 0x08) >> 3),
        .tile = entry.tile,
    };
}

}  // namespace

int ghostDropRows(const systems::GameContext& game) {
    const BoundedVec<systems::PieceCell, kMaxNewPieceCells> cells = systems::activePieceCells(game);
    if (cells.empty()) {
        return 0;
    }

    // A piece already overlapping something has no honest landing row - it is mid-lock. The test is
    // the lock's own, so the two cannot disagree about what overlapping means.
    if (systems::detectCollision(game)) {
        return 0;
    }

    // The board is what stops the walk: the floor below the playing field is not empty, so every
    // column runs into something. The bound is the board's own height, since a piece that has
    // stepped that far has come back around to where it started and nothing below it is real.
    int rows = 0;
    while (rows < static_cast<int>(kBoardRows) && !collidesBelow(game, cells, rows + 1)) {
        ++rows;
    }
    return rows;
}

bool ghostVisible(const systems::GameContext& game) {
    if (game.flow.gameState != GameState::NORMAL_GAMEPLAY) {
        return false;
    }
    // Pausing puts the second map on screen; the playing field is not what is being shown, so
    // nothing about the piece belongs on it.
    if (game.display.displayed != DisplayedMap::FIRST) {
        return false;
    }
    if (game.spriteRenderer.slots[kActivePieceSlot].hidden) {
        return false;
    }

    // A shadow the piece has come down onto is withdrawn whole rather than eroded - see
    // shadowWouldTouchThePiece. This also covers a piece already at rest, whose every landing cell is
    // a cell it is standing on.
    return !shadowWouldTouchThePiece(systems::activePieceCells(game), ghostDropRows(game));
}

BoundedVec<systems::PieceCell, kMaxNewPieceCells> ghostShadowCells(const systems::GameContext& game) {
    if (!ghostVisible(game)) {
        return {};
    }

    // Visible means the shadow is clear of the piece entirely, so every cell is drawn.
    const BoundedVec<systems::PieceCell, kMaxNewPieceCells> occupied = systems::activePieceCells(game);
    const int                               rows     = ghostDropRows(game);

    std::array<systems::PieceCell, kMaxNewPieceCells> shadow{};
    std::size_t                                      count = 0;
    for (const systems::PieceCell& from : occupied) {
        shadow[count++] = landingOf(from, rows);
    }
    return makeShadowCells(shadow, count);
}

std::vector<retropp::Region> ghostPieceRegions(const systems::GameContext& game,
                                               const TileAtlas& atlas, std::uint16_t tick,
                                               std::uint8_t ramp) {
    std::vector<retropp::Region> regions;
    if (!ghostVisible(game)) {
        return regions;
    }

    const int  dropRows = ghostDropRows(game);
    const auto drop     = static_cast<float>(dropRows * kSpriteSizePx);
    const auto fill     = rampColours(ramp)[0];  // the darkest of the four

    // The cells the shadow is drawn in - the piece's own, moved down, less the ones the piece has
    // already descended onto. Taken from ghostShadowCells rather than worked out again here, so the
    // rule that is tested is the rule that draws.
    const BoundedVec<systems::PieceCell, kMaxNewPieceCells> shadow = ghostShadowCells(game);
    const auto inShadow = [&shadow](const systems::PieceCell& cell) {
        for (const systems::PieceCell& drawn : shadow) {
            if (drawn.row == cell.row && drawn.col == cell.col) return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < game.engine.oam.size(); ++i) {
        // The parts of the falling piece, and only those: the renderer records which descriptor drew
        // each entry, and the active piece has one of its own (systems/oam_source.h).
        const OamSource& src = game.oamSources.entries[i];
        if (!src.drawn || src.slot != kActivePieceSlot) {
            continue;
        }

        // This part's own landing cell. The row steps around the board, as the walk's does, so a
        // part still above the playing field is compared where it physically is.
        const systems::PieceCell from = cellOfEntry(game.engine.oam[i]);
        const systems::PieceCell landing{
            .row = static_cast<std::uint8_t>(
                (static_cast<std::size_t>(from.row) + static_cast<std::size_t>(dropRows)) %
                kBoardRows),
            .col  = from.col,
            .tile = from.tile,
        };
        if (!inShadow(landing)) {
            continue;
        }

        // The sprite the frame submits for this entry, asked for its own shape. Layer space is what
        // puts the flips, the rotation and the placement into the answer; the sprite layer is the
        // viewport parked at the origin (render/sprites.h), so a layer point is a viewport pixel and
        // the shape needs no mapping of ours.
        // Off-screen parts included: at the top of the field most of an upright piece is above the
        // first row, and all of it is inside the field at the row it lands on, so every part
        // contributes its shape whether or not it is currently on screen.
        const auto sprite = oamEntrySprite(game.engine, game.oamSources, i, game.display.sheet,
                                           tick, atlas, ramp, /*includeOffScreen=*/true);
        if (!sprite) {
            continue;
        }

        retropp::ShapePoints shape = sprite->maskShape(kGhostShapeVertices, retropp::Space::Layer);
        if (shape.points.empty()) {
            continue;  // a wholly see-through part casts no shadow
        }
        shape.transform = retropp::Transform::translation(0.0f, drop);

        regions.push_back(retropp::Region{
            // Regions are not interpolated, so a key need only be present. The entry it came from
            // keeps them distinct within the frame.
            .key     = retropp::ObjectKey{"ghost-" + std::to_string(i)},
            .shape   = std::move(shape),
            .effects = {retropp::ScreenSpaceEffect{
                .kind = retropp::ScreenSpaceEffectKind::ColorFill, .fill = fill}},
            .alpha   = kGhostAlpha,
        });
    }
    return regions;
}

}  // namespace kirpich::render
