// Piece system — behavioral tests against docs/contracts/piece-system.md.
//
// Device-free: the piece functions are pure logic over the game-state aggregate. Every asserted value
// is traced to the tetris.asm lines named in the contract (NextPiece :5078, DropPiece :5194,
// RotateAndShiftPiece :5910, DetectCollision :6030, LockPieceIntoBackground :6068, and the geometry
// laws in _RenderSprites :6774 / _LookupTile :6558). The active-piece cell geometry is checked against
// values hand-traced from those routines. No ROM read, no virtual machine.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/piece.h>
#include <kirpich/sprite_id.h>

#include "data/scoring.h"
#include "data/sprites.h"
#include "retropp/input.h"
#include "systems/game_context.h"
#include "systems/input.h"
#include "systems/piece.h"

namespace {

using kirpich::Action;
using kirpich::ActiveDemo;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::getSprite;
using kirpich::kActivePieceSlot;
using kirpich::kPreviewPieceSlot;
using kirpich::kScoreSaturation;
using kirpich::Piece;
using kirpich::SpriteId;
using kirpich::SquareSfxId;
using kirpich::WaveSfxId;
using kirpich::systems::activePieceCells;
using kirpich::systems::detectCollision;
using kirpich::systems::dropPiece;
using kirpich::systems::GameContext;
using kirpich::systems::kKeyRepeatBlockedRetry;
using kirpich::systems::kKeyRepeatInitialDelay;
using kirpich::systems::kKeyRepeatRate;
using kirpich::systems::lockPieceIntoBackground;
using kirpich::systems::nextPiece;
using kirpich::systems::PieceCell;
using kirpich::systems::rotateAndShiftPiece;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);  // 0x2F
constexpr std::uint8_t kBrick = 0x28;                                       // any non-space collides

// Build an ActionSet from a list of game actions.
retropp::ActionSet actions(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// Fill the whole board with the empty-space tile — the playable state the title screen sets up (boot
// is all-zero, which collides). Tests place obstacles on top of this.
void spaceField(GameContext& game) {
    for (auto& row : game.field.board) {
        row.fill(kSpace);
    }
}

// Place the active piece: identity, position, visibility.
void setActive(GameContext& game, SpriteId id, std::uint8_t y, std::uint8_t x, bool hidden = false) {
    auto& slot = game.spriteRenderer.slots[kActivePieceSlot];
    slot.spriteId = id;
    slot.y = y;
    slot.x = x;
    slot.hidden = hidden;
}

void expectCell(const PieceCell& c, int row, int col, int tile) {
    EXPECT_EQ(c.row, row);
    EXPECT_EQ(c.col, col);
    EXPECT_EQ(c.tile, tile);
}

constexpr std::uint8_t kSpawnY = 0x18;
constexpr std::uint8_t kSpawnX = 0x3F;

}  // namespace

// 1. PieceCellGeometry — the add/adc position law (tetris.asm:6774-6823) plus the tile-lookup cell map
// (:6558-6584, +$30 shadow at :6044). Two piece sprites are hand-traced at the spawn position; the
// carry from (offset + slotPos) leaks into the (+ partOffset) add, so with the negative piece offsets
// the low byte and carry both matter. Plus the hidden-piece $FF-Y law and the four-parts/no-skip
// invariant across all 28 piece-rotation ids.
TEST(PieceSystem, PieceCellGeometry) {
    GameContext game;

    // L_0 at spawn → tile 0x84 at (1,5)(1,6)(1,7)(2,5).
    setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
    auto cells = activePieceCells(game);
    ASSERT_EQ(cells.size(), 4u);
    expectCell(cells[0], 1, 5, 0x84);
    expectCell(cells[1], 1, 6, 0x84);
    expectCell(cells[2], 1, 7, 0x84);
    expectCell(cells[3], 2, 5, 0x84);

    // J_0 at spawn → tile 0x81 at (1,5)(1,6)(1,7)(2,7).
    setActive(game, SpriteId::J_0, kSpawnY, kSpawnX);
    cells = activePieceCells(game);
    ASSERT_EQ(cells.size(), 4u);
    expectCell(cells[0], 1, 5, 0x81);
    expectCell(cells[1], 1, 6, 0x81);
    expectCell(cells[2], 1, 7, 0x81);
    expectCell(cells[3], 2, 7, 0x81);

    // Hidden law: every part's OAM Y becomes $FF → row 29; the real X (and so the column) is kept.
    setActive(game, SpriteId::L_0, kSpawnY, kSpawnX, /*hidden=*/true);
    cells = activePieceCells(game);
    ASSERT_EQ(cells.size(), 4u);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        EXPECT_EQ(cells[i].row, 29) << "hidden part " << i;
    }
    EXPECT_EQ(cells[0].col, 5);
    EXPECT_EQ(cells[1].col, 6);
    EXPECT_EQ(cells[2].col, 7);
    EXPECT_EQ(cells[3].col, 5);

    // Every one of the 28 piece-rotation sprites composes to exactly four parts, no skips — the
    // invariant activePieceCells relies on and that matches the four-slot collision/lock loops.
    for (std::uint8_t id = 0x00; id <= 0x1B; ++id) {
        EXPECT_EQ(getSprite(static_cast<SpriteId>(id)).parts.size(), 4u)
            << "sprite id 0x" << std::hex << static_cast<int>(id);
    }
}

// 2. DetectCollisionVerdicts — collide when any covered cell is not the empty space (tetris.asm:6047,
// the CharTile::SPACE tie). Empty field at spawn does not collide; any non-space byte under a cell
// does, including the zero boot byte.
TEST(PieceSystem, DetectCollisionVerdicts) {
    GameContext game;
    spaceField(game);
    setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);  // covers (1,5)(1,6)(1,7)(2,5)

    EXPECT_FALSE(detectCollision(game));  // empty field at spawn

    game.field.board[1][5] = kBrick;  // a brick under one covered cell
    EXPECT_TRUE(detectCollision(game));

    game.field.board[1][5] = 0x00;  // the zero boot byte is not space → still collides
    EXPECT_TRUE(detectCollision(game));

    game.field.board[1][5] = kSpace;  // back to empty → no collision
    EXPECT_FALSE(detectCollision(game));

    // A brick under a cell the piece does not cover is not a collision.
    game.field.board[5][5] = kBrick;
    EXPECT_FALSE(detectCollision(game));
}

// 3. RotateVectors — the four-orientation step with block-wrap (tetris.asm:5925-5949), B-before-A
// priority (:5920-5923), the hidden guard (:5911-5914), collision revert, and the SFX write-then-
// cancel (:5952-5963).
TEST(PieceSystem, RotateVectors) {
    // Clockwise: decrement, with orientation 0 wrapping up to 3 within the piece's four-id block.
    struct Case { SpriteId from, to; };
    const std::array<Case, 4> cw = {{{SpriteId::L_0, SpriteId::L_3},
                                     {SpriteId::L_1, SpriteId::L_0},
                                     {SpriteId::L_2, SpriteId::L_1},
                                     {SpriteId::L_3, SpriteId::L_2}}};
    for (const auto& c : cw) {
        GameContext game;
        spaceField(game);
        setActive(game, c.from, kSpawnY, kSpawnX);
        game.joypad.pressed = actions({Action::RotateClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, c.to);
    }

    // Counter-clockwise: increment, with orientation 3 wrapping down to 0.
    const std::array<Case, 4> ccw = {{{SpriteId::L_0, SpriteId::L_1},
                                      {SpriteId::L_1, SpriteId::L_2},
                                      {SpriteId::L_2, SpriteId::L_3},
                                      {SpriteId::L_3, SpriteId::L_0}}};
    for (const auto& c : ccw) {
        GameContext game;
        spaceField(game);
        setActive(game, c.from, kSpawnY, kSpawnX);
        game.joypad.pressed = actions({Action::RotateCounterClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, c.to);
    }

    // B (CCW) is tested before A (CW): both pressed rotates counter-clockwise.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.joypad.pressed = actions({Action::RotateClockwise, Action::RotateCounterClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::L_1);
    }

    // Hidden guard: a hidden piece is untouched.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX, /*hidden=*/true);
        game.joypad.pressed = actions({Action::RotateClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::L_0);
    }

    // Successful rotation cues the rotate SFX (write).
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.joypad.pressed = actions({Action::RotateClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::L_3);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::ROTATE_PIECE);
    }

    // Colliding rotation reverts and cancels the cue (cancel). L_0 -> L_3 covers (0,5), which L_0
    // does not; a brick there blocks the rotation.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.field.board[0][5] = kBrick;
        game.joypad.pressed = actions({Action::RotateClockwise});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].spriteId, SpriteId::L_0);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
}

// 4. ShiftDasVectors — the DAS core at the shift site (tetris.asm:5965-6028): press fires + arms 23,
// a held countdown fires + reloads 9, a blocked shift parks the timer at 1 (wall charge), idle re-arms
// to 23, right-held blocks left, and a held path entered with a stale zero timer wraps to 255.
TEST(PieceSystem, ShiftDasVectors) {
    // Press → fire, move right, arm 23.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 0;
        game.joypad.held = actions({Action::MoveRight});
        game.joypad.pressed = actions({Action::MoveRight});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX + 0x08);
        EXPECT_EQ(game.flow.keyRepeatTimer, kKeyRepeatInitialDelay);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::SHIFT_PIECE);
    }

    // Held, timer expires → fire, reload 9.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 1;
        game.joypad.held = actions({Action::MoveRight});  // held, not freshly pressed
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX + 0x08);
        EXPECT_EQ(game.flow.keyRepeatTimer, kKeyRepeatRate);
    }

    // Held, timer not expired → no fire, just decrement.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 5;
        game.joypad.held = actions({Action::MoveRight});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX);
        EXPECT_EQ(game.flow.keyRepeatTimer, 4);
    }

    // Wall charge: a blocked shift reverts and parks the timer at 1. Right-shifted L_0 covers (1,8),
    // which L_0 does not; a brick there blocks the shift.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.field.board[1][8] = kBrick;
        game.flow.keyRepeatTimer = 0;
        game.joypad.held = actions({Action::MoveRight});
        game.joypad.pressed = actions({Action::MoveRight});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX);  // reverted
        EXPECT_EQ(game.flow.keyRepeatTimer, kKeyRepeatBlockedRetry);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }

    // Idle re-arm: no shift button active → timer reloaded to 23, piece unmoved.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 5;
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX);
        EXPECT_EQ(game.flow.keyRepeatTimer, kKeyRepeatInitialDelay);
    }

    // Stale-zero → 255: held path entered with the timer already at zero wraps to 255.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 0;
        game.joypad.held = actions({Action::MoveRight});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.flow.keyRepeatTimer, 255);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX);
    }

    // Right held blocks left: with both held and left freshly pressed, only right is processed (so the
    // piece does not move left and the timer takes the right branch's decrement).
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.keyRepeatTimer = 5;
        game.joypad.held = actions({Action::MoveRight, Action::MoveLeft});
        game.joypad.pressed = actions({Action::MoveLeft});
        rotateAndShiftPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].x, kSpawnX);  // no left move
        EXPECT_EQ(game.flow.keyRepeatTimer, 4);                             // right branch decrement
    }
}

// 5. DropGravityAndSoftDrop — the gravity timer (tetris.asm:5202-5219), the three soft-drop gates and
// the 3-frame cadence (:5177-5191), the post-lock latch and its fresh-press unlatch (:5167-5176), and
// the soft-drop counter's accumulation and the down-plus-direction cancel (:5195-5201).
TEST(PieceSystem, DropGravityAndSoftDrop) {
    // Gravity: drop timer decrements, no step.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.dropTimer = 5;
        dropPiece(game);
        EXPECT_EQ(game.flow.dropTimer, 4);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].y, kSpawnY);
        EXPECT_EQ(game.flow.softDropCounter, 0);
    }

    // Gravity: timer at zero reloads framesPerDrop and steps down.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.dropTimer = 0;
        game.flow.framesPerDrop = 20;
        dropPiece(game);
        EXPECT_EQ(game.flow.dropTimer, 20);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].y, kSpawnY + 0x08);
    }

    // Soft-drop gates: any of timer2 / lock stage / wipe counter non-zero returns with no step.
    for (int gate = 0; gate < 3; ++gate) {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.joypad.held = actions({Action::SoftDrop});
        if (gate == 0) game.flow.timer2 = 1;
        if (gate == 1) game.flow.pieceLockStage = 1;
        if (gate == 2) game.flow.wipeCounter = 1;
        dropPiece(game);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].y, kSpawnY) << "gate " << gate;
        EXPECT_EQ(game.flow.softDropCounter, 0) << "gate " << gate;
    }

    // Soft drop steps, sets the 3-frame cadence, and accumulates the counter across frames.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.joypad.held = actions({Action::SoftDrop});
        dropPiece(game);
        EXPECT_EQ(game.flow.timer2, 3);
        EXPECT_EQ(game.flow.softDropCounter, 1);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].y, kSpawnY + 0x08);
        game.flow.timer2 = 0;  // the dispatcher's timer law would clear it between frames
        dropPiece(game);
        EXPECT_EQ(game.flow.softDropCounter, 2);
        EXPECT_EQ(game.spriteRenderer.slots[kActivePieceSlot].y, kSpawnY + 0x10);
    }

    // Post-lock latch: with the latch set, a held-but-not-fresh Down does not soft drop (it falls to
    // gravity) and the latch stays set.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.engine.blockSoftDropAfterLock = true;
        game.flow.dropTimer = 5;
        game.joypad.held = actions({Action::SoftDrop});  // held, not freshly pressed
        dropPiece(game);
        EXPECT_EQ(game.flow.softDropCounter, 0);
        EXPECT_EQ(game.flow.dropTimer, 4);  // gravity ran
        EXPECT_TRUE(game.engine.blockSoftDropAfterLock);
        EXPECT_EQ(game.flow.timer2, 0);  // soft-drop cadence never set
    }

    // Post-lock unlatch: a fresh exactly-Down press clears the latch and soft-drops this frame.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.engine.blockSoftDropAfterLock = true;
        game.joypad.held = actions({Action::SoftDrop});
        game.joypad.pressed = actions({Action::SoftDrop});
        dropPiece(game);
        EXPECT_FALSE(game.engine.blockSoftDropAfterLock);
        EXPECT_EQ(game.flow.softDropCounter, 1);
        EXPECT_EQ(game.flow.timer2, 3);
    }

    // Down plus a direction is not a soft drop: gravity runs (counter stays zero, no 3-frame cadence).
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.dropTimer = 5;
        game.joypad.held = actions({Action::SoftDrop, Action::MoveRight});
        dropPiece(game);
        EXPECT_EQ(game.flow.softDropCounter, 0);
        EXPECT_EQ(game.flow.dropTimer, 4);
        EXPECT_EQ(game.flow.timer2, 0);
    }
}

// 6. LockAndTopout — the lock-stage guard and the four board writes (tetris.asm:6068-6106), the top-out
// counter law (:5261-5281), and the soft-drop award fork at lock (:5237-5260, :5283-5299).
TEST(PieceSystem, LockAndTopout) {
    // Lock guard: nothing happens unless a lock is in progress (stage 1).
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.pieceLockStage = 0;
        lockPieceIntoBackground(game);
        EXPECT_EQ(game.field.board[1][5], kSpace);  // unchanged
        EXPECT_EQ(game.flow.pieceLockStage, 0);
        EXPECT_FALSE(game.spriteRenderer.slots[kActivePieceSlot].hidden);
    }

    // Lock writes the four tiles into the board, advances the stage, and hides the piece.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.flow.pieceLockStage = 1;
        lockPieceIntoBackground(game);
        EXPECT_EQ(game.field.board[1][5], 0x84);
        EXPECT_EQ(game.field.board[1][6], 0x84);
        EXPECT_EQ(game.field.board[1][7], 0x84);
        EXPECT_EQ(game.field.board[2][5], 0x84);
        EXPECT_EQ(game.flow.pieceLockStage, 2);
        EXPECT_TRUE(game.spriteRenderer.slots[kActivePieceSlot].hidden);
    }

    // Top-out: a piece that locks at the spawn position increments the counter the first time and tops
    // the game out (game-over state + reset + game-over wave cue) the second time. A brick at (3,5)
    // blocks the gravity step from the spawn, forcing a lock without moving the piece.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, kSpawnY, kSpawnX);
        game.field.board[3][5] = kBrick;
        game.flow.framesPerDrop = 20;
        game.flow.topOutLockCount = 0;

        dropPiece(game);  // first lock at spawn
        EXPECT_EQ(game.flow.topOutLockCount, 1);
        EXPECT_EQ(game.flow.pieceLockStage, 1);
        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);
        EXPECT_EQ(game.audioCues.wave, WaveSfxId::NONE);
        EXPECT_FALSE(game.audioCues.resetRequested);

        game.flow.dropTimer = 0;  // let gravity step again
        dropPiece(game);  // second lock at spawn → top out
        EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME_OVER);
        EXPECT_TRUE(game.audioCues.resetRequested);
        EXPECT_EQ(game.audioCues.wave, WaveSfxId::GAME_OVER);
    }

    // Soft-drop award, Type A: the points go to the score (with the minus-one) and flag a redraw. A
    // brick at (4,5) blocks the step from y=0x20, away from the spawn row so top-out does not fire.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, 0x20, kSpawnX);
        game.field.board[4][5] = kBrick;
        game.flow.gameType = GameType::TYPE_A;
        game.engine.score = 100;
        game.flow.softDropCounter = 4;  // incremented to 5 by the soft-drop step
        game.joypad.held = actions({Action::SoftDrop});
        dropPiece(game);
        EXPECT_EQ(game.engine.score, 104u);  // 100 + softDropAward(5) == 100 + 4
        EXPECT_TRUE(game.engine.scoreRedrawRequested);
        EXPECT_EQ(game.flow.softDropCounter, 0);
        EXPECT_EQ(game.flow.pieceLockStage, 1);
    }

    // Soft-drop award, Type A saturation: the award clamps at the score ceiling.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, 0x20, kSpawnX);
        game.field.board[4][5] = kBrick;
        game.flow.gameType = GameType::TYPE_A;
        game.engine.score = kScoreSaturation;
        game.flow.softDropCounter = 4;
        game.joypad.held = actions({Action::SoftDrop});
        dropPiece(game);
        EXPECT_EQ(game.engine.score, kScoreSaturation);
    }

    // Soft-drop award, non-Type-A: the points accumulate in the separate soft-drop total, not the score.
    {
        GameContext game;
        spaceField(game);
        setActive(game, SpriteId::L_0, 0x20, kSpawnX);
        game.field.board[4][5] = kBrick;
        game.flow.gameType = GameType::TYPE_B;
        game.engine.score = 100;
        game.engine.softDropPoints = 10;
        game.flow.softDropCounter = 4;
        game.joypad.held = actions({Action::SoftDrop});
        dropPiece(game);
        EXPECT_EQ(game.engine.softDropPoints, 14);  // 10 + 4
        EXPECT_EQ(game.engine.score, 100u);
        EXPECT_FALSE(game.engine.scoreRedrawRequested);
    }
}

// 7. NextPieceVectors — the spawn setup and preview→active promotion (tetris.asm:5079-5089), the
// deterministic ring walk with index wrap and the staged-garbage bit-7 mark (:5096-5114), and the
// random path's OR-based rejection, third-draw auto-accept, and next-preview-only pipeline write
// (:5116-5163).
TEST(PieceSystem, NextPieceVectors) {
    // Random path: promotion, slot writes, next-preview pipeline, and drop-timer reload. Reference
    // kind is the promoted piece's kind (T, 0x18); candidate 0x04 adds a bit outside T, so it is
    // accepted on the first draw. The old next-preview (I_0, 0x08) becomes the visible preview.
    {
        GameContext game;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::T_0;  // 0x18
        game.flow.nextPreviewPiece = Piece{0x08};                               // I_0
        game.flow.framesPerDrop = 25;
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x04; };  // J kind
        nextPiece(game, draw);

        const auto& active = game.spriteRenderer.slots[kActivePieceSlot];
        EXPECT_EQ(active.spriteId, SpriteId::T_0);
        EXPECT_EQ(active.y, kSpawnY);
        EXPECT_EQ(active.x, kSpawnX);
        EXPECT_FALSE(active.hidden);
        EXPECT_EQ(game.spriteRenderer.slots[kPreviewPieceSlot].spriteId, SpriteId::I_0);  // 0x08
        EXPECT_EQ(game.flow.nextPreviewPiece, Piece{0x04});
        EXPECT_EQ(game.flow.dropTimer, 25);
        EXPECT_EQ(calls, 1);
    }

    // Random path rejection: reference kind L (0x00); a candidate of kind L is rejected; the loop
    // retries and accepts the second draw (J, 0x04).
    {
        GameContext game;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::L_0;  // 0x00
        game.flow.nextPreviewPiece = Piece{0x00};
        std::vector<std::uint8_t> seq = {0x00, 0x04};  // reject, accept
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { return seq[calls++]; };
        nextPiece(game, draw);
        EXPECT_EQ(calls, 2);
        EXPECT_EQ(game.flow.nextPreviewPiece, Piece{0x04});
    }

    // Random path auto-accept: three would-be-rejected candidates (all kind L); the third is accepted
    // unconditionally, so exactly three draws happen.
    {
        GameContext game;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::L_0;
        game.flow.nextPreviewPiece = Piece{0x00};
        std::vector<std::uint8_t> seq = {0x00, 0x00, 0x00};
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { return seq[calls++]; };
        nextPiece(game, draw);
        EXPECT_EQ(calls, 3);
        EXPECT_EQ(game.flow.nextPreviewPiece, Piece{0x00});  // the auto-accepted candidate
    }

    // Deterministic path (multiplayer): walk the piece list by index, wrap at 256, mark staged garbage
    // with bit 7, and never draw.
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::L_0;
        game.flow.nextPreviewPiece = Piece{0x08};
        game.engine.pieceList[255] = Piece{0x0C};  // O_0
        game.flow.numPiecesPlayed = 255;
        game.multiplayer.garbageRowsPending = 2;
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x00; };
        nextPiece(game, draw);
        EXPECT_EQ(game.spriteRenderer.slots[kPreviewPieceSlot].spriteId, SpriteId::O_0);  // 0x0C
        EXPECT_EQ(game.flow.numPiecesPlayed, 0);              // 255 + 1 wraps
        EXPECT_EQ(game.multiplayer.garbageRowsPending, 0x82);  // bit 7 set
        EXPECT_EQ(game.flow.nextPreviewPiece, Piece{0x08});   // untouched on the deterministic path
        EXPECT_EQ(calls, 0);
    }

    // Deterministic path, no staged garbage: the mark is skipped and the index advances normally.
    {
        GameContext game;
        game.demo.activeDemo = ActiveDemo::TYPE_A;  // demo also takes the deterministic path
        game.spriteRenderer.slots[kPreviewPieceSlot].spriteId = SpriteId::L_0;
        game.engine.pieceList[5] = Piece{0x14};  // Z_0
        game.flow.numPiecesPlayed = 5;
        game.multiplayer.garbageRowsPending = 0;
        int calls = 0;
        auto draw = [&]() -> std::uint8_t { ++calls; return 0x00; };
        nextPiece(game, draw);
        EXPECT_EQ(game.spriteRenderer.slots[kPreviewPieceSlot].spriteId, SpriteId::Z_0);  // 0x14
        EXPECT_EQ(game.flow.numPiecesPlayed, 6);
        EXPECT_EQ(game.multiplayer.garbageRowsPending, 0);
        EXPECT_EQ(calls, 0);
    }
}
