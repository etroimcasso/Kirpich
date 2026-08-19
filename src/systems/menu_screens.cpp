#include "systems/menu_screens.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/sprite_id.h>

#include "data/misc.h"
#include "data/scene_sprites.h"
#include "data/sfx.h"
#include "data/tilemaps.h"  // kConfigScreenTilemap, kTypeADifficultyTilemap, kTypeBDifficultyTilemap
#include "retropp/input.h"  // actionId
#include "state/display_state.h"
#include "state/engine_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/screen.h"           // loadScreenTilemap, loadTileSheet
#include "systems/sprite_renderer.h"  // renderCursors

namespace kirpich::systems {

namespace {

// The two cursor-slot roles the selection screens use. Slot 0 ($C200) carries the music-type cursor
// and the single / first digit cursor; slot 1 ($C210) carries the game-type cursor and the second
// digit cursor.
constexpr std::size_t kSlot0 = 0;  // $C200 — music cursor / first digit cursor
constexpr std::size_t kSlot1 = 1;  // $C210 — game-type cursor / second digit cursor

// The music-type cursor tiles form a 2x2 grid ($1C $1D / $1E $1F); the middle boundary is $1E.
constexpr std::uint8_t kMusicGridSecondRow = 0x1E;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Call_1675 / Call_16E6 (tetris.asm:3444-3448 / :3508-3512): write the next state and unhide the
// cursor slot (its status byte back to $00). The two upstream copies are byte-identical.
void setStateAndShowCursor(GameContext& game, GameState next, std::size_t slot) {
    game.flow.gameState = next;
    game.spriteRenderer.slots[slot].hidden = false;
}

}  // namespace

void blinkCursor(GameContext& game, std::size_t slot) {
    // ReadJoypadAndBlinkCursor (tetris.asm:3597-3608). The pressed snapshot the original reads here
    // already lives on game.joypad (the dispatcher sampled it before this handler ran), so only the
    // blink remains. It is gated on the frame timer, which the frame loop decrements each tick: while
    // the timer counts the cursor holds; when it reaches zero the cursor toggles and the timer reloads
    // 16 frames. The toggle (XOR, not set) means a same-tick visibility write elsewhere composes.
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.timer1 = 16;
    SpriteSlot& s = game.spriteRenderer.slots[slot];
    s.hidden = !s.hidden;
}

void switchMusic(GameContext& game) {
    // SwitchMusic (tetris.asm:3248-3256): the music-type cursor value ($1C-$1F) minus $17 is the song
    // id (5 = Type A, 6 = Type B, 7 = Type C); offset 8 (cursor $1F, "music off") maps to the stop-all
    // cue instead.
    const auto value =
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(game.flow.musicType) - 0x17);
    game.audioCues.music = (value == 0x08) ? MusicId::STOP : static_cast<MusicId>(value);
}

void positionMusicTypeSprite(GameContext& game, bool playSfx) {
    // PositionMusicTypeSprite (tetris.asm:3152-3172): place the music-type cursor (slot 0) at the
    // coordinate for the current music type and set its sprite to the music-type tile itself (the four
    // choices have consecutive sprite ids $1C-$1F). The top entry also cues the menu-move sound —
    // whether the driver plays it is the sound unit's concern (the original's own "I don't think this
    // plays" note); the .positionSprite entry that skips the cue is used only by the two-player init.
    if (playSfx) {
        game.audioCues.square = SquareSfxId::TINK;
    }
    const SpriteCoordinate coord = musicTypeSpriteCoordinate(game.flow.musicType);
    SpriteSlot& s = game.spriteRenderer.slots[kSlot0];
    s.y = coord.y;
    s.x = coord.x;
    s.spriteId = static_cast<SpriteId>(static_cast<std::uint8_t>(game.flow.musicType));
}

void updateDigitCursor(GameContext& game, std::size_t slot,
                       std::span<const SpriteCoordinate> coords, std::uint8_t index, bool playSfx) {
    // UpdateDigitCursor (tetris.asm:3574-3594): move a digit cursor to the coordinate for the selected
    // value and set its sprite to that digit (digit 0 is sprite $20, so the sprite is index + $20). The
    // top entry cues the menu-move sound; the .afterSFX entry (used elsewhere) skips it. Every
    // selection-screen and difficulty-init caller enters at the top, so these callers pass playSfx.
    if (playSfx) {
        game.audioCues.square = SquareSfxId::TINK;
    }
    const SpriteCoordinate coord = coords[index];
    SpriteSlot& s = game.spriteRenderer.slots[slot];
    s.y = coord.y;
    s.x = coord.x;
    s.spriteId = static_cast<SpriteId>(static_cast<std::uint8_t>(SpriteId::DIGIT_0) + index);
}

void loadSceneSprites(SpriteRendererState& renderer, std::span<const SceneSprite> sprites) {
    // LoadSprites (tetris.asm:3611-3628): copy each scene record into consecutive $10-byte sprite slots
    // from slot 0, then hide the slot past the last (the terminator: its status byte to $80). Each
    // record fills the six bytes the renderer state models per slot — status, Y, X, sprite, and the
    // priority / flip attribute pair. The scene corpus never sets vertical flip, so it clears to false.
    std::size_t slot = 0;
    for (const SceneSprite& src : sprites) {
        SpriteSlot& dst = renderer.slots[slot];
        dst.hidden = src.hidden;
        dst.y = src.y;
        dst.x = src.x;
        dst.spriteId = src.sprite;
        dst.behindBg = src.behindBg;
        dst.yflip = false;
        dst.xflip = src.xflip;
        ++slot;
    }
    renderer.slots[slot].hidden = true;
}

void clearOamObjects(GameContext& game) {
    // ClearObjects (tetris.asm:3630-3638): zero the whole 40-entry OAM staging buffer. The port also
    // forgets what drew each entry: an emptied buffer holds no objects, so there is nothing left for
    // the next screen's objects to be mistaken for.
    game.engine.oam.fill(OamEntry{});
    game.oamSources.reset();
}

void loadConfigScreenBody(GameContext& game) {
    // GameState_08 .loadTiles (tetris.asm:3121-3148), entered directly by the demo-start and two-player
    // paths as well as by the config-screen state. The LCD-on step is render mechanism; everything else
    // is the art and backdrop loads, the object-buffer clear, the two cursor sprites, placing the
    // music-type cursor (slot 0) and the game-type cursor (slot 1), the music cue, and the transition
    // into game-type selection.
    loadTileSheet(game.display, TileSheet::GAMEPLAY);        // LoadGameplayTiles (:3123)
    loadScreenTilemap(game.display, kConfigScreenTilemap);     // (:3124-3125)
    clearOamObjects(game);
    loadSceneSprites(game.spriteRenderer, configScreenSprites());  // Data_26CF: 2 markers + terminator

    positionMusicTypeSprite(game, /*playSfx=*/true);

    // The game-type cursor's X coordinate is the game-type value itself, and its sprite the matching
    // label; the load above supplied the rest of the slot.
    SpriteSlot& gameCursor = game.spriteRenderer.slots[kSlot1];
    gameCursor.x = static_cast<std::uint8_t>(game.flow.gameType);
    gameCursor.spriteId =
        (game.flow.gameType == GameType::TYPE_A) ? SpriteId::A_TYPE : SpriteId::B_TYPE;

    renderCursors(game);  // (:3143)
    switchMusic(game);
    game.flow.gameState = GameState::SELECT_GAME_TYPE;
}

void initConfigScreen(GameContext& game) {
    // GameState_08 (tetris.asm:3114-3148). The routine opens by resetting the serial hardware registers
    // (interrupt-enable, serial data / control, interrupt-flag) — link-cable mechanism the serial unit
    // owns, with no simulation effect here — then runs the shared screen body.
    loadConfigScreenBody(game);
}

void selectGameType(GameContext& game) {
    // GameState_0E (tetris.asm:3258-3314): the game-type selector (slot 1). The game-type value doubles
    // as the cursor's X coordinate and selects its label sprite. Left picks Type A, Right picks Type B
    // (each cues the menu-move sound). Confirm advances to music selection; Start cues the change-screen
    // sound and advances to the chosen difficulty screen; both unhide the cursor as they leave. Up and
    // down are ignored.
    blinkCursor(game, kSlot1);

    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot1];

    if (pressed(game, Action::Start)) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        game.flow.gameState = (game.flow.gameType == GameType::TYPE_A)
                                  ? GameState::INIT_TYPE_A_DIFFICULTY
                                  : GameState::INIT_TYPE_B_DIFFICULTY;
        cursor.hidden = false;
        return;
    }
    if (pressed(game, Action::Confirm)) {
        game.flow.gameState = GameState::SELECT_MUSIC_TYPE;
        cursor.hidden = false;
        return;
    }
    // Every d-pad path — including the two end-stops that change nothing — leaves through the shared
    // exit that redraws the cursors (:3296). The Start and Confirm transitions above do not: they
    // leave by the state-change path (:3300-3313), which never reaches the redraw.
    if (pressed(game, Action::MenuRight)) {
        if (game.flow.gameType != GameType::TYPE_B) {
            game.flow.gameType = GameType::TYPE_B;
            game.audioCues.square = SquareSfxId::TINK;
            cursor.x = static_cast<std::uint8_t>(GameType::TYPE_B);
            cursor.spriteId = SpriteId::B_TYPE;
        }
    } else if (pressed(game, Action::MenuLeft)) {
        if (game.flow.gameType != GameType::TYPE_A) {
            game.flow.gameType = GameType::TYPE_A;
            game.audioCues.square = SquareSfxId::TINK;
            cursor.x = static_cast<std::uint8_t>(GameType::TYPE_A);
            cursor.spriteId = SpriteId::A_TYPE;
        }
    }

    renderCursors(game);
}

void selectMusicType(GameContext& game) {
    // GameState_0F (tetris.asm:3181-3246): the music-type selector (slot 0), a 2x2 grid over cursor
    // tiles $1C-$1F ($1C $1D / $1E $1F). Start and Confirm share the game-type screen's advance path.
    // Back returns to game-type selection in one-player (unhiding this cursor); in two-player it is
    // inert (the original falls through to the d-pad, and Back sets no direction). Each move repositions
    // the cursor, updates the music, and cues the menu-move sound.
    blinkCursor(game, kSlot0);

    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot0];

    if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        game.flow.gameState = (game.flow.gameType == GameType::TYPE_A)
                                  ? GameState::INIT_TYPE_A_DIFFICULTY
                                  : GameState::INIT_TYPE_B_DIFFICULTY;
        cursor.hidden = false;
        return;
    }
    if (pressed(game, Action::Back) && !game.multiplayer.isMultiplayer) {
        game.flow.gameState = GameState::SELECT_GAME_TYPE;
        cursor.hidden = false;
        return;
    }

    const auto before = static_cast<std::uint8_t>(game.flow.musicType);
    std::uint8_t value = before;
    if (pressed(game, Action::MenuRight)) {
        if (value != 0x1D && value != 0x1F) ++value;
    } else if (pressed(game, Action::MenuLeft)) {
        if (value != 0x1C && value != 0x1E) --value;
    } else if (pressed(game, Action::MenuUp)) {
        if (value >= kMusicGridSecondRow) value -= 0x02;
    } else if (pressed(game, Action::MenuDown)) {
        if (value < kMusicGridSecondRow) value += 0x02;
    }
    if (value != before) {
        game.flow.musicType = static_cast<MusicType>(value);
        positionMusicTypeSprite(game, /*playSfx=*/true);
        switchMusic(game);
    }

    // The shared d-pad exit. The original's down-boundary case reaches the game-type screen's copy
    // of this redraw rather than its own (`jp z, GameState_0E.out`, :3201 — the disassembly notes
    // the detour); both are the same call, so one exit serves.
    renderCursors(game);
}

void initTypeADifficultyScreen(GameContext& game, const TopScoresRefresh& refresh) {
    // GameState_10 (tetris.asm:3317-3342): set up the Type A difficulty screen. The LCD-on step is
    // render mechanism, and the top-score-field clear and the draw-to-VRAM are top-score render seams
    // (no simulation effect here). The rest is the backdrop, clearing the object buffer, loading the
    // one digit cursor, placing it at the chosen level (which cues the menu-move sound), and refreshing
    // the Type A top scores. Level selection is entered next — unless the just-finished game earned a
    // top score, which routes straight to name entry. No art load here: the config screen this is
    // entered from already loaded the gameplay set, and the original does not reload it (:3318-3320).
    loadScreenTilemap(game.display, kTypeADifficultyTilemap);  // (:3319-3320)
    clearOamObjects(game);  // ClearTopScoreFields is a top-score render seam (no sim effect)
    loadSceneSprites(game.spriteRenderer, typeADifficultySprites());  // Data_26DB: 1 digit cursor

    updateDigitCursor(game, kSlot0, kTypeALevelCursorCoordinates, game.flow.typeALevel,
                      /*playSfx=*/true);
    if (refresh) refresh(game);  // UpdateTypeATopScores; DrawTopScoresToVRAM is a render seam
    renderCursors(game);         // (:3331) — before the branch, so both exits draw the cursor

    game.flow.gameState = GameState::TYPE_A_LEVEL_SELECTION;
    if (game.highScores.newTopScore) {
        game.flow.gameState = GameState::ENTER_TOP_SCORE;
    } else {
        switchMusic(game);
    }
}

void selectTypeALevel(GameContext& game, const TopScoresRefresh& refresh) {
    // GameState_11 (tetris.asm:3350-3400): the Type A level picker — a 2x5 grid over levels 0-9
    // (slot 0). Start or Confirm begins the game; Back returns to the config screen. These transitions
    // write the state without unhiding the cursor — the asymmetry the Type B pickers do not share. A
    // move repositions the cursor and refreshes the top scores.
    blinkCursor(game, kSlot0);

    if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
        game.flow.gameState = GameState::INIT_GAME;  // GameState_10.nextState — no cursor unhide
        return;
    }
    if (pressed(game, Action::Back)) {
        game.flow.gameState = GameState::INIT_TYPE_SELECTION;  // no cursor unhide
        return;
    }

    const std::uint8_t before = game.flow.typeALevel;
    std::uint8_t value = before;
    if (pressed(game, Action::MenuRight)) {
        if (value != 9) ++value;
    } else if (pressed(game, Action::MenuLeft)) {
        if (value != 0) --value;
    } else if (pressed(game, Action::MenuUp)) {
        if (value >= 5) value -= 5;
    } else if (pressed(game, Action::MenuDown)) {
        if (value < 5) value += 5;
    }
    if (value != before) {
        game.flow.typeALevel = value;
        updateDigitCursor(game, kSlot0, kTypeALevelCursorCoordinates, value, /*playSfx=*/true);
        if (refresh) refresh(game);  // UpdateTypeATopScores
    }

    renderCursors(game);  // .renderCursor (:3386-3388), the exit every d-pad path converges on
}

void initTypeBDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh) {
    // GameState_12 (tetris.asm:3408-3441): the Type B difficulty screen, with two digit cursors — the
    // level (slot 0) and the starting garbage height (slot 1). Same shape as the Type A init, but with
    // no separate top-score-field clear (the Type B refresh does its own) and two cursors seeded.
    loadScreenTilemap(game.display, kTypeBDifficultyTilemap);  // (:3410-3411)
    clearOamObjects(game);
    loadSceneSprites(game.spriteRenderer, typeBDifficultySprites());  // Data_26E1: 2 digit cursors

    updateDigitCursor(game, kSlot0, kTypeBLevelCursorCoordinates, game.flow.typeBLevel,
                      /*playSfx=*/true);
    updateDigitCursor(game, kSlot1, kTypeBStartHeightCursorCoordinates, game.flow.typeBStartHeight,
                      /*playSfx=*/true);
    if (refresh) refresh(game);  // UpdateTypeBTopScores; DrawTopScoresToVRAM is a render seam
    renderCursors(game);         // (:3425) — before the branch, as on the Type A screen

    game.flow.gameState = GameState::TYPE_B_LEVEL_SELECTION;
    if (game.highScores.newTopScore) {
        game.flow.gameState = GameState::ENTER_TOP_SCORE;
    } else {
        switchMusic(game);
    }
}

void selectTypeBLevel(GameContext& game, const TopScoresRefresh& refresh) {
    // GameState_13 (tetris.asm:3450-3501): the Type B level picker — a 2x5 grid over levels 0-9
    // (slot 0). Start begins the game, Confirm advances to the start-height picker, Back returns to the
    // config screen; each transition unhides the cursor.
    blinkCursor(game, kSlot0);

    if (pressed(game, Action::Start)) {
        setStateAndShowCursor(game, GameState::INIT_GAME, kSlot0);
        return;
    }
    if (pressed(game, Action::Confirm)) {
        setStateAndShowCursor(game, GameState::TYPE_B_HEIGHT_SELECTION, kSlot0);
        return;
    }
    if (pressed(game, Action::Back)) {
        setStateAndShowCursor(game, GameState::INIT_TYPE_SELECTION, kSlot0);
        return;
    }

    const std::uint8_t before = game.flow.typeBLevel;
    std::uint8_t value = before;
    if (pressed(game, Action::MenuRight)) {
        if (value != 9) ++value;
    } else if (pressed(game, Action::MenuLeft)) {
        if (value != 0) --value;
    } else if (pressed(game, Action::MenuUp)) {
        if (value >= 5) value -= 5;
    } else if (pressed(game, Action::MenuDown)) {
        if (value < 5) value += 5;
    }
    if (value != before) {
        game.flow.typeBLevel = value;
        updateDigitCursor(game, kSlot0, kTypeBLevelCursorCoordinates, value, /*playSfx=*/true);
        if (refresh) refresh(game);  // UpdateTypeBTopScores
    }

    renderCursors(game);  // the shared d-pad exit (:3488)
}

void selectTypeBHeight(GameContext& game, const TopScoresRefresh& refresh) {
    // GameState_14 (tetris.asm:3514-3564): the Type B starting-height picker — a 2x3 grid over heights
    // 0-5 (slot 1). Start or Confirm begins the game; Back returns to level selection; each transition
    // unhides the cursor.
    blinkCursor(game, kSlot1);

    if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
        setStateAndShowCursor(game, GameState::INIT_GAME, kSlot1);
        return;
    }
    if (pressed(game, Action::Back)) {
        setStateAndShowCursor(game, GameState::TYPE_B_LEVEL_SELECTION, kSlot1);
        return;
    }

    const std::uint8_t before = game.flow.typeBStartHeight;
    std::uint8_t value = before;
    if (pressed(game, Action::MenuRight)) {
        if (value != 5) ++value;
    } else if (pressed(game, Action::MenuLeft)) {
        if (value != 0) --value;
    } else if (pressed(game, Action::MenuUp)) {
        if (value >= 3) value -= 3;
    } else if (pressed(game, Action::MenuDown)) {
        if (value < 3) value += 3;
    }
    if (value != before) {
        game.flow.typeBStartHeight = value;
        updateDigitCursor(game, kSlot1, kTypeBStartHeightCursorCoordinates, value, /*playSfx=*/true);
        if (refresh) refresh(game);  // UpdateTypeBTopScores
    }

    renderCursors(game);  // the shared d-pad exit (:3551)
}

void installMenuScreenHandlers(GameStateDispatcher& dispatcher) {
    dispatcher.setHandler(GameState::INIT_TYPE_SELECTION, initConfigScreen);
    dispatcher.setHandler(GameState::SELECT_GAME_TYPE, selectGameType);
    dispatcher.setHandler(GameState::SELECT_MUSIC_TYPE, selectMusicType);
    dispatcher.setHandler(GameState::INIT_TYPE_A_DIFFICULTY,
                          [](GameContext& g) { initTypeADifficultyScreen(g); });
    dispatcher.setHandler(GameState::TYPE_A_LEVEL_SELECTION,
                          [](GameContext& g) { selectTypeALevel(g); });
    dispatcher.setHandler(GameState::INIT_TYPE_B_DIFFICULTY,
                          [](GameContext& g) { initTypeBDifficultyScreen(g); });
    dispatcher.setHandler(GameState::TYPE_B_LEVEL_SELECTION,
                          [](GameContext& g) { selectTypeBLevel(g); });
    dispatcher.setHandler(GameState::TYPE_B_HEIGHT_SELECTION,
                          [](GameContext& g) { selectTypeBHeight(g); });
}

}  // namespace kirpich::systems
