#include "systems/piece.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/piece.h>
#include <kirpich/sprite_id.h>

#include "data/scoring.h"             // softDropAward, kScoreSaturation
#include "data/sprites.h"             // Sprite, SpritePart, getSprite
#include "systems/sprite_renderer.h"  // spritePartPosition, renderActivePieceSprite

namespace kirpich::systems {

namespace {

// The action's bit index, for reading the joypad snapshot's action sets.
constexpr retropp::ActionId aid(Action a) { return retropp::actionId(a); }

// The board cell one part of the active piece covers. Two original mechanisms compose here:
//
//   * The renderer's position law (_RenderSprites, tetris.asm:6774-6823), including the carry that
//     leaks between its two position adds and the off-screen y a hidden piece takes. That law is
//     the renderer's, and it is used from here rather than restated: the original renders the piece
//     into the sprite buffer and reads that buffer back, so the position collision sees is by
//     construction the position the screen shows.
//   * The tile-lookup cell map (_LookupTile, tetris.asm:6558-6584, reading the board shadow the
//     collision/lock code reaches by adding $30 to the address high byte, tetris.asm:6044). The OAM
//     pixel maps to a board cell by removing the hardware sprite offsets and dividing by the 8-pixel
//     tile size.
//
// A hidden piece maps to row 29, so collision and locking read its real cells even while it is
// invisible. All arithmetic is 8-bit wraparound.
PieceCell cellForPart(const SpriteSlot& slot, const Sprite& sprite, const SpritePart& part) {
    const SpritePosition pos = spritePartPosition(slot, sprite, part);

    const std::uint8_t row = static_cast<std::uint8_t>(static_cast<std::uint8_t>(pos.y - 0x10) >> 3);
    const std::uint8_t col = static_cast<std::uint8_t>(static_cast<std::uint8_t>(pos.x - 0x08) >> 3);
    return PieceCell{.row = row, .col = col, .tile = part.tile};
}

// The X shift (tetris.asm:5965-6028), shared by the right and left directions. `fired` is the
// keyRepeatFire verdict for the chosen direction; `dx` is +8 (right) or -8 (left, as an 8-bit
// wraparound add). Moves the piece, cues the shift sound, and on a collision reverts, cancels the
// cue, and parks the repeat timer for an immediate retry next frame.
// The two directions render and cue in opposite orders (right at :5988-5990, left at :6022-6024 —
// the disassembly notes the inconsistency itself), but both do so between the move and the collision
// test, and the two writes touch nothing in common, so one order serves both.
void applyShift(GameContext& game, SpriteSlot& slot, std::uint8_t oldX, std::uint8_t dx, bool fired) {
    if (!fired) {
        return;
    }
    slot.x = static_cast<std::uint8_t>(slot.x + dx);
    game.audioCues.square = SquareSfxId::SHIFT_PIECE;
    renderActivePieceSprite(game);
    if (detectCollision(game)) {
        slot.x = oldX;
        game.audioCues.square = SquareSfxId::NONE;
        renderActivePieceSprite(game);
        game.flow.keyRepeatTimer = kKeyRepeatBlockedRetry;
    }
}

}  // namespace

BoundedVec<PieceCell, 4> activePieceCells(const GameContext& game) {
    const SpriteSlot& slot = game.spriteRenderer.slots[kActivePieceSlot];
    const Sprite& sprite = getSprite(slot.spriteId);
    // Each of the 28 piece-rotation sprites composes to exactly four parts with no skips, so the
    // active piece always covers four cells — matching the original's fixed four-slot collision and
    // lock loops (b = 4). Pinned by the geometry test.
    assert(sprite.parts.size() == 4 && "active piece sprite must compose to exactly four parts");
    return BoundedVec<PieceCell, 4>{
        cellForPart(slot, sprite, sprite.parts[0]),
        cellForPart(slot, sprite, sprite.parts[1]),
        cellForPart(slot, sprite, sprite.parts[2]),
        cellForPart(slot, sprite, sprite.parts[3]),
    };
}

bool detectCollision(const GameContext& game) {
    // tetris.asm:6030-6058. Every board byte but an empty space collides (the empty cell ties
    // CharTile::SPACE). The original's dead result-shadow write ($FF9B) is dropped; the return value
    // is the contract. The four-cell early-out on a zero OAM X is unreachable for pieces, so it too is
    // dropped.
    for (const PieceCell& cell : activePieceCells(game)) {
        if (game.field.board[cell.row][cell.col] != static_cast<std::uint8_t>(CharTile::SPACE)) {
            return true;
        }
    }
    return false;
}

void rotateAndShiftPiece(GameContext& game) {
    SpriteSlot& slot = game.spriteRenderer.slots[kActivePieceSlot];

    // Hidden guard (tetris.asm:5911-5914): a hidden active piece is not manipulated.
    if (slot.hidden) {
        return;
    }

    // --- Rotation (tetris.asm:5920-5963) --------------------------------------------------------
    // Counter-clockwise (B) is tested before clockwise (A). A rotation changes the sprite id, which
    // for a piece IS its rotation: the four orientations are four consecutive ids, so rotating is
    // stepping the id while wrapping within the piece's own four-id block.
    const retropp::ActionSet& pressed = game.joypad.pressed;
    const bool ccw = pressed.test(aid(Action::RotateCounterClockwise));
    const bool cw = pressed.test(aid(Action::RotateClockwise));
    if (ccw || cw) {
        const std::uint8_t before = static_cast<std::uint8_t>(slot.spriteId);
        std::uint8_t rotated;
        if (ccw) {
            // Increment; from orientation 3, clear the low two bits to wrap back to orientation 0.
            rotated = ((before & 0x03) == 0x03) ? static_cast<std::uint8_t>(before & ~0x03)
                                                : static_cast<std::uint8_t>(before + 1);
        } else {
            // Decrement; from orientation 0, set the low two bits to wrap up to orientation 3.
            rotated = ((before & 0x03) == 0x00) ? static_cast<std::uint8_t>(before | 0x03)
                                                : static_cast<std::uint8_t>(before - 1);
        }
        slot.spriteId = static_cast<SpriteId>(rotated);
        // Cue the rotate sound before the collision test; on a collision, revert the rotation and
        // cancel the cue in the same frame (tetris.asm:5952-5963).
        game.audioCues.square = SquareSfxId::ROTATE_PIECE;
        renderActivePieceSprite(game);
        if (detectCollision(game)) {
            game.audioCues.square = SquareSfxId::NONE;
            slot.spriteId = static_cast<SpriteId>(before);
            renderActivePieceSprite(game);
        }
    }

    // --- Shift + auto-repeat (tetris.asm:5965-6028) ---------------------------------------------
    // Right takes priority: a right press or hold short-circuits the left checks entirely. Each
    // direction's fire decision comes from the shared key-repeat core over the one repeat timer.
    const retropp::ActionSet& held = game.joypad.held;
    const std::uint8_t oldX = slot.x;

    const bool rightP = pressed.test(aid(Action::MoveRight));
    const bool rightH = held.test(aid(Action::MoveRight));
    if (rightP || rightH) {
        applyShift(game, slot, oldX, 0x08, keyRepeatFire(game.flow.keyRepeatTimer, rightP, rightH));
        return;
    }
    const bool leftP = pressed.test(aid(Action::MoveLeft));
    const bool leftH = held.test(aid(Action::MoveLeft));
    if (leftP || leftH) {
        applyShift(game, slot, oldX,
                   static_cast<std::uint8_t>(0x100 - 0x08),  // -8 as an 8-bit add
                   keyRepeatFire(game.flow.keyRepeatTimer, leftP, leftH));
        return;
    }

    // Idle re-arm (tetris.asm:6006-6011): with no shift button active, keep the repeat timer fully
    // armed so the next shift begins after the full initial delay.
    game.flow.keyRepeatTimer = kKeyRepeatInitialDelay;
}

void dropPiece(GameContext& game) {
    SpriteSlot& slot = game.spriteRenderer.slots[kActivePieceSlot];
    EngineState& engine = game.engine;
    GameFlowState& flow = game.flow;
    const retropp::ActionSet& held = game.joypad.held;
    const retropp::ActionSet& pressed = game.joypad.pressed;

    // The soft-drop branch runs when Down is held and neither Left nor Right is (tetris.asm:5195-5198;
    // Up is outside the tested mask and does not matter). Otherwise gravity runs.
    const bool downOnly = held.test(aid(Action::SoftDrop)) && !held.test(aid(Action::MoveLeft)) &&
                          !held.test(aid(Action::MoveRight));
    bool step = false;

    if (downOnly) {
        // DownHeld (tetris.asm:5167-5191).
        bool cancel = false;
        if (engine.blockSoftDropAfterLock) {
            // The post-lock latch: only a fresh press that is exactly Down among the three unlatches
            // it; anything else cancels the soft drop for this frame (tetris.asm:5171-5176).
            const bool freshDownOnly = pressed.test(aid(Action::SoftDrop)) &&
                                       !pressed.test(aid(Action::MoveLeft)) &&
                                       !pressed.test(aid(Action::MoveRight));
            if (freshDownOnly) {
                engine.blockSoftDropAfterLock = false;
            } else {
                cancel = true;
            }
        }
        if (!cancel) {
            // trySoftDrop (tetris.asm:5177-5191): three gates, then the 3-frame soft-drop cadence.
            // All three gates leave through the shared exit that redraws the piece (:5208).
            if (flow.timer2 != 0 || flow.pieceLockStage != 0 || flow.wipeCounter != 0) {
                renderActivePieceSprite(game);
                return;
            }
            flow.timer2 = 3;
            flow.softDropCounter++;
            step = true;
        }
    }

    if (!step) {
        // cancelSoftDrop / gravity (tetris.asm:5199-5219).
        flow.softDropCounter = 0;
        if (flow.dropTimer != 0) {
            flow.dropTimer--;
            renderActivePieceSprite(game);  // the same shared exit (:5207-5209)
            return;
        }
        // These two gates return without redrawing (:5214, :5217) — a stack that is still falling
        // leaves the piece's entry exactly as the last frame left it.
        if (flow.pieceLockStage == 3 || flow.wipeCounter != 0) {
            return;
        }
        flow.dropTimer = flow.framesPerDrop;
    }

    // The step (tetris.asm:5220-5236): move down one row and test. No collision ends the frame.
    const std::uint8_t oldY = slot.y;
    slot.y = static_cast<std::uint8_t>(slot.y + 0x08);
    renderActivePieceSprite(game);
    if (!detectCollision(game)) {
        return;
    }
    // Collision: revert, begin the lock, and latch out soft drop until the next fresh press.
    slot.y = oldY;
    renderActivePieceSprite(game);
    flow.pieceLockStage = 1;
    engine.blockSoftDropAfterLock = true;

    // Soft-drop award (tetris.asm:5237-5260, 5283-5299). Type A and Type C add the points to the score
    // (saturating at the score ceiling) and flag a redraw; every other mode accumulates them in the
    // separate soft-drop total the scoreboard tallies later. The award is one point per soft-dropped
    // row minus one — the original's own off-by-one, held in softDropAward.
    if (flow.softDropCounter != 0) {
        const std::uint8_t counter = flow.softDropCounter;
        if (flow.gameType != GameType::TYPE_B) {
            engine.score = std::min<std::uint32_t>(engine.score + softDropAward(counter),
                                                   kScoreSaturation);
            flow.scorePrintFlag = 1;  // AddBCD marks the score changed (:187-188, called at :5296)
            engine.scoreRedrawRequested = true;
        } else {
            engine.softDropPoints =
                static_cast<std::uint16_t>(engine.softDropPoints + softDropAward(counter));
        }
        flow.softDropCounter = 0;
    }

    // Top-out (tetris.asm:5261-5281): a piece that locks at the spawn position tops the game out on
    // the second such lock. The counter shares its byte with the top-score pointer; the two uses are
    // disjoint in time.
    if (slot.y == 0x18 && slot.x == 0x3F) {
        if (flow.topOutLockCount == 1) {
            game.audioCues.resetRequested = true;  // re-init the driver (InitAudio, tetris.asm:5272)
            flow.gameState = GameState::INIT_GAME_OVER;
            game.audioCues.wave = WaveSfxId::GAME_OVER;
        } else {
            flow.topOutLockCount++;
        }
    }
}

void lockPieceIntoBackground(GameContext& game) {
    GameFlowState& flow = game.flow;
    // Guard (tetris.asm:6069-6071): only run while the lock is at its first stage (drop set it).
    if (flow.pieceLockStage != 1) {
        return;
    }
    // Write each covered cell's tile into the board and into the displayed map. The original writes
    // both — the board, then the same tile into video memory under an HBlank wait
    // (tetris.asm:6072-6098) — because a locked piece has to appear at once rather than waiting for a
    // wipe to carry it across. The wait itself is hardware timing and is not carried.
    for (const PieceCell& cell : activePieceCells(game)) {
        game.field.board[cell.row][cell.col] = cell.tile;
        game.display.map[cell.row][cell.col] = cell.tile;
    }
    flow.pieceLockStage = 2;
    game.spriteRenderer.slots[kActivePieceSlot].hidden = true;
}

void nextPiece(GameContext& game, const std::function<std::uint8_t()>& draw) {
    SpriteSlot& active = game.spriteRenderer.slots[kActivePieceSlot];
    SpriteSlot& preview = game.spriteRenderer.slots[kPreviewPieceSlot];
    GameFlowState& flow = game.flow;
    EngineState& engine = game.engine;

    // Reset the active slot and promote the preview into it (tetris.asm:5079-5089). The rejection
    // reference is the promoted piece's kind.
    const std::uint8_t promoted = static_cast<std::uint8_t>(preview.spriteId);
    active.hidden = false;
    active.y = 0x18;
    active.x = 0x3F;
    active.spriteId = static_cast<SpriteId>(promoted);
    const std::uint8_t referenceKind = static_cast<std::uint8_t>(promoted & ~0x03);

    std::uint8_t newPreview;
    if (game.demo.activeDemo != ActiveDemo::NONE || game.multiplayer.isMultiplayer) {
        // Deterministic path (tetris.asm:5096-5114): walk the shared piece list by index, which wraps
        // at 256. If received garbage is staged, mark it to apply at this piece (bit 7).
        newPreview = engine.pieceList[flow.numPiecesPlayed].raw;
        flow.numPiecesPlayed = static_cast<std::uint8_t>(flow.numPiecesPlayed + 1);
        if (game.multiplayer.garbageRowsPending != 0) {
            game.multiplayer.garbageRowsPending =
                static_cast<std::uint8_t>(game.multiplayer.garbageRowsPending | 0x80);
        }
    } else {
        // Random path (tetris.asm:5116-5157): up to three candidates; reject one whose kind, OR-ed
        // with the next-preview and the reference kind, adds no bit outside the reference kind; the
        // third draw is accepted unconditionally. The accepted candidate becomes the next preview,
        // and the piece promoted into the visible preview slot is the OLD next-preview (a one-stage
        // pipeline). The next-preview does not change during the loop, so it is read once.
        std::uint8_t cand = 0;
        for (int tries = 3;;) {
            cand = draw();
            --tries;
            if (tries == 0) {
                break;  // third draw: auto-accept
            }
            const std::uint8_t masked = static_cast<std::uint8_t>(
                (static_cast<std::uint8_t>(flow.nextPreviewPiece.raw) | cand | referenceKind) & ~0x03);
            if (masked != referenceKind) {
                break;  // accept
            }
        }
        newPreview = static_cast<std::uint8_t>(flow.nextPreviewPiece.raw);  // old next-preview
        flow.nextPreviewPiece = Piece{cand};
    }

    // Set the visible preview and reload the drop timer (tetris.asm:5158-5163).
    preview.spriteId = static_cast<SpriteId>(newPreview);
    renderPreviewPieceSprite(game);
    flow.dropTimer = flow.framesPerDrop;
}

}  // namespace kirpich::systems
