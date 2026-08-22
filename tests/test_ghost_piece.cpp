// The ghost piece — behavioral tests over src/render/ghost_piece.h.
//
// Device-free, and that bounds what is covered here. The landing walk and the visibility gate are
// logic over game state and are tested in full. Assembling the regions is not: a sprite resolves its
// own coverage against the uploaded sheet through retropp::Renderer::instance(), which throws unless
// a renderer has been constructed, and constructing one needs a graphics device no test job has. The
// shape itself is therefore owed by hand, on a running build - which is also the only place the
// thing it draws can be judged.
//
// There is no cartridge counterpart to any of this. The original has no ghost piece, so every
// asserted value comes from this feature's own stated contract - except the landing row, which is
// pinned against the lock's own collision test (systems/piece.h) rather than against a number, so
// the shadow cannot claim a resting place the piece could not occupy.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/sprite_id.h>

#include "render/ghost_piece.h"
#include "state/display_state.h"
#include "state/playing_field_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/piece.h"

namespace {

using kirpich::CharTile;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::kActivePieceSlot;
using kirpich::kBoardRows;
using kirpich::SpriteId;
using kirpich::render::ghostDropRows;
using kirpich::render::ghostShadowCells;
using kirpich::render::ghostVisible;
using kirpich::systems::activePieceCells;
using kirpich::systems::detectCollision;
using kirpich::systems::GameContext;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kBrick = 0x28;  // any non-space collides

// The spawn position the round init uses, and the one an upright piece is rotated at.
constexpr std::uint8_t kSpawnY = 0x18;
constexpr std::uint8_t kSpawnX = 0x3F;

// The floor: the row below the playing field, which is what stops a falling piece on an empty board.
constexpr std::size_t kFloorRow = 18;

// A board a piece can fall through, with a floor under it.
void playableField(GameContext& game) {
    for (auto& row : game.field.board) {
        row.fill(kSpace);
    }
    game.field.board[kFloorRow].fill(kBrick);
}

// A game in the state the ghost is drawn in, with the given piece placed.
GameContext playing(SpriteId id, std::uint8_t y = kSpawnY, std::uint8_t x = kSpawnX) {
    GameContext game;
    playableField(game);
    game.flow.gameState    = GameState::NORMAL_GAMEPLAY;
    game.display.displayed = DisplayedMap::FIRST;

    auto& slot    = game.spriteRenderer.slots[kActivePieceSlot];
    slot.spriteId = id;
    slot.y        = y;
    slot.x        = x;
    slot.hidden   = false;
    return game;
}

// Whether the piece would overlap the board if it were `rows` further down, asked of the lock's own
// collision test rather than of anything this feature owns. The context is taken by value, so the
// probe moves nothing.
bool collidesAfterDropping(GameContext game, int rows) {
    auto& slot = game.spriteRenderer.slots[kActivePieceSlot];
    slot.y     = static_cast<std::uint8_t>(slot.y + 8 * rows);
    return detectCollision(game);
}

// The landing row's defining property: the piece fits there, and it does not fit one row lower.
void expectLandsWhereItRests(const GameContext& game, const char* what) {
    const int rows = ghostDropRows(game);
    EXPECT_FALSE(collidesAfterDropping(game, rows))
        << what << ": the shadow sits where the piece cannot";
    EXPECT_TRUE(collidesAfterDropping(game, rows + 1))
        << what << ": the shadow sits above where the piece would stop";
}

// (1) The landing row is where the piece comes to rest - swept over every piece and rotation, and
// over a column of the field with a stack in it as well as an empty one.
TEST(GhostPiece, LandingRowIsWhereThePieceComesToRest) {
    for (std::uint8_t id = 0; id <= static_cast<std::uint8_t>(SpriteId::I_3); ++id) {
        const auto sprite = static_cast<SpriteId>(id);

        {
            GameContext game = playing(sprite);
            expectLandsWhereItRests(game, "empty column");
            EXPECT_GT(ghostDropRows(game), 0) << "a piece at spawn has somewhere to fall";
        }

        {
            // A stack four rows deep under the piece: it must come to rest on top of it, not in it.
            GameContext game = playing(sprite);
            for (std::size_t row = kFloorRow - 4; row < kFloorRow; ++row) {
                game.field.board[row].fill(kBrick);
            }
            expectLandsWhereItRests(game, "on a stack");
        }
    }
}

// (2) The regression the field found: an upright piece at the top of the playing field sits mostly
// ABOVE the first row, and a cell above the first row does not come out negative - the row is an
// eight-bit subtraction shifted right three, so it comes out as 29, 30 or 31. Walking that row down
// has to step around the board rather than off the end of it. Reading the wrap as the bottom of the
// board reported those cells as already landed, so the piece was said to have nowhere to fall and no
// shadow was drawn until it had descended clear of the top.
TEST(GhostPiece, UprightPieceAboveTheFieldStillCastsAShadow) {
    const GameContext game = playing(SpriteId::I_1);  // the I-piece, stood on end

    // The precondition this test exists for. If the spawn ever moves so that the whole piece is
    // inside the field, this stops being the case it was written for and should say so rather than
    // passing quietly.
    bool anyAboveTheField = false;
    for (const auto& cell : activePieceCells(game)) {
        if (cell.row >= kBoardRows - 4) anyAboveTheField = true;
    }
    ASSERT_TRUE(anyAboveTheField)
        << "an upright piece at spawn should have cells above the first row, whose rows wrap high";

    EXPECT_GT(ghostDropRows(game), 0);
    EXPECT_TRUE(ghostVisible(game));
    expectLandsWhereItRests(game, "upright at the top of the field");
}

// (3) A resting piece casts no shadow: it is already where it would land, and a shadow under the
// piece's own feet is noise rather than help.
TEST(GhostPiece, RestingPieceCastsNoShadow) {
    GameContext game = playing(SpriteId::L_0);

    // Drop it onto the floor first, then ask.
    const int rows = ghostDropRows(game);
    ASSERT_GT(rows, 0);
    auto& slot = game.spriteRenderer.slots[kActivePieceSlot];
    slot.y     = static_cast<std::uint8_t>(slot.y + 8 * rows);

    EXPECT_EQ(ghostDropRows(game), 0);
    EXPECT_FALSE(ghostVisible(game));
}

// (4) The shadow goes whole, at the moment it would touch the piece casting it - it is never partly
// drawn and never shares a cell with the piece. Sharing one is what it must not do: a piece block is
// see-through in its middle, so a shadow under one shows through it and tints it, whatever draws in
// front of what. Giving way cell by cell would answer that too, and would take the shadow apart a
// block at a time on the way down; this is the other answer.
//
// Driven down the field a row at a time with the upright I-piece, the shape that comes closest to its
// own shadow over the most rows: four cells in one column.
TEST(GhostPiece, ShadowIsWithdrawnWholeWhenThePieceReachesIt) {
    GameContext game = playing(SpriteId::I_1);
    auto&       slot = game.spriteRenderer.slots[kActivePieceSlot];

    const int fall = ghostDropRows(game);
    ASSERT_GT(fall, 4) << "the piece must start clear of its own shadow for this to be a descent";

    bool withdrawn = false;
    for (int step = 0; step <= fall; ++step) {
        const auto drawn = ghostShadowCells(game);

        // Never a partial shadow: all four cells, or none at all.
        EXPECT_TRUE(drawn.size() == 4u || drawn.empty())
            << "step " << step << ": " << drawn.size() << " cells drawn";
        EXPECT_EQ(drawn.empty(), !ghostVisible(game)) << "step " << step;

        // And never a cell the piece is standing on.
        for (const auto& shadowCell : drawn) {
            for (const auto& pieceCell : activePieceCells(game)) {
                EXPECT_FALSE(shadowCell.row == pieceCell.row && shadowCell.col == pieceCell.col)
                    << "step " << step << ": a shadow cell under the piece at row "
                    << +shadowCell.row;
            }
        }

        // Once gone it stays gone - the shadow does not come back as the piece keeps falling.
        if (drawn.empty()) withdrawn = true;
        EXPECT_FALSE(withdrawn && !drawn.empty()) << "step " << step << ": the shadow came back";

        slot.y = static_cast<std::uint8_t>(slot.y + 8);
    }

    EXPECT_TRUE(withdrawn) << "the shadow must have gone by the time the piece landed";
}

// (5) The gate: a shadow belongs on screen only during a round, on the playing field, behind a piece
// that is visible. Each condition is failed on its own, so no one of them can be carrying the rest.
TEST(GhostPiece, VisibilityGate) {
    {
        const GameContext game = playing(SpriteId::L_0);
        ASSERT_TRUE(ghostVisible(game)) << "the baseline every case below varies one thing from";
    }

    // Not in a round. Every state but the one is refused, swept rather than sampled.
    for (int state = 0; state < 0x36; ++state) {
        if (state == static_cast<int>(GameState::NORMAL_GAMEPLAY)) continue;
        GameContext game    = playing(SpriteId::L_0);
        game.flow.gameState = static_cast<GameState>(state);
        EXPECT_FALSE(ghostVisible(game)) << "state " << state;
    }

    // Paused: the second map is on screen, so the playing field is not what is being shown.
    {
        GameContext game       = playing(SpriteId::L_0);
        game.display.displayed = DisplayedMap::SECOND;
        EXPECT_FALSE(ghostVisible(game));
    }

    // A hidden piece has no position worth shadowing.
    {
        GameContext game = playing(SpriteId::L_0);
        game.spriteRenderer.slots[kActivePieceSlot].hidden = true;
        EXPECT_FALSE(ghostVisible(game));
    }
}

}  // namespace
