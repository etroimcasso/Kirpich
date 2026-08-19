#include "systems/gameplay.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/serial_role.h>
#include <kirpich/sprite_id.h>

#include "data/gravity.h"    // framesPerDrop
#include "data/music.h"      // MusicId
#include "data/playing_field.h"
#include "data/scene_sprites.h"  // activePieceSprite, previewPieceSprite
#include "data/scoring.h"    // rocketSpriteForScore
#include "data/tilemaps.h"   // kGameOverTilemap, kTryAgainTilemap, the two gameplay backdrops
#include "retropp/input.h"   // actionId
#include "state/playing_field_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/line_clear.h"    // checkForCompletedRows, moveBlocksDownAfterLineClear, clearLineClearsList
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/piece.h"         // rotateAndShiftPiece, dropPiece, lockPieceIntoBackground, nextPiece
#include "systems/screen.h"        // loadScreenTilemap
#include "systems/scoring.h"          // addLineClearScore, clearScoreAndStats
#include "systems/sprite_renderer.h"  // renderActivePieceSprite, renderPreviewPieceSprite

namespace kirpich::systems {

namespace {

// The tile the game-over curtain fills the field with. No tetromino uses it, which is what makes the
// curtain read as something other than an ordinary field clear.
constexpr std::uint8_t kGameOverCurtainTile = 0x87;

// Where the wipe animation starts when a field fill arms it.
constexpr std::uint8_t kWipeStartStep = 2;

// Frame counts: the curtain hold before the game-over screen paints (70 frames, about 1 1/6 seconds),
// the two-player hold before the end jingle (63), the bonus-ending hold before the scene starts (144,
// about 2.4 seconds), and the Type B results count-up interval (5).
constexpr std::uint8_t kGameOverCurtainFrames = 70;
constexpr std::uint8_t kMultiplayerGameOverFrames = 63;
constexpr std::uint8_t kBonusEndingFrames = 144;
constexpr std::uint8_t kScoreboardStepFrames = 5;

// The line count a Type B round starts from and counts down to zero.
constexpr std::uint16_t kTypeBStartingLines = 25;

// The drop period a Type B round starts on, before the gravity table takes over.
constexpr std::uint8_t kTypeBInitialDropTimer = 52;

// The count-up phase the Type B results screen advances into on each interval.
constexpr std::uint8_t kScoreboardTallyRunning = 1;

// The serial byte the two-player game-over path flags before the end jingle.
constexpr std::uint8_t kMultiplayerGameOverFlag = 0x1B;

// The command the master sends to bring both sides out of a pause.
constexpr std::uint8_t kUnpauseCommand = 0x94;

// The hidden marker a sprite slot's status byte carries. Slots model it as a bool; the piece sprites
// are hidden by writing it and shown by clearing it.
constexpr bool kHidden = true;

// Where the two rows below the visible field sit on the board (Call_1FF2, tetris.asm:5061-5075: it
// clears ten cells on each of the last two board rows, at the field's own left edge).
constexpr std::size_t kBelowFieldFirstRow = 30;
constexpr std::size_t kBelowFieldRowCount = 2;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

bool held(const GameContext& game, Action action) {
    return game.joypad.held.test(retropp::actionId(action));
}

// The Start+Select+B+A chord (tetris.asm:4441-4444). Held levels, not edges, exactly as the frame
// dispatcher tests it.
bool softResetChordHeld(const GameContext& game) {
    return held(game, Action::Start) && held(game, Action::Select) &&
           held(game, Action::RotateCounterClockwise) && held(game, Action::RotateClockwise);
}

// Call_1FF2 (tetris.asm:5061-5075): clear the two board rows below the visible field.
void clearBelowFieldRows(PlayingFieldState& field) {
    const auto empty = static_cast<std::uint8_t>(CharTile::SPACE);
    for (std::size_t i = 0; i < kBelowFieldRowCount; ++i) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            field.board[kBelowFieldFirstRow + i][kPlayingFieldOriginCol + col] = empty;
        }
    }
}

// Call_1F7D (tetris.asm:4973-4990): print a window block into the board, one column of eight cells at
// a time, starting at the given field cell.
void printWindowBlock(PlayingFieldState& field, std::size_t fieldRow, std::size_t fieldCol,
                      const auto& block) {
    for (std::size_t row = 0; row < block.size(); ++row) {
        for (std::size_t col = 0; col < block[row].size(); ++col) {
            field.fieldCell(fieldRow + row, fieldCol + col) = block[row][col];
        }
    }
}

// CopyUntilFF (tetris.asm:6267-6276) applied to one descriptor. The round's init seeds both piece
// slots from their stored templates (:4176-4181), and that is where the two pieces get their screen
// positions and attributes: the preview's position is the only thing that ever puts it in its box,
// and the pieces themselves only ever change which sprite they show. Without it a round inherits
// whatever the previous screen left in the slots.
void loadSpriteTemplate(SpriteSlot& dst, const SceneSprite& src) {
    dst.hidden = src.hidden;
    dst.y = src.y;
    dst.x = src.x;
    dst.spriteId = src.sprite;
    dst.behindBg = src.behindBg;
    dst.yflip = false;
    dst.xflip = src.xflip;
}

// Show or hide both piece sprites at once (the pause and game-over paths write both status bytes).
void setPieceSpritesHidden(SpriteRendererState& renderer, bool hidden) {
    renderer.slots[kActivePieceSlot].hidden = hidden;
    renderer.slots[kPreviewPieceSlot].hidden = hidden;
}

// HandlePausedMultiplayer.unpause (tetris.asm:4538-4546): restore the serial buffers saved across the
// pause, resume the music, and clear the pause flag. Both sides reach it — the master directly when it
// presses Start again, the slave when it reads the master's unpause command off the wire.
void unpauseMultiplayer(GameContext& game) {
    game.multiplayer.rx = game.multiplayer.savedRx;
    game.multiplayer.tx = game.multiplayer.savedTx;
    game.audioCues.pause = AudioPauseCommand::UNPAUSE;
    game.flow.paused = false;
}

// handleSelect (tetris.asm:4423-4438): toggle the preview piece, and hide or show its sprite to match.
void handleSelect(GameContext& game) {
    if (!pressed(game, Action::Select)) {
        return;
    }
    game.engine.hidePreviewPiece = !game.engine.hidePreviewPiece;
    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = game.engine.hidePreviewPiece;
    renderPreviewPieceSprite(game);  // (:4433) — the toggle is only visible once it is redrawn
}

}  // namespace

void fillPlayingFieldAndWipe(GameContext& game, std::uint8_t fill) {
    // The wipe is armed before the fill and the two are inseparable here: the init disarms it on the
    // next line, the two game-over callers let it run (tetris.asm:5039-5043).
    game.flow.wipeCounter = kWipeStartStep;
    for (std::size_t row = 0; row < kPlayingFieldRows; ++row) {
        for (std::size_t col = 0; col < kPlayingFieldCols; ++col) {
            game.field.fieldCell(row, col) = fill;
        }
    }
}

void initGame(GameContext& game, const std::function<std::uint8_t()>& draw,
              const InitGarbageHook& initGarbage) {
    // Clear the entry block (tetris.asm:4126-4132). The lock-stage shadow at $FF9B is written by the
    // sprite path and never read, so it has no field here; the line count clears whole because the
    // fork below rewrites it either way.
    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = false;
    game.flow.pieceLockStage = 0;
    game.flow.blinkCounter = 0;
    game.flow.topOutLockCount = 0;
    game.flow.lines = 0;

    fillPlayingFieldAndWipe(game, static_cast<std::uint8_t>(CharTile::SPACE));
    clearBelowFieldRows(game.field);
    clearScoreAndStats(game);

    // The fill armed the wipe; this round starts with a clean field, not a wipe (tetris.asm:4137-4138).
    game.flow.wipeCounter = 0;

    clearOamObjects(game);

    const bool typeB = game.flow.gameType == GameType::TYPE_B;
    game.flow.level = typeB ? game.flow.typeBLevel : game.flow.typeALevel;
    game.flow.lines = typeB ? kTypeBStartingLines : 0;

    // The round's backdrop, chosen by game type in the same fork (tetris.asm:4141/:4148, loaded at
    // :4154). It lands after the space-fill above, exactly as the original orders them, and its own
    // columns supply the field's walls and the stats panel; columns 2-11 are spaces, so the field it
    // lays out is empty. The starting garbage below writes over it, which is why that runs later.
    //
    // No art load here: the screen this is entered from loaded the gameplay set already, and the
    // original does not reload it. The second copy of this backdrop the original writes to its other
    // background map (:4155-4157) is the paused screen, which the port does not draw — see
    // docs/contracts/screen.md.
    loadScreenTilemap(game.display, typeB ? kTypeBGameplayTilemap : kTypeAGameplayTilemap);

    game.flow.framesPerDrop = framesPerDrop(game.flow.level, game.flow.heartMode != 0);
    game.flow.dropTimer = game.flow.framesPerDrop;

    // Both piece descriptors come from their templates (:4176-4181), before the preview's own
    // visibility is applied over the top of it.
    loadSpriteTemplate(game.spriteRenderer.slots[kActivePieceSlot], activePieceSprite());
    loadSpriteTemplate(game.spriteRenderer.slots[kPreviewPieceSlot], previewPieceSprite());

    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = game.engine.hidePreviewPiece;

    // Three draws fill the one-stage pipeline: each returns the previous preview, so it takes three to
    // leave both the active piece and the visible preview valid (tetris.asm:4203-4205).
    nextPiece(game, draw);
    nextPiece(game, draw);
    nextPiece(game, draw);
    renderActivePieceSprite(game);  // (:4206) — the draws leave the preview drawn; this adds the piece

    game.flow.completedRowCount = 0;

    if (typeB) {
        game.flow.dropTimer = kTypeBInitialDropTimer;
        if (game.flow.typeBStartHeight != 0 && initGarbage) {
            initGarbage(game, game.flow.typeBStartHeight, game.demo.activeDemo != ActiveDemo::NONE);
        }
    }

    game.flow.gameState = GameState::NORMAL_GAMEPLAY;
}

void normalGameplay(GameContext& game, const GameplayDemoHooks& demo, const SoftResetHook& softReset) {
    handleStartSelect(game, softReset);
    if (game.flow.paused) {
        return;
    }

    if (demo.checkForEndOfDemo) {
        demo.checkForEndOfDemo(game);
    }
    if (demo.simulateJoypad) {
        demo.simulateJoypad(game);
    }
    if (demo.recordDemo) {
        demo.recordDemo(game);
    }

    // The order is the contract: the scan runs before the lock, so a piece is scanned in the position
    // it had on entry; the compaction runs before the award, so the award sees its tallies.
    rotateAndShiftPiece(game);
    dropPiece(game);
    checkForCompletedRows(game);
    lockPieceIntoBackground(game);
    moveBlocksDownAfterLineClear(game);
    addLineClearScore(game);

    if (demo.restoreSavedJoypad) {
        demo.restoreSavedJoypad(game);
    }
}

void handleStartSelect(GameContext& game, const SoftResetHook& softReset) {
    // The frame dispatcher tests this same chord each tick and this tests it again — the original does
    // both (tetris.asm:4441-4444).
    if (softResetChordHeld(game)) {
        if (softReset) {
            softReset();
        }
        return;
    }

    // Recorded input cannot pause the game (tetris.asm:4445-4447).
    if (game.demo.activeDemo != ActiveDemo::NONE) {
        return;
    }

    if (!pressed(game, Action::Start)) {
        handleSelect(game);
        return;
    }

    if (game.multiplayer.isMultiplayer) {
        // Only the master may pause or unpause (tetris.asm:4497-4499).
        if (game.multiplayer.role != SerialRole::MASTER) {
            return;
        }
        game.flow.paused = !game.flow.paused;
        if (!game.flow.paused) {
            // The master jumps straight into the shared unpause, skipping the protocol wait
            // (tetris.asm:4500-4503).
            unpauseMultiplayer(game);
            return;
        }
        game.audioCues.pause = AudioPauseCommand::PAUSE;
        game.multiplayer.savedRx = game.multiplayer.rx;
        game.multiplayer.savedTx = game.multiplayer.tx;
        return;
    }

    game.flow.paused = !game.flow.paused;
    if (game.flow.paused) {
        game.audioCues.pause = AudioPauseCommand::PAUSE;
        setPieceSpritesHidden(game.spriteRenderer, kHidden);
        // Both branches leave through the same pair of redraws (:4482-4483), which is what makes
        // the status change visible.
        renderActivePieceSprite(game);
        renderPreviewPieceSprite(game);
        return;
    }

    game.audioCues.pause = AudioPauseCommand::UNPAUSE;
    game.spriteRenderer.slots[kActivePieceSlot].hidden = false;
    // The preview comes back only if the player has not hidden it (tetris.asm:4490-4494).
    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = game.engine.hidePreviewPiece;
    renderActivePieceSprite(game);
    renderPreviewPieceSprite(game);
}

bool handlePausedMultiplayer(GameContext& game) {
    if (!game.flow.paused) {
        return false;
    }

    // The original loads the serial-transfer flag and branches on zero (tetris.asm:4519-4520) — but the
    // load leaves the condition flags alone, so the branch tests the pause check two lines above, which
    // already returned when it was zero. The branch can therefore never be taken and the routine always
    // proceeds regardless of the flag. Preserved as written.
    game.multiplayer.transferCompleted = 0;

    if (game.multiplayer.role == SerialRole::MASTER) {
        game.multiplayer.tx = kUnpauseCommand;
        game.multiplayer.sendPending = kUnpauseCommand;
        return true;
    }

    game.multiplayer.tx = 0;
    if (game.multiplayer.rx == kUnpauseCommand) {
        return true;
    }

    unpauseMultiplayer(game);
    return false;
}

void initGameOver(GameContext& game) {
    setPieceSpritesHidden(game.spriteRenderer, kHidden);
    renderActivePieceSprite(game);   // (:4581) — the curtain falls over an emptied object layer
    renderPreviewPieceSprite(game);  // (:4582)
    game.flow.pieceLockStage = 0;
    game.flow.blinkCounter = 0;
    clearLineClearsList(game);
    fillPlayingFieldAndWipe(game, kGameOverCurtainTile);
    game.flow.timer1 = kGameOverCurtainFrames;
    game.flow.gameState = GameState::GAME_OVER_CURTAIN;
}

void gameOverCurtain(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.audioCues.music = MusicId::GAME_OVER;

    if (game.multiplayer.isMultiplayer) {
        game.flow.timer1 = kMultiplayerGameOverFrames;
        game.multiplayer.transferCompleted = kMultiplayerGameOverFlag;
        game.flow.gameState = GameState::TWO_PLAYER_END_JINGLE;
        return;
    }

    fillPlayingFieldAndWipe(game, static_cast<std::uint8_t>(CharTile::SPACE));
    printWindowBlock(game.field, 2, 1, kGameOverTilemap);
    printWindowBlock(game.field, 12, 1, kTryAgainTilemap);

    // Only a Type A round earns a rocket ending (tetris.asm:4943-4945).
    if (game.flow.gameType == GameType::TYPE_A) {
        if (const std::optional<SpriteId> rocket = rocketSpriteForScore(game.engine.score)) {
            game.flow.rocketSpriteIndex = *rocket;
            game.flow.timer1 = kBonusEndingFrames;
            game.flow.gameState = GameState::GAME_OVER_TO_BONUS;
            return;
        }
    }

    game.flow.gameState = GameState::GAME_OVER_SCREEN;
}

void gameOverScreen(GameContext& game) {
    if (!pressed(game, Action::RotateClockwise) && !pressed(game, Action::Start)) {
        return;
    }
    game.flow.wipeCounter = 0;

    if (game.multiplayer.isMultiplayer) {
        game.flow.gameState = GameState::INIT_2P_DIFFICULTY;
        return;
    }
    game.flow.gameState = game.flow.gameType == GameType::TYPE_A
                              ? GameState::INIT_TYPE_A_DIFFICULTY
                              : GameState::INIT_TYPE_B_DIFFICULTY;
}

void initTypeBScoreboard(GameContext& game) {
    if (game.flow.timer1 != 0) {
        return;
    }
    game.engine.scoreboardTallyPhase = kScoreboardTallyRunning;
    game.flow.timer1 = kScoreboardStepFrames;
}

void state0CUnknown(GameContext& game) {
    // Any newly-pressed input advances (tetris.asm:4910-4914). Every physical button binds to at least
    // one action, so a non-empty pressed set matches the original's non-zero test.
    if (game.joypad.pressed.bits() == 0) {
        return;
    }
    game.flow.gameState = GameState::BURAN_LIFTOFF;
}

void installGameplayHandlers(GameStateDispatcher& dispatcher, GameplayWiring wiring) {
    dispatcher.setHandler(GameState::INIT_GAME,
                          [wiring](GameContext& g) { initGame(g, wiring.draw, wiring.initGarbage); });
    dispatcher.setHandler(GameState::NORMAL_GAMEPLAY, [wiring](GameContext& g) {
        normalGameplay(g, wiring.demo, wiring.softReset);
    });
    dispatcher.setHandler(GameState::INIT_GAME_OVER, initGameOver);
    dispatcher.setHandler(GameState::GAME_OVER_CURTAIN, gameOverCurtain);
    dispatcher.setHandler(GameState::GAME_OVER_SCREEN, gameOverScreen);
    dispatcher.setHandler(GameState::INIT_TYPE_B_SCOREBOARD, initTypeBScoreboard);
    dispatcher.setHandler(GameState::STATE_0C_UNKNOWN, state0CUnknown);
}

}  // namespace kirpich::systems
