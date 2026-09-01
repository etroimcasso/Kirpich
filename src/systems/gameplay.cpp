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
#include "data/tilemaps.h"        // kGameOverTilemap, kTryAgainTilemap, the stored gameplay backdrops
#include "data/type_c_tilemap.h"  // kTypeCGameplayTilemap
#include "retropp/input.h"   // actionId
#include "state/playing_field_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/line_clear.h"    // checkForCompletedRows, moveBlocksDownAfterLineClear, clearLineClearsList
#include "systems/stats.h"         // beginRound, endRound, pauseRound, resumeRound
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/piece.h"         // rotateAndShiftPiece, dropPiece, lockPieceIntoBackground, nextPiece
#include "systems/readouts.h"      // printLevel, printLinesSeed, printStartHeight, printRise, copyLinesToSecondMap
#include "systems/rising_floor.h"  // armRiseCounter
#include "systems/screen.h"        // loadScreenTilemap
#include "systems/scoring.h"          // addLineClearScore, clearScoreAndStats
#include "systems/settings_screen.h"  // openSettings
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
// The clock, or nothing. A build that wires no clock still records every round; each one is simply
// zero seconds long.
std::uint64_t nowFrom(const NowNanos& now) { return now ? now() : 0; }

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

// The same block print into a background map rather than the board. The round init stamps the pause
// message into the second map this way (tetris.asm:4158-4161).
void printWindowBlockToMap(BackgroundMap& map, std::size_t row, std::size_t col, const auto& block) {
    for (std::size_t r = 0; r < block.size(); ++r) {
        for (std::size_t c = 0; c < block[r].size(); ++c) {
            map[row + r][col + c] = block[r][c];
        }
    }
}

// Where the pause message sits in the second map ($9C63).
constexpr std::size_t kPauseMessageRow = 3;
constexpr std::size_t kPauseMessageCol = 3;

// The port's own hint on the paused screen: the field area, which that screen leaves blank and where
// a paused player is already looking. It says which button opens the settings screen, because the
// cartridge has no such screen and so leaves no habit to lean on. It borrows the wording and the
// centring of the message above it ("hit start to continue game"), and names the button the way the
// game names its buttons rather than the way any one keyboard spells it.
constexpr std::size_t kSettingsHintRow      = 14;
constexpr std::size_t kSettingsHintButtonCol = 4;  // "hit a" centred over the message's own columns
constexpr std::size_t kSettingsHintWordCol   = 3;  // "settings"

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
              const InitGarbageHook& initGarbage, const NowNanos& now) {
    // Before anything is cleared: a round left open by an abandonment closes here, with the score it
    // actually earned, and this round latches the combination it is being played at. An attract demo
    // reaches this too, and beginRound is what turns it away.
    beginRound(game, nowFrom(now));

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

    const GameType type = game.flow.gameType;
    const bool typeB = type == GameType::TYPE_B;

    // The starting level is whichever one this mode's own difficulty screen chose.
    switch (type) {
        case GameType::TYPE_B: game.flow.level = game.flow.typeBLevel; break;
        case GameType::TYPE_C: game.flow.level = game.flow.typeCLevel; break;
        default:               game.flow.level = game.flow.typeALevel; break;
    }

    // Type B counts a fixed set of lines down to zero; the marathons count theirs up from none.
    game.flow.lines = typeB ? kTypeBStartingLines : 0;

    // The round's backdrop, chosen by game type in the same fork (tetris.asm:4141/:4148, loaded at
    // :4154). It lands after the space-fill above, exactly as the original orders them, and its own
    // columns supply the field's walls and the stats panel; columns 2-11 are spaces, so the field it
    // lays out is empty. The starting garbage below writes over it, which is why that runs later.
    //
    // No art load here: the screen this is entered from loaded the gameplay set already, and the
    // original does not reload it.
    const ScreenTilemap& backdrop = typeB                    ? kTypeBGameplayTilemap
                                  : type == GameType::TYPE_C ? kTypeCGameplayTilemap
                                                             : kTypeAGameplayTilemap;
    loadScreenTilemap(game.display.map, backdrop);

    // The same backdrop goes into the second map, with the pause message stamped over it (:4155-4161).
    // That map is the paused screen: the same panel, no playing field, and a PAUSE label.
    loadScreenTilemap(game.display.secondMap, backdrop);
    printWindowBlockToMap(game.display.secondMap, kPauseMessageRow, kPauseMessageCol,
                          kPauseMessageTilemap);
    writeMapText(game.display.secondMap, kSettingsHintRow, kSettingsHintButtonCol, "hit a");
    writeMapText(game.display.secondMap, kSettingsHintRow + 1, kSettingsHintWordCol, "settings");

    // The level, into the cell its game type uses, in both maps (:4162-4175).
    printLevel(game);

    game.flow.framesPerDrop = framesPerDrop(game.flow.level, game.flow.heartMode != 0);
    game.flow.dropTimer = game.flow.framesPerDrop;

    // Both piece descriptors come from their templates (:4176-4181), before the preview's own
    // visibility is applied over the top of it.
    loadSpriteTemplate(game.spriteRenderer.slots[kActivePieceSlot], activePieceSprite());
    loadSpriteTemplate(game.spriteRenderer.slots[kPreviewPieceSlot], previewPieceSprite());

    // The opening line count, into the live map (:4183-4194).
    printLinesSeed(game);

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
        // The starting height, under the panel's HIGH label, in both maps (:4214-4218).
        printStartHeight(game);
        if (game.flow.typeBStartHeight != 0 && initGarbage) {
            initGarbage(game, game.flow.typeBStartHeight, game.demo.activeDemo != ActiveDemo::NONE);
        }
    } else if (type == GameType::TYPE_C) {
        // A Type C round starts on an empty field - the floor comes to the player rather than being
        // there to begin with - so there is no garbage to lay.
        //
        // The counter is armed here, after the three pipeline draws above, so that filling the
        // pipeline does not spend part of the player's first interval. Both maps get the count: the
        // paused screen carries the panel too, and an unwritten cell would read as a blank where the
        // number belongs.
        armRiseCounter(game);
        printRise(game, game.display.map);
        printRise(game, game.display.secondMap);
    }

    game.flow.gameState = GameState::NORMAL_GAMEPLAY;
}

void normalGameplay(GameContext& game, const GameplayDemoHooks& demo, const SoftResetHook& softReset,
                    const NowNanos& now) {
    // A matched soft-reset chord ends the frame here. The original reaches its reset with a jump
    // (tetris.asm:4444) and that reset falls into the top of the main loop, so the rest of this frame —
    // the demo substitution, the piece, the scan, the lock, the compaction, the award — never runs.
    // Continuing would step a piece across the board the reset has just cleared.
    if (!handleStartSelect(game, softReset, now)) {
        return;
    }
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

bool handleStartSelect(GameContext& game, const SoftResetHook& softReset, const NowNanos& now) {
    // The frame dispatcher tests this same chord each tick and this tests it again — the original does
    // both (tetris.asm:4441-4444).
    //
    // The original's second test is a jump into the reset, not a call, so it abandons the rest of the
    // frame as well as the rest of this routine; false is how that reaches the caller.
    if (softResetChordHeld(game)) {
        if (softReset) {
            softReset();
        }
        return false;
    }

    // Recorded input cannot pause the game (tetris.asm:4445-4447).
    if (game.demo.activeDemo != ActiveDemo::NONE) {
        return true;
    }

    // A opens the settings screen from a paused round - the port's own screen, which the cartridge
    // has no counterpart for. The button is free here because rotation does not run while paused.
    // The frame continues: the pause check below this routine ends it anyway, and the screen's own
    // init runs on the next tick.
    if (game.flow.paused && pressed(game, Action::Confirm)) {
        openSettings(game);
        return true;
    }

    if (!pressed(game, Action::Start)) {
        handleSelect(game);
        return true;
    }

    if (game.multiplayer.isMultiplayer) {
        // Only the master may pause or unpause (tetris.asm:4497-4499).
        if (game.multiplayer.role != SerialRole::MASTER) {
            return true;
        }
        game.flow.paused = !game.flow.paused;
        if (!game.flow.paused) {
            // The master jumps straight into the shared unpause, skipping the protocol wait
            // (tetris.asm:4500-4503).
            resumeRound(game, nowFrom(now));
            unpauseMultiplayer(game);
            return true;
        }
        pauseRound(game, nowFrom(now));
        game.audioCues.pause = AudioPauseCommand::PAUSE;
        game.multiplayer.savedRx = game.multiplayer.rx;
        game.multiplayer.savedTx = game.multiplayer.tx;
        return true;
    }

    game.flow.paused = !game.flow.paused;
    if (game.flow.paused) {
        // Show the second map: the panel, no field, and the PAUSE message (tetris.asm:4461).
        game.display.displayed = DisplayedMap::SECOND;
        // Paused time is not played time, and a screen opened from the pause is on the far side of
        // this too: the round is already banked before that screen can be reached.
        pauseRound(game, nowFrom(now));
        game.audioCues.pause = AudioPauseCommand::PAUSE;
        // The line count reaches the second map only here, which is why the paused screen shows the
        // count as it stands now rather than as it stood at the last clear (:4464-4476).
        copyLinesToSecondMap(game);
        setPieceSpritesHidden(game.spriteRenderer, kHidden);
        // Both branches leave through the same pair of redraws (:4482-4483), which is what makes
        // the status change visible.
        renderActivePieceSprite(game);
        renderPreviewPieceSprite(game);
        return true;
    }

    game.display.displayed = DisplayedMap::FIRST;  // (:4487)
    resumeRound(game, nowFrom(now));
    game.audioCues.pause = AudioPauseCommand::UNPAUSE;
    game.spriteRenderer.slots[kActivePieceSlot].hidden = false;
    // The preview comes back only if the player has not hidden it (tetris.asm:4490-4494).
    game.spriteRenderer.slots[kPreviewPieceSlot].hidden = game.engine.hidePreviewPiece;
    renderActivePieceSprite(game);
    renderPreviewPieceSprite(game);
    return true;
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

void initGameOver(GameContext& game, const NowNanos& now) {
    // Topping out is the end of the round, and the score is still standing here - the curtain and the
    // screens after it do not change it, but this is the point play stopped.
    endRound(game, nowFrom(now));

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

    // The scoring modes earn a rocket ending; Type B has its own (tetris.asm:4943-4945). Type C earns
    // one at the same score boundaries Type A does.
    if (game.flow.gameType != GameType::TYPE_B) {
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
    // Back to the difficulty screen the round came from, one per mode.
    switch (game.flow.gameType) {
        case GameType::TYPE_B:
            game.flow.gameState = GameState::INIT_TYPE_B_DIFFICULTY;
            break;
        case GameType::TYPE_C:
            game.flow.gameState = GameState::INIT_TYPE_C_DIFFICULTY;
            break;
        default:
            game.flow.gameState = GameState::INIT_TYPE_A_DIFFICULTY;
            break;
    }
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
    dispatcher.setHandler(GameState::INIT_GAME, [wiring](GameContext& g) {
        initGame(g, wiring.draw, wiring.initGarbage, wiring.now);
    });
    dispatcher.setHandler(GameState::NORMAL_GAMEPLAY, [wiring](GameContext& g) {
        normalGameplay(g, wiring.demo, wiring.softReset, wiring.now);
    });
    dispatcher.setHandler(GameState::INIT_GAME_OVER,
                          [wiring](GameContext& g) { initGameOver(g, wiring.now); });
    dispatcher.setHandler(GameState::GAME_OVER_CURTAIN, gameOverCurtain);
    dispatcher.setHandler(GameState::GAME_OVER_SCREEN, gameOverScreen);
    dispatcher.setHandler(GameState::INIT_TYPE_B_SCOREBOARD, initTypeBScoreboard);
    dispatcher.setHandler(GameState::STATE_0C_UNKNOWN, state0CUnknown);
}

}  // namespace kirpich::systems
