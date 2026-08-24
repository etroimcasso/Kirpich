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
#include "systems/settings_screen.h"  // blinkScreenCursor
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

// ── The game-type grid ────────────────────────────────────────────────────────────────────────────
//
// With the extra modes turned on, the game-type box grows from one row of choices to two, in the
// music box's own form: A and B on the top row, C on the bottom left, and the fourth cell left empty
// for whatever comes next. The cursor skips that empty cell, so a player cannot land on nothing.
//
// The box grows downward into the two blank rows below it, which is exactly the room the music box's
// extra two rows take, so the screen still ends where it ended.
// The stored box puts its bottom rule on row 6 and leaves rows 7 and 8 blank. The grid writes a rule
// where that bottom was, its second choice row into the first blank, and the bottom rule into the
// second - so the box ends on row 8 and the music box below it never moves.
constexpr std::size_t kGameGridDividerRow = 6;
constexpr std::size_t kGameGridSecondRow  = 7;
constexpr std::size_t kGameGridBottomRow  = 8;

// The stored screen's own rows the grid is built from: the game box's bottom rule, and the music
// box's rule and second choice row, plus the columns that row's right-hand choice occupies.
constexpr std::size_t kStoredGameBoxBottom   = 6;
constexpr std::size_t kMusicDividerRow       = 13;
constexpr std::size_t kMusicSecondChoiceRow  = 14;
constexpr std::size_t kGridRightHalfFirstCol = 11;
constexpr std::size_t kGridRightHalfLastCol  = 16;

// Where the cursor sits on each cell. The x values are the two the stored screen already uses for the
// left and right halves of a choice row; the second row is two cells below the first.
constexpr std::uint8_t kGameCursorLeftX   = 0x37;
constexpr std::uint8_t kGameCursorRightX  = 0x77;
constexpr std::uint8_t kGameCursorTopY    = 0x38;
constexpr std::uint8_t kGameCursorBottomY = kGameCursorTopY + 2 * 8;

// Which cell of the grid a game type occupies. Type C's byte is not a screen coordinate the way the
// two cartridge values are, so the grid places every cursor itself rather than reading the byte.
struct GridCell {
    std::uint8_t x;
    std::uint8_t y;
};

GridCell gridCellFor(GameType type) {
    switch (type) {
        case GameType::TYPE_B: return {kGameCursorRightX, kGameCursorTopY};
        case GameType::TYPE_C: return {kGameCursorLeftX, kGameCursorBottomY};
        default:               return {kGameCursorLeftX, kGameCursorTopY};
    }
}

SpriteId labelFor(GameType type) {
    switch (type) {
        case GameType::TYPE_B: return SpriteId::B_TYPE;
        case GameType::TYPE_C: return SpriteId::C_TYPE;
        default:               return SpriteId::A_TYPE;
    }
}

// The difficulty screen a game type begins at — one per mode.
GameState difficultyScreenFor(GameType type) {
    switch (type) {
        case GameType::TYPE_B: return GameState::INIT_TYPE_B_DIFFICULTY;
        case GameType::TYPE_C: return GameState::INIT_TYPE_C_DIFFICULTY;
        default:               return GameState::INIT_TYPE_A_DIFFICULTY;
    }
}

// Which cell each direction leads to, or the same type when that way is a wall or the empty cell.
GameType gameTypeAfterMove(GameType from, Action direction) {
    switch (direction) {
        case Action::MenuRight:
            return from == GameType::TYPE_A ? GameType::TYPE_B : from;  // nothing right of C
        case Action::MenuLeft:
            return from == GameType::TYPE_B ? GameType::TYPE_A : from;
        case Action::MenuDown:
            return from == GameType::TYPE_A ? GameType::TYPE_C : from;  // nothing below B
        case Action::MenuUp:
            return from == GameType::TYPE_C ? GameType::TYPE_A : from;
        default:
            return from;
    }
}

// Grow the game-type box into the two blank rows below it, in the music box's form: the two choice
// rows separated by a rule, C on the bottom left, and the bottom right cell empty.
//
// The rows are copied from the stored screen's own music box rather than composed, so the grid is
// built out of the art the screen already uses and the two boxes cannot drift apart. The stored map is
// read, never written.
void layOutGameTypeGrid(BackgroundMap& map) {
    // The rule between two choice rows, and the second choice row itself: the music box's, which is
    // the only other place this screen draws them.
    for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
        map[kGameGridDividerRow][col] = kConfigScreenTilemap[kMusicDividerRow][col];
        map[kGameGridSecondRow][col]  = kConfigScreenTilemap[kMusicSecondChoiceRow][col];
        map[kGameGridBottomRow][col]  = kConfigScreenTilemap[kStoredGameBoxBottom][col];
    }

    // The music box's second row carries its own right-hand choice; the game-type grid's fourth cell
    // is empty until there is a fourth mode to put in it.
    for (std::size_t col = kGridRightHalfFirstCol; col <= kGridRightHalfLastCol; ++col) {
        map[kGameGridSecondRow][col] = static_cast<std::uint8_t>(CharTile::SPACE);
    }
}

// Put the game-type cursor on its cell and give it the matching label.
void placeGameTypeCursor(GameContext& game, bool grid) {
    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot1];
    cursor.spriteId    = labelFor(game.flow.gameType);
    if (grid) {
        const GridCell cell = gridCellFor(game.flow.gameType);
        cursor.x = cell.x;
        cursor.y = cell.y;
    } else {
        // The stored screen's own pun: the game-type byte is the cursor's x coordinate.
        cursor.x = static_cast<std::uint8_t>(game.flow.gameType);
    }
}

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

// Call_1675 / Call_16E6 (tetris.asm:3444-3448 / :3508-3512): write the next state and unhide the
// cursor slot (its status byte back to $00). The two upstream copies are byte-identical.
void setStateAndShowCursor(GameContext& game, GameState next, std::size_t slot) {
    game.flow.gameState = next;
    game.spriteRenderer.slots[slot].hidden = false;
}

// Where the stored config screen keeps the two boxes the section is squeezed between, and a row of
// its interior with nothing on it but the border.
constexpr std::size_t kGameBoxFirst  = 2;
constexpr std::size_t kGameBoxLast   = 6;
constexpr std::size_t kMusicBoxFirst = 9;
constexpr std::size_t kMusicBoxLast  = 15;
constexpr std::size_t kBlankRow      = 7;


// Where those boxes end up, and therefore where the gap between them opens.
constexpr std::size_t kGapFirst = kGameBoxLast - kConfigSectionShiftRows + 1;
constexpr std::size_t kGapLast  = kMusicBoxFirst + kConfigSectionShiftRows - 1;

static_assert(kGapFirst == kConfigSectionGapFirstRow && kGapLast - kGapFirst + 1 == kConfigSectionGapRows,
              "the section is placed against the gap the two boxes open, so the two must agree");

// Which row of the stored config screen a row of the re-laid one holds. The two borders stay where
// they are; the boxes move a row apart; what opens between them is the screen's own blank interior.
std::size_t sourceRowFor(std::size_t row) {
    if (row >= kGameBoxFirst - kConfigSectionShiftRows &&
        row <= kGameBoxLast - kConfigSectionShiftRows) {
        return row + kConfigSectionShiftRows;
    }
    if (row >= kMusicBoxFirst + kConfigSectionShiftRows &&
        row <= kMusicBoxLast + kConfigSectionShiftRows) {
        return row - kConfigSectionShiftRows;
    }
    if (row >= kGapFirst && row <= kGapLast) {
        return kBlankRow;
    }
    return row;
}

// The music-type cursor is placed from the stored coordinate table, which describes the cartridge's
// screen. When the third section has pushed the music box down, the cursor goes with the box it
// points into — every time the table is read, not only on the way in.
void followMusicBox(GameContext& game, bool showSection) {
    if (!showSection) {
        return;
    }
    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot0];
    cursor.y = static_cast<std::uint8_t>(cursor.y + kConfigSectionShiftPixels);
}

}  // namespace

void layOutConfigSection(BackgroundMap& map) {
    for (std::size_t row = 0; row < kTilemapScreenRows; ++row) {
        const std::size_t source = sourceRowFor(row);
        for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
            map[row][col] = kConfigScreenTilemap[source][col];
        }
    }
}

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

void loadConfigScreenBody(GameContext& game, bool showSection, bool showGrid) {
    // GameState_08 .loadTiles (tetris.asm:3121-3148), entered directly by the demo-start and two-player
    // paths as well as by the config-screen state. The LCD-on step is render mechanism; everything else
    // is the art and backdrop loads, the object-buffer clear, the two cursor sprites, placing the
    // music-type cursor (slot 0) and the game-type cursor (slot 1), the music cue, and the transition
    // into game-type selection.
    loadTileSheet(game.display, TileSheet::GAMEPLAY);        // LoadGameplayTiles (:3123)
    loadScreenTilemap(game.display, kConfigScreenTilemap);     // (:3124-3125)

    // With the third section shown, the screen is re-laid over the stamp above: its two boxes a row
    // apart, and the rows between them left blank for the section to be drawn over.
    if (showSection) {
        layOutConfigSection(game.display.map);
    }

    clearOamObjects(game);
    loadSceneSprites(game.spriteRenderer, configScreenSprites());  // Data_26CF: 2 markers + terminator

    positionMusicTypeSprite(game, /*playSfx=*/true);
    followMusicBox(game, showSection);

    // With the extra modes on, the game-type box grows a second choice row before the cursor is
    // placed, because where the cursor goes depends on the box it is pointing into.
    if (showGrid) {
        layOutGameTypeGrid(game.display.map);
    }

    // The cursor's cell and its label; the load above supplied the rest of the slot.
    SpriteSlot& gameCursor = game.spriteRenderer.slots[kSlot1];
    placeGameTypeCursor(game, showGrid);
    if (showSection) {
        // Up with the box it points into, as the music cursor goes down with the other one.
        gameCursor.y = static_cast<std::uint8_t>(gameCursor.y - kConfigSectionShiftPixels);
    }

    renderCursors(game);  // (:3143)
    switchMusic(game);
    game.flow.gameState = GameState::SELECT_GAME_TYPE;
}

void initConfigScreen(GameContext& game, bool showSection, bool showGrid) {
    // GameState_08 (tetris.asm:3114-3148). The routine opens by resetting the serial hardware registers
    // (interrupt-enable, serial data / control, interrupt-flag) — link-cable mechanism the serial unit
    // owns, with no simulation effect here — then runs the shared screen body.
    loadConfigScreenBody(game, showSection, showGrid);
}

void selectGameType(GameContext& game, bool showSection, bool showGrid) {
    // GameState_0E (tetris.asm:3258-3314): the game-type selector (slot 1). The game-type value doubles
    // as the cursor's X coordinate and selects its label sprite. Left picks Type A, Right picks Type B
    // (each cues the menu-move sound). Confirm advances to music selection; Start cues the change-screen
    // sound and advances to the chosen difficulty screen; both unhide the cursor as they leave. Up and
    // down are ignored.
    blinkCursor(game, kSlot1);

    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot1];

    if (pressed(game, Action::Start)) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        game.flow.gameState   = difficultyScreenFor(game.flow.gameType);
        cursor.hidden = false;
        return;
    }
    if (pressed(game, Action::Confirm)) {
        // The next section down, which is the third one when the screen has grown it.
        game.flow.gameState =
            showSection ? GameState::SELECT_MODE_OPTION : GameState::SELECT_MUSIC_TYPE;
        cursor.hidden = false;
        return;
    }
    // Every d-pad path — including the end-stops that change nothing — leaves through the shared exit
    // that redraws the cursors (:3296). The Start and Confirm transitions above do not: they leave by
    // the state-change path (:3300-3313), which never reaches the redraw.
    //
    // With the grid up the walk is two-dimensional and every direction is answered by the cell map;
    // without it the screen has one row of two choices, which is the cartridge's own walk.
    for (const Action direction :
         {Action::MenuRight, Action::MenuLeft, Action::MenuDown, Action::MenuUp}) {
        if (!pressed(game, direction)) {
            continue;
        }
        const GameType next =
            showGrid ? gameTypeAfterMove(game.flow.gameType, direction)
                     : (direction == Action::MenuRight  ? GameType::TYPE_B
                        : direction == Action::MenuLeft ? GameType::TYPE_A
                                                        : game.flow.gameType);
        if (next != game.flow.gameType) {
            game.flow.gameType    = next;
            game.audioCues.square = SquareSfxId::TINK;
            placeGameTypeCursor(game, showGrid);
        }
        break;
    }

    renderCursors(game);
}

void selectMusicType(GameContext& game, bool showSection) {
    // GameState_0F (tetris.asm:3181-3246): the music-type selector (slot 0), a 2x2 grid over cursor
    // tiles $1C-$1F ($1C $1D / $1E $1F). Start and Confirm share the game-type screen's advance path.
    // Back returns to game-type selection in one-player (unhiding this cursor); in two-player it is
    // inert (the original falls through to the d-pad, and Back sets no direction). Each move repositions
    // the cursor, updates the music, and cues the menu-move sound.
    blinkCursor(game, kSlot0);

    SpriteSlot& cursor = game.spriteRenderer.slots[kSlot0];

    if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        game.flow.gameState   = difficultyScreenFor(game.flow.gameType);
        cursor.hidden = false;
        return;
    }
    if (pressed(game, Action::Back) && !game.multiplayer.isMultiplayer) {
        // One section back, which is the third one when the screen has grown it.
        game.flow.gameState =
            showSection ? GameState::SELECT_MODE_OPTION : GameState::SELECT_GAME_TYPE;
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
        followMusicBox(game, showSection);
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

void initTypeCDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh) {
    // The Type A difficulty screen's shape, over Type C's own stored level. The screen the cartridge
    // draws for Type A is a level picker and nothing more, so the same backdrop, the same single digit
    // cursor and the same 2x5 grid serve a mode that also picks nothing but a level.
    loadScreenTilemap(game.display, kTypeADifficultyTilemap);
    clearOamObjects(game);
    loadSceneSprites(game.spriteRenderer, typeADifficultySprites());

    updateDigitCursor(game, kSlot0, kTypeALevelCursorCoordinates, game.flow.typeCLevel,
                      /*playSfx=*/true);
    if (refresh) refresh(game);
    renderCursors(game);

    game.flow.gameState = GameState::TYPE_C_LEVEL_SELECTION;
    if (game.highScores.newTopScore) {
        game.flow.gameState = GameState::ENTER_TOP_SCORE;
    } else {
        switchMusic(game);
    }
}

void selectTypeCLevel(GameContext& game, const TopScoresRefresh& refresh) {
    // The Type A level picker's walk, writing Type C's level: a 2x5 grid over levels 0-9. Start or
    // Confirm begins the round; Back returns to the config screen. Neither transition unhides the
    // cursor, which is the Type A screen's own asymmetry and is kept here so the two read alike.
    blinkCursor(game, kSlot0);

    if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
        game.flow.gameState = GameState::INIT_GAME;
        return;
    }
    if (pressed(game, Action::Back)) {
        game.flow.gameState = GameState::INIT_TYPE_SELECTION;
        return;
    }

    const std::uint8_t before = game.flow.typeCLevel;
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
        game.flow.typeCLevel = value;
        updateDigitCursor(game, kSlot0, kTypeALevelCursorCoordinates, value, /*playSfx=*/true);
        if (refresh) refresh(game);
    }

    renderCursors(game);
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

void selectModeOption(GameContext& game) {
    // The blink the section's chosen label is drawn by. It is the same law and the same timer the
    // neighbouring sections blink their cursor sprites on; what differs is that this section has no
    // cursor of its own — the choice is shown by the label itself coming and going.
    blinkScreenCursor(game);

    // Every way out leaves the label drawn rather than wherever the blink had got to, which is what
    // the neighbours do by unhiding their cursor slot as they go.
    const auto leaveFor = [&game](GameState next) {
        game.screens.cursorVisible = true;
        game.flow.gameState        = next;
    };

    if (pressed(game, Action::Start)) {
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;
        leaveFor(game.flow.gameType == GameType::TYPE_A ? GameState::INIT_TYPE_A_DIFFICULTY
                                                        : GameState::INIT_TYPE_B_DIFFICULTY);
        return;
    }
    if (pressed(game, Action::Confirm)) {
        leaveFor(GameState::SELECT_MUSIC_TYPE);
        return;
    }
    if (pressed(game, Action::Back)) {
        leaveFor(GameState::SELECT_GAME_TYPE);
        return;
    }

    if (pressed(game, Action::MenuRight) && !game.screens.modeOptionRight) {
        game.screens.modeOptionRight = true;
        game.audioCues.square        = SquareSfxId::TINK;
    } else if (pressed(game, Action::MenuLeft) && game.screens.modeOptionRight) {
        game.screens.modeOptionRight = false;
        game.audioCues.square        = SquareSfxId::TINK;
    }
}

void installMenuScreenHandlers(GameStateDispatcher& dispatcher, const TopScoresRefresh& typeA,
                               const TopScoresRefresh&      typeB,
                               const std::function<bool()>& showSection,
                               const std::function<bool()>& showGrid,
                               const TopScoresRefresh&      typeC) {
    // Both are asked per frame rather than captured once: whatever answers them is a setting a player
    // can change between one visit to this screen and the next.
    const auto section = [showSection] { return showSection && showSection(); };
    const auto grid    = [showGrid] { return showGrid && showGrid(); };

    dispatcher.setHandler(GameState::INIT_TYPE_SELECTION, [section, grid](GameContext& g) {
        initConfigScreen(g, section(), grid());
    });
    dispatcher.setHandler(GameState::SELECT_GAME_TYPE, [section, grid](GameContext& g) {
        selectGameType(g, section(), grid());
    });
    dispatcher.setHandler(GameState::SELECT_MUSIC_TYPE,
                          [section](GameContext& g) { selectMusicType(g, section()); });
    dispatcher.setHandler(GameState::SELECT_MODE_OPTION, selectModeOption);
    dispatcher.setHandler(GameState::INIT_TYPE_C_DIFFICULTY,
                          [typeC](GameContext& g) { initTypeCDifficultyScreen(g, typeC); });
    dispatcher.setHandler(GameState::TYPE_C_LEVEL_SELECTION,
                          [typeC](GameContext& g) { selectTypeCLevel(g, typeC); });
    dispatcher.setHandler(GameState::INIT_TYPE_A_DIFFICULTY,
                          [typeA](GameContext& g) { initTypeADifficultyScreen(g, typeA); });
    dispatcher.setHandler(GameState::TYPE_A_LEVEL_SELECTION,
                          [typeA](GameContext& g) { selectTypeALevel(g, typeA); });
    dispatcher.setHandler(GameState::INIT_TYPE_B_DIFFICULTY,
                          [typeB](GameContext& g) { initTypeBDifficultyScreen(g, typeB); });
    dispatcher.setHandler(GameState::TYPE_B_LEVEL_SELECTION,
                          [typeB](GameContext& g) { selectTypeBLevel(g, typeB); });
    dispatcher.setHandler(GameState::TYPE_B_HEIGHT_SELECTION,
                          [typeB](GameContext& g) { selectTypeBHeight(g, typeB); });
}

}  // namespace kirpich::systems
