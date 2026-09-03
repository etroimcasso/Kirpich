#include "systems/title_screens.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>

#include "data/charmap.h"   // encodeCharmapText
#include "data/demo.h"      // kDemoPieceList
#include "data/music.h"     // MusicId
#include "data/tilemaps.h"  // kCopyrightScreenTilemap, kTitleScreenTilemap
#include "retropp/input.h"  // actionId
#include "state/display_state.h"
#include "state/playing_field_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/line_clear.h"    // clearLineClearsList
#include "systems/menu_screens.h"  // clearOamObjects
#include "systems/screen.h"          // loadScreenTilemap, loadTileSheet, writeMapText
#include "systems/scoring.h"         // clearScoreAndStats
#include "systems/settings_screen.h"  // openSettings
#include "systems/stats_screens.h"    // openStatsMenu

namespace kirpich::systems {

namespace {

// The solid border tile the title board's walls and floor are drawn from ($8E).
constexpr std::uint8_t kFieldBorderTile = 0x8E;

// The 1P/2P selector cursor (OAM object 0) on the title screen: a fixed tile and Y, and one of two X
// positions for the one- and two-player choices.
constexpr std::uint8_t kTitleCursorTile = 0x58;
constexpr std::uint8_t kTitleCursorY = 0x80;
constexpr std::uint8_t kCursorX1P = 0x10;
constexpr std::uint8_t kCursorX2P = 0x60;

// The port's own bottom row, drawn as objects rather than as a row of the background.
//
// The background is a grid of eight-pixel cells and the panel's two spare cells are spoken for, but
// the art inside them is not. The player options' underline inks only the FIRST pixel row of its
// cell, leaving the seven below it blank, and a font letter inks six of its eight rows. So a word
// fits in that blank band with its own line beneath it - and objects are what can be placed there,
// because they are positioned per pixel while a background cell cannot be.
//
// Object coordinates are offset from the screen's by (8, 16). A word's letters ink screen rows
// 122-127, its line lands on 128 directly beneath them, and the copyright keeps a whole cell to
// itself at the bottom of the panel.
constexpr std::uint8_t kObjectOriginX = 8;
constexpr std::uint8_t kObjectOriginY = 16;

constexpr std::size_t  kSettingsTextCol = 6;  // "settings" - eight cells, centred in twenty
constexpr std::uint8_t kSettingsTextY   = 123 + kObjectOriginY;
constexpr std::uint8_t kSettingsLineY   = 130 + kObjectOriginY;
constexpr std::uint8_t kSettingsCursorY = 123 + kObjectOriginY;
constexpr std::uint8_t kSettingsCursorX = 0x30;  // one cell left of the word

// The two words the bottom row can hold. With the statistics switched off it carries the first
// alone, centred where it has always been; with them on it carries both, one under each player-count
// column.
constexpr std::string_view kSettingsWord = "settings";
constexpr std::string_view kStatsWord    = "stats";

// A bottom-row item's word starts one cell to the right of its cursor, which is how the player
// options above it are laid out.
constexpr std::size_t columnRightOf(std::uint8_t cursorX) {
    return static_cast<std::size_t>((cursorX - kObjectOriginX) / 8 + 1);
}

// The two columns are the player options' own, taken from their cursor positions rather than written
// out again, so an item sits under the option it belongs to by construction.
constexpr std::size_t kBottomLeftTextCol  = columnRightOf(kCursorX1P);
constexpr std::size_t kBottomRightTextCol = columnRightOf(kCursorX2P);

static_assert(kBottomLeftTextCol + kSettingsWord.size() < kBottomRightTextCol,
              "the left item must end before the right item's cursor, or one would run under the "
              "other");
static_assert(kBottomRightTextCol + kStatsWord.size() <= kTilemapScreenCols,
              "the right item must end within the screen");

// The tile the game underlines its own player options with, borrowed for the third item so the two
// are underlined with the same line.
constexpr std::uint8_t kUnderlineTile = 0x9A;

// The stored screen's copyright row. Its cell is cleared and the line is redrawn as objects one
// pixel higher than any cell could put it, so the panel keeps a margin under the notice instead of
// running it into the bottom of the screen.
constexpr std::size_t  kCopyrightRow = 16;
constexpr std::uint8_t kCopyrightY   = 135 + kObjectOriginY;

// The object entries the bottom row and the copyright line occupy. Entry 0 stays the selector, as
// the stored screen has it, and everything from here up is redrawn together each frame.
constexpr std::size_t kSettingsFirstObject = 1;

// Frame counts: the title screen's attract timer (125) and the copyright screen's display timer
// (250 = 4*60 + 10).
constexpr std::uint8_t kTitleTimer = 125;
constexpr std::uint8_t kCopyrightTimer = 250;

// The attract counter seeds: how many title-timer expiries pass before a demo launches. Four when a demo
// just ended (the attract chain continues), nineteen on a cold entry (no demo has run yet).
constexpr std::uint8_t kAttractBetweenDemos = 4;
constexpr std::uint8_t kAttractColdEntry = 19;

// Heart mode is read only as zero / non-zero (docs/contracts/game-state-machine-state.md), so the port
// stores a canonical non-zero flag rather than reconstructing the original's raw held-joypad byte.
constexpr std::uint8_t kHeartModeEnabled = 1;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

bool held(const GameContext& game, Action action) {
    return game.joypad.held.test(retropp::actionId(action));
}

// Call_26A9 (tetris.asm:6255-6264): draw the border tile down one board column, all 32 rows.
void fillWallColumn(PlayingFieldState& field, std::size_t col) {
    for (std::size_t row = 0; row < kBoardRows; ++row) {
        field.board[row][col] = kFieldBorderTile;
    }
}

// Fill consecutive object entries with one glyph each, left to right from `col`. Returns the entry
// after the last one written.
std::size_t drawTextObjects(GameContext& game, std::size_t entry, std::uint8_t y, std::size_t col,
                            std::string_view text) {
    const auto glyphs = encodeCharmapText(text);
    if (!glyphs) {
        return entry;
    }
    for (std::size_t i = 0; i < glyphs->size(); ++i, ++entry) {
        OamEntry& object = game.engine.oam[entry];
        object.y    = y;
        object.x    = static_cast<std::uint8_t>((col + i) * 8 + kObjectOriginX);
        object.tile = static_cast<std::uint8_t>((*glyphs)[i]);
    }
    return entry;
}

// One bottom-row item: the word, then the line under it, one object per cell of each.
std::size_t drawBottomItem(GameContext& game, std::size_t entry, std::size_t col,
                           std::string_view word) {
    entry = drawTextObjects(game, entry, kSettingsTextY, col, word);

    for (std::size_t i = 0; i < word.size(); ++i, ++entry) {
        OamEntry& object = game.engine.oam[entry];
        object.y    = kSettingsLineY;
        object.x    = static_cast<std::uint8_t>((col + i) * 8 + kObjectOriginX);
        object.tile = kUnderlineTile;
    }
    return entry;
}

// The bottom row: the settings item alone, centred where it has always been, or that item and the
// stats item under the two player-count columns.
std::size_t drawBottomRow(GameContext& game, std::size_t entry, bool twoItems) {
    if (!twoItems) {
        return drawBottomItem(game, entry, kSettingsTextCol, kSettingsWord);
    }
    entry = drawBottomItem(game, entry, kBottomLeftTextCol, kSettingsWord);
    return drawBottomItem(game, entry, kBottomRightTextCol, kStatsWord);
}

// The copyright line, redrawn from the stored screen's own row at the pixel height that leaves a
// margin below it. The tiles are read out of that row rather than written out here, so the notice is
// the cartridge's own art wherever it ends up being drawn; the blank cells of the row are skipped,
// since an object is only needed where there is something to see.
std::size_t drawCopyrightLine(GameContext& game, std::size_t entry) {
    const auto blank = static_cast<std::uint8_t>(CharTile::SPACE);
    for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
        const std::uint8_t tile = kTitleScreenTilemap[kCopyrightRow][col];
        if (tile == blank) {
            continue;
        }
        OamEntry& object = game.engine.oam[entry++];
        object.y    = kCopyrightY;
        object.x    = static_cast<std::uint8_t>(col * 8 + kObjectOriginX);
        object.tile = tile;
    }
    return entry;
}

// The whole run of objects below the player options: the bottom row and the copyright line under it.
//
// Every entry from the first is emptied before the run is written, because the two layouts are
// different lengths - dropping from two items to one would otherwise leave the second item's letters
// behind, since the shorter run stops before reaching them.
void drawTitleObjects(GameContext& game, bool twoItems) {
    for (std::size_t entry = kSettingsFirstObject; entry < game.engine.oam.size(); ++entry) {
        game.engine.oam[entry] = OamEntry{};
    }
    drawCopyrightLine(game, drawBottomRow(game, kSettingsFirstObject, twoItems));
}

// Put the selector where the current choice is. On the player-count row that is the original's own
// placement (.updateCursor, tetris.asm:710-718), where the one/two-player flag doubles as the cursor
// index; on the bottom row it is the one fixed place below it, or one of the two player columns once
// that row holds two items.
void placeTitleCursor(GameContext& game, bool twoItems) {
    if (game.screens.titleSettingsSelected) {
        game.engine.oam[0].y = kSettingsCursorY;
        if (!twoItems) {
            game.engine.oam[0].x = kSettingsCursorX;
            return;
        }
        game.engine.oam[0].x = game.screens.titleStatsColumn ? kCursorX2P : kCursorX1P;
        return;
    }
    game.engine.oam[0].y = kTitleCursorY;
    game.engine.oam[0].x = game.multiplayer.isMultiplayer ? kCursorX2P : kCursorX1P;
}

// The title screen's .updateCursor: store the one/two-player flag and place the selector.
void setTitleCursor(GameContext& game, bool multiplayer, bool twoItems) {
    game.multiplayer.isMultiplayer = multiplayer;
    placeTitleCursor(game, twoItems);
}

// Move between the player-count row and the bottom row, leaving the player count where the player
// left it - and, coming back down, leaving the bottom row's own column where they left that too.
// Returns whether the cursor moved.
bool setTitleSettingsSelected(GameContext& game, bool selected, bool twoItems) {
    if (game.screens.titleSettingsSelected == selected) {
        return false;
    }
    game.screens.titleSettingsSelected = selected;
    placeTitleCursor(game, twoItems);
    return true;
}

// Move between the bottom row's two items. Nothing happens at either end, which is the law the row
// above it follows: left from the one-player option and right from the two-player one both stay put.
void setTitleStatsColumn(GameContext& game, bool stats) {
    if (game.screens.titleStatsColumn == stats) {
        return;
    }
    game.screens.titleStatsColumn = stats;
    placeTitleCursor(game, /*twoItems=*/true);
}

}  // namespace

void initCopyrightScreen(GameContext& game) {
    // GameState_24 (tetris.asm:479-500). The LCD toggle is render mechanism (:480, :494-495).
    loadTileSheet(game.display, TileSheet::COPYRIGHT_TITLE);              // (:481)
    loadScreenTilemap(game.display, kCopyrightScreenTilemap);               // (:482-483)
    clearOamObjects(game);  // ClearObjects (:484)

    // Seed the piece ring from the demo list (:485-493). The original loops until the write pointer crosses
    // $C400, copying 256 bytes from the 48-entry DemoPieceList and over-reading 208 bytes past it. That
    // tail is never read (docs/contracts/title-screens.md), so the port copies the 48 real entries and
    // leaves the rest of the ring untouched.
    std::copy(kDemoPieceList.begin(), kDemoPieceList.end(), game.engine.pieceList.begin());

    game.flow.timer1 = kCopyrightTimer;                 // 250 (:496)
    game.flow.gameState = GameState::COPYRIGHT_SCREEN;  // (:498-499)
}

void copyrightHold(GameContext& game) {
    // GameState_25 (tetris.asm:502-510): hold while the frame timer counts down (the dispatcher decrements
    // it each frame), then re-arm the timer and advance to the skippable screen.
    if (game.flow.timer1 != 0) {
        return;
    }
    game.flow.timer1 = kCopyrightTimer;                          // 250 (:506)
    game.flow.gameState = GameState::COPYRIGHT_SCREEN_SKIPPABLE;  // (:508-509)
}

void copyrightSkippable(GameContext& game) {
    // GameState_35 (tetris.asm:512-522): any newly-pressed input, or the frame timer expiring, advances to
    // the title-screen init. Every physical button binds to at least one action, so a non-empty pressed
    // set matches the original's non-zero hJoyPressed test; the check reads the pressed edge, not the held
    // level.
    if (game.joypad.pressed.bits() != 0 || game.flow.timer1 == 0) {
        game.flow.gameState = GameState::INIT_TITLE_SCREEN;  // $06
    }
}

void initTitleScreen(GameContext& game, const ShowStatsQuery& showStats) {
    // GameState_06 (tetris.asm:524-580). Reset leftover state from a prior round, paint the title board,
    // stamp the title screen over it, seed the cursor, and arm the attract countdown. The LCD toggles are
    // render mechanism (:525, :567-568).
    loadTileSheet(game.display, TileSheet::COPYRIGHT_TITLE);  // (:537)

    // Field and pointer clears (:526-534). Each raw operand resolves to one owner via the HRAM census
    // (docs/contracts/game-state-machine-state.md). $FF9B is a dead byte with no port field, so it is
    // absent here; $FF9F is the hLines high byte, and the port clears the whole decimal `lines` field (the
    // low byte is unobservable before the next game-init rewrites it — see the contract).
    game.demo.recording = 0;              // hDemoRecording
    game.flow.pieceLockStage = 0;         // $FF98 — leftover lock state
    game.flow.blinkCounter = 0;           // $FF9C
    game.flow.topOutLockCount = 0;        // hTopScorePointerHi ($FFFB)
    game.flow.lines = 0;                  // $FF9F (hLines high byte)
    game.flow.wipeCounter = 0;            // hWipeCounter
    game.highScores.newTopScore = false;  // hNewTopScore
    clearLineClearsList(game);            // ClearLineClearsList (:535)
    clearScoreAndStats(game);             // ClearScoreAndStats (:536)

    // Board paint (:538-555): fill the whole 32x32 page with the empty tile, then the two wall columns
    // (board columns 1 and 12, bracketing the 10-wide visible field) and the floor (row 18, columns 1-12).
    const auto empty = static_cast<std::uint8_t>(CharTile::SPACE);
    for (auto& row : game.field.board) {
        row.fill(empty);
    }
    fillWallColumn(game.field, 1);   // $C801 — left wall
    fillWallColumn(game.field, 12);  // $C80C — right wall
    for (std::size_t col = 1; col <= 12; ++col) {
        game.field.board[18][col] = kFieldBorderTile;  // $CA41 — floor row
    }

    // The title screen itself (:556-557), stamped over the board paint above. The original writes the
    // two to different places — the paint to the board, the screen to the background map — and only
    // brings the board forward later, one row per frame, as the wipe runs. Here they are one grid, so
    // the screen covers the paint in the visible region and the paint survives outside it (the floor
    // row, and the wall columns past the screen's 20). See docs/contracts/screen.md.
    loadScreenTilemap(game.display, kTitleScreenTilemap);

    // Empty the copyright line's cell. Both it and the third item are redrawn as objects below, at
    // pixel heights no cell can reach.
    for (std::size_t col = 0; col < kTilemapScreenCols; ++col) {
        game.display.map[kCopyrightRow][col] = static_cast<std::uint8_t>(CharTile::SPACE);
    }

    // The 1P/2P selector cursor (:558-564): clear the object buffer, then seed OAM object 0. The
    // cursor starts on the player-count row whatever it was on when the player last left the screen.
    clearOamObjects(game);
    game.screens.titleSettingsSelected = false;
    game.engine.oam[0].y = kTitleCursorY;
    game.engine.oam[0].x = kCursorX1P;
    game.engine.oam[0].tile = kTitleCursorTile;

    // The port's own bottom row, over the blank band the player options' underline leaves, and then
    // the copyright line beneath it.
    drawTitleObjects(game, showStats ? showStats() : false);

    game.audioCues.music = MusicId::TITLE;  // wNewMusicID = 3 (:565-566)

    game.flow.gameState = GameState::TITLE_SCREEN;  // (:569-570)
    game.flow.timer1 = kTitleTimer;                 // 125 (:571-572)

    // Attract countdown seed (:573-579).
    game.flow.coarseCountdown = kAttractBetweenDemos;
    if (game.demo.activeDemo == ActiveDemo::NONE) {
        game.flow.coarseCountdown = kAttractColdEntry;
    }
}

void titleScreen(GameContext& game, const StartDemoHook& startDemo,
                 const ShowStatsQuery& showStats) {
    // GameState_07 (tetris.asm:632-731). Three concerns each frame: the attract countdown, the deferred
    // serial poll, and the cursor / input.

    // Whether the bottom row holds two items. Asked every frame rather than at the init, because the
    // settings screen is where it changes and that screen hands the title screen's own objects back
    // as it found them - so a row drawn once would still be the row the player left.
    const bool twoItems = showStats && showStats();

    // Attract countdown (:633-640): when the frame timer expires, count down the attract counter; at zero
    // launch the demo, otherwise re-arm the timer. Decrementing a counter already at zero wraps to 255, so
    // the demo does not launch that frame.
    if (game.flow.timer1 == 0) {
        if (--game.flow.coarseCountdown == 0) {
            if (startDemo) {
                startDemo(game);  // StartDemo (:582-630) — filled by the demo system
            }
            return;
        }
        game.flow.timer1 = kTitleTimer;
    }

    // Serial poll (:642-655): the slave-mode link-cable check that launches a peer-initiated two-player
    // game or resets the cursor. Link-cable mechanism the serial system owns; nothing sets its trigger
    // without that system, so it has no effect here.

    // The port's own row sits below the player-count pair, and its objects are laid down again each
    // frame so that the row follows the setting rather than whatever it was drawn as.
    drawTitleObjects(game, twoItems);

    // The bottom row's cursor is placed again with them, because where it sits depends on how many
    // items the row holds. The player-count cursor above is left alone: its position depends only on
    // a flag that nothing but the branches below can change, and re-placing it every frame would
    // make a press that moves nothing indistinguishable from one that does.
    if (game.screens.titleSettingsSelected) {
        placeTitleCursor(game, twoItems);
    }

    // Up and down move between the two rows, and the pair's own left/right/select laws apply only
    // while the cursor is on it.
    if (pressed(game, Action::MenuDown)) {
        setTitleSettingsSelected(game, true, twoItems);
        return;
    }
    if (pressed(game, Action::MenuUp)) {
        setTitleSettingsSelected(game, false, twoItems);
        return;
    }
    if (game.screens.titleSettingsSelected) {
        if (twoItems) {
            if (pressed(game, Action::MenuRight)) {
                setTitleStatsColumn(game, true);
                return;
            }
            if (pressed(game, Action::MenuLeft)) {
                setTitleStatsColumn(game, false);
                return;
            }
        }
        if (pressed(game, Action::Start) || pressed(game, Action::Confirm)) {
            if (twoItems && game.screens.titleStatsColumn) {
                openStatsMenu(game);
            } else {
                openSettings(game);
            }
        }
        return;
    }

    // Cursor / input (:657-731): isMultiplayer doubles as the cursor index. Buttons are tested in this
    // order; the first match handles the frame.
    if (pressed(game, Action::Select)) {  // (:661-662, :708-718)
        setTitleCursor(game, !game.multiplayer.isMultiplayer, twoItems);
        return;
    }
    if (pressed(game, Action::MenuRight)) {  // (:663-664, :720-724)
        if (!game.multiplayer.isMultiplayer) {
            setTitleCursor(game, true, twoItems);  // 1P -> 2P only
        }
        return;
    }
    if (pressed(game, Action::MenuLeft)) {  // (:665-666, :726-731)
        if (game.multiplayer.isMultiplayer) {
            setTitleCursor(game, false, twoItems);  // 2P -> 1P only
        }
        return;
    }
    // The cartridge acts on Start alone here (:667-668). The port takes A as well, so one button
    // acts on all three items instead of on the third one only.
    if (!pressed(game, Action::Start) && !pressed(game, Action::Confirm)) {
        return;
    }
    if (game.multiplayer.isMultiplayer) {
        // Two-player Start (:672-687): the master-election serial handshake. Link-cable mechanism the
        // serial system owns — no action here.
        return;
    }

    // One-player Start (:698-706): latch heart mode if Down (SoftDrop) is held, then enter the config
    // screen with the entry zeroing (.nextState, :688-696).
    if (held(game, Action::SoftDrop)) {
        game.flow.heartMode = kHeartModeEnabled;  // Down held (:700-701)
    }
    game.flow.gameState = GameState::INIT_TYPE_SELECTION;  // $08
    game.flow.timer1 = 0;
    game.flow.typeALevel = 0;
    game.flow.typeBLevel = 0;
    game.flow.typeBStartHeight = 0;
    game.demo.activeDemo = ActiveDemo::NONE;
}

void installTitleScreenHandlers(GameStateDispatcher& dispatcher, StartDemoHook startDemo,
                                ShowStatsQuery showStats) {
    dispatcher.setHandler(GameState::INIT_COPYRIGHT, initCopyrightScreen);
    dispatcher.setHandler(GameState::COPYRIGHT_SCREEN, copyrightHold);
    dispatcher.setHandler(GameState::COPYRIGHT_SCREEN_SKIPPABLE, copyrightSkippable);
    dispatcher.setHandler(GameState::INIT_TITLE_SCREEN, [showStats](GameContext& g) {
        initTitleScreen(g, showStats);
    });
    dispatcher.setHandler(GameState::TITLE_SCREEN,
                          [startDemo = std::move(startDemo),
                           showStats = std::move(showStats)](GameContext& g) {
                              titleScreen(g, startDemo, showStats);
                          });
}

}  // namespace kirpich::systems
