// Menu screens — behavioral tests against docs/contracts/menu-screens.md.
//
// Device-free: the eight state handlers and their helpers are pure logic over the game-state
// aggregate. Every asserted value is traced to the tetris.asm lines named in the contract. Blink is
// suppressed in the input-law tests by seeding a nonzero frame timer, so a handler's own cursor writes
// are asserted cleanly; the blink law itself is exercised separately through the frame dispatcher.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include <kirpich/action.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/sprite_id.h>

#include "data/misc.h"  // musicTypeSpriteCoordinate, cursor coordinate tables
#include "data/sfx.h"   // SquareSfxId
#include "retropp/input.h"
#include "state/engine_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"        // heldActions
#include "systems/menu_screens.h"

namespace {

using kirpich::Action;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::MusicId;
using kirpich::MusicType;
using kirpich::OamEntry;
using kirpich::SpriteId;
using kirpich::SquareSfxId;
using kirpich::kTypeALevelCursorCoordinates;
using kirpich::kTypeBLevelCursorCoordinates;
using kirpich::kTypeBStartHeightCursorCoordinates;
using kirpich::musicTypeSpriteCoordinate;
using kirpich::systems::GameContext;
using kirpich::systems::GameStateDispatcher;

// Constants used across the tests.
constexpr std::uint8_t kBlinkReload = 16;
constexpr std::uint8_t kDigitSpriteBase = static_cast<std::uint8_t>(SpriteId::DIGIT_0);  // $20

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// Present a pressed set to a handler (held mirrors it; menu handlers read only pressed).
void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held = game.joypad.pressed;
}

// A fresh context with the blink suppressed (nonzero frame timer) — the default for input-law tests.
GameContext menuContext() {
    GameContext game;
    game.flow.timer1 = 5;  // blinkCursor is a no-op while timer1 != 0
    return game;
}

}  // namespace

// ── Test 1: ConfigInitVectors ─────────────────────────────────────────────────────────────────────
// GameState_08 .loadTiles (tetris.asm:3121-3148): object-buffer clear, the two cursors, the music and
// game-type cursor placement, the music cue, and the transition into game-type selection. The config
// state adds only the serial-register reset (mechanism, no sim effect), so initConfigScreen matches
// loadConfigScreenBody.
TEST(MenuScreens, ConfigInitVectors) {
    auto check = [](GameType gt, MusicType mt) {
        GameContext game;
        game.flow.gameType = gt;
        game.flow.musicType = mt;
        game.engine.oam[5] = OamEntry{.y = 0x11};  // dirty a slot to prove the clear

        kirpich::systems::loadConfigScreenBody(game);

        // Object buffer cleared.
        EXPECT_EQ(game.engine.oam[5], OamEntry{});
        EXPECT_EQ(game.engine.oam[0], OamEntry{});

        // Music cursor (slot 0): coordinate + sprite from the music type.
        const auto coord = musicTypeSpriteCoordinate(mt);
        EXPECT_EQ(game.spriteRenderer.slots[0].y, coord.y);
        EXPECT_EQ(game.spriteRenderer.slots[0].x, coord.x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId,
                  static_cast<SpriteId>(static_cast<std::uint8_t>(mt)));

        // Game-type cursor (slot 1): X is the game-type value, sprite is the matching label.
        EXPECT_EQ(game.spriteRenderer.slots[1].x, static_cast<std::uint8_t>(gt));
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId,
                  gt == GameType::TYPE_A ? SpriteId::A_TYPE : SpriteId::B_TYPE);

        // Terminator: slot 2 hidden.
        EXPECT_TRUE(game.spriteRenderer.slots[2].hidden);

        // Cues: the menu-move sound (from positionMusicTypeSprite's top entry) and the switched music.
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        const auto expected = static_cast<std::uint8_t>(static_cast<std::uint8_t>(mt) - 0x17);
        EXPECT_EQ(game.audioCues.music,
                  expected == 0x08 ? MusicId::STOP : static_cast<MusicId>(expected));

        // Transition.
        EXPECT_EQ(game.flow.gameState, GameState::SELECT_GAME_TYPE);
    };

    check(GameType::TYPE_A, MusicType::MUSIC_A);
    check(GameType::TYPE_B, MusicType::MUSIC_B);
    check(GameType::TYPE_A, MusicType::MUSIC_C);
    check(GameType::TYPE_B, MusicType::OFF);  // music off -> STOP cue

    // The config state itself: identical sim effect to the body (serial reset has none).
    GameContext viaState;
    viaState.flow.gameType = GameType::TYPE_A;
    viaState.flow.musicType = MusicType::MUSIC_A;
    GameContext viaBody = viaState;
    kirpich::systems::initConfigScreen(viaState);
    kirpich::systems::loadConfigScreenBody(viaBody);
    EXPECT_EQ(viaState, viaBody);
}

// ── Test 2: GameTypeSelectVectors ───────────────────────────────────────────────────────────────
// GameState_0E (tetris.asm:3258-3314): the game-type toggle, the cursor writes, the cues, the
// transitions, and up/down being ignored.
TEST(MenuScreens, GameTypeSelectVectors) {
    // Right: A -> B, sets cursor X / sprite and cues the menu-move sound.
    {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_A;
        press(game, {Action::MenuRight});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
        EXPECT_EQ(game.spriteRenderer.slots[1].x, static_cast<std::uint8_t>(GameType::TYPE_B));
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId, SpriteId::B_TYPE);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
    }
    // Right again at Type B: no change, no cue.
    {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_B;
        press(game, {Action::MenuRight});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
    // Left: B -> A.
    {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_B;
        press(game, {Action::MenuLeft});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
        EXPECT_EQ(game.spriteRenderer.slots[1].x, static_cast<std::uint8_t>(GameType::TYPE_A));
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId, SpriteId::A_TYPE);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
    }
    // Left again at Type A: no change.
    {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_A;
        press(game, {Action::MenuLeft});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
    // Confirm -> music selection, cursor unhidden.
    {
        GameContext game = menuContext();
        game.spriteRenderer.slots[1].hidden = true;
        press(game, {Action::Confirm});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameState, GameState::SELECT_MUSIC_TYPE);
        EXPECT_FALSE(game.spriteRenderer.slots[1].hidden);
    }
    // Start -> difficulty screen fork, change-screen cue, cursor unhidden.
    for (auto [gt, next] : std::array{std::pair{GameType::TYPE_A, GameState::INIT_TYPE_A_DIFFICULTY},
                                      std::pair{GameType::TYPE_B, GameState::INIT_TYPE_B_DIFFICULTY}}) {
        GameContext game = menuContext();
        game.flow.gameType = gt;
        game.spriteRenderer.slots[1].hidden = true;
        press(game, {Action::Start});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameState, next);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
        EXPECT_FALSE(game.spriteRenderer.slots[1].hidden);
    }
    // Up / down ignored.
    for (const Action dir : {Action::MenuUp, Action::MenuDown}) {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_A;
        press(game, {dir});
        kirpich::systems::selectGameType(game);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_A);
        EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);  // no transition
    }
}

// ── Test 3: MusicTypeSelectVectors ──────────────────────────────────────────────────────────────
// GameState_0F (tetris.asm:3181-3246): the 2x2 grid walk incl. every boundary no-op, the reposition /
// music / cue on change, the shared Start/Confirm transition, and the one- vs two-player Back.
TEST(MenuScreens, MusicTypeSelectVectors) {
    // Move helper: press dir from value, return the resulting music-type byte.
    auto moved = [](std::uint8_t start, Action dir) {
        GameContext game = menuContext();
        game.flow.musicType = static_cast<MusicType>(start);
        press(game, {dir});
        kirpich::systems::selectMusicType(game);
        return static_cast<std::uint8_t>(game.flow.musicType);
    };

    // The full grid ($1C $1D / $1E $1F): right/left/up/down expected results per cell.
    struct Row { std::uint8_t v, right, left, up, down; };
    for (const Row r : std::array<Row, 4>{{
             {0x1C, 0x1D, 0x1C, 0x1C, 0x1E},
             {0x1D, 0x1D, 0x1C, 0x1D, 0x1F},
             {0x1E, 0x1F, 0x1E, 0x1C, 0x1E},
             {0x1F, 0x1F, 0x1E, 0x1D, 0x1F},
         }}) {
        EXPECT_EQ(moved(r.v, Action::MenuRight), r.right) << "right from " << int(r.v);
        EXPECT_EQ(moved(r.v, Action::MenuLeft), r.left) << "left from " << int(r.v);
        EXPECT_EQ(moved(r.v, Action::MenuUp), r.up) << "up from " << int(r.v);
        EXPECT_EQ(moved(r.v, Action::MenuDown), r.down) << "down from " << int(r.v);
    }

    // On a move: reposition slot 0, cue the menu-move sound, and switch music (STOP for "off").
    {
        GameContext game = menuContext();
        game.flow.musicType = MusicType::MUSIC_B;  // $1D
        press(game, {Action::MenuDown});           // -> $1F (off)
        kirpich::systems::selectMusicType(game);
        EXPECT_EQ(game.flow.musicType, MusicType::OFF);
        const auto coord = musicTypeSpriteCoordinate(MusicType::OFF);
        EXPECT_EQ(game.spriteRenderer.slots[0].y, coord.y);
        EXPECT_EQ(game.spriteRenderer.slots[0].x, coord.x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId, SpriteId::OFF);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        EXPECT_EQ(game.audioCues.music, MusicId::STOP);
    }
    // A boundary no-op writes no cue and does not move.
    {
        GameContext game = menuContext();
        game.flow.musicType = MusicType::MUSIC_A;  // $1C
        press(game, {Action::MenuLeft});           // left edge — no move
        kirpich::systems::selectMusicType(game);
        EXPECT_EQ(game.flow.musicType, MusicType::MUSIC_A);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE);
    }
    // Start / Confirm share the advance path (per game type), unhiding slot 0.
    for (const Action go : {Action::Start, Action::Confirm}) {
        GameContext game = menuContext();
        game.flow.gameType = GameType::TYPE_B;
        game.spriteRenderer.slots[0].hidden = true;
        press(game, {go});
        kirpich::systems::selectMusicType(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_B_DIFFICULTY);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
        EXPECT_FALSE(game.spriteRenderer.slots[0].hidden);
    }
    // Back: one-player returns to game-type selection and unhides slot 0.
    {
        GameContext game = menuContext();
        game.flow.gameState = GameState::SELECT_MUSIC_TYPE;
        game.multiplayer.isMultiplayer = false;
        game.spriteRenderer.slots[0].hidden = true;
        press(game, {Action::Back});
        kirpich::systems::selectMusicType(game);
        EXPECT_EQ(game.flow.gameState, GameState::SELECT_GAME_TYPE);
        EXPECT_FALSE(game.spriteRenderer.slots[0].hidden);
    }
    // Back: two-player is inert (falls through; no direction set).
    {
        GameContext game = menuContext();
        game.flow.gameState = GameState::SELECT_MUSIC_TYPE;
        game.flow.musicType = MusicType::MUSIC_A;
        game.multiplayer.isMultiplayer = true;
        press(game, {Action::Back});
        kirpich::systems::selectMusicType(game);
        EXPECT_EQ(game.flow.gameState, GameState::SELECT_MUSIC_TYPE);  // no transition
        EXPECT_EQ(game.flow.musicType, MusicType::MUSIC_A);           // no move
    }
}

// ── Test 4: TypeALevelSelectVectors ─────────────────────────────────────────────────────────────
// GameState_11 (tetris.asm:3350-3400): the 2x5 grid over levels 0-9, updateDigitCursor effects, the
// refresh hook firing on change only, and the transitions (no cursor unhide).
TEST(MenuScreens, TypeALevelSelectVectors) {
    // Grid sweep: every level 0-9 x four directions, expected via the contract's grid law.
    for (std::uint8_t v = 0; v <= 9; ++v) {
        auto moved = [&](Action dir) {
            GameContext game = menuContext();
            game.flow.typeALevel = v;
            press(game, {dir});
            kirpich::systems::selectTypeALevel(game);
            return game.flow.typeALevel;
        };
        EXPECT_EQ(moved(Action::MenuRight), v == 9 ? 9 : v + 1) << "right " << int(v);
        EXPECT_EQ(moved(Action::MenuLeft), v == 0 ? 0 : v - 1) << "left " << int(v);
        EXPECT_EQ(moved(Action::MenuUp), v >= 5 ? v - 5 : v) << "up " << int(v);
        EXPECT_EQ(moved(Action::MenuDown), v < 5 ? v + 5 : v) << "down " << int(v);
    }

    // updateDigitCursor effects on a move + the refresh hook fires on change only.
    {
        int refreshes = 0;
        auto probe = [&](GameContext&) { ++refreshes; };
        GameContext game = menuContext();
        game.flow.typeALevel = 0;
        press(game, {Action::MenuRight});  // -> 1
        kirpich::systems::selectTypeALevel(game, probe);
        EXPECT_EQ(game.flow.typeALevel, 1);
        EXPECT_EQ(game.spriteRenderer.slots[0].y, kTypeALevelCursorCoordinates[1].y);
        EXPECT_EQ(game.spriteRenderer.slots[0].x, kTypeALevelCursorCoordinates[1].x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 1));  // DIGIT_1
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        EXPECT_EQ(refreshes, 1);

        // No-move press (right at 9): the hook does not fire.
        refreshes = 0;
        GameContext capped = menuContext();
        capped.flow.typeALevel = 9;
        press(capped, {Action::MenuRight});
        kirpich::systems::selectTypeALevel(capped, probe);
        EXPECT_EQ(refreshes, 0);
    }

    // Start and Confirm begin the game; Back returns to the config screen — none unhide the cursor.
    for (const Action go : {Action::Start, Action::Confirm}) {
        GameContext game = menuContext();
        game.spriteRenderer.slots[0].hidden = true;
        press(game, {go});
        kirpich::systems::selectTypeALevel(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_GAME);
        EXPECT_TRUE(game.spriteRenderer.slots[0].hidden);  // asymmetry: no unhide
    }
    {
        GameContext game = menuContext();
        game.spriteRenderer.slots[0].hidden = true;
        press(game, {Action::Back});
        kirpich::systems::selectTypeALevel(game);
        EXPECT_EQ(game.flow.gameState, GameState::INIT_TYPE_SELECTION);
        EXPECT_TRUE(game.spriteRenderer.slots[0].hidden);
    }
}

// ── Test 5: TypeBSelectVectors ──────────────────────────────────────────────────────────────────
// GameState_13 + GameState_14 (tetris.asm:3450-3564): both grids, the level->height->game
// transitions, the cursor-unhiding helper on the right slot, and the coordinate tables.
TEST(MenuScreens, TypeBSelectVectors) {
    // Type B level grid (slot 0), 0-9.
    for (std::uint8_t v = 0; v <= 9; ++v) {
        auto moved = [&](Action dir) {
            GameContext game = menuContext();
            game.flow.typeBLevel = v;
            press(game, {dir});
            kirpich::systems::selectTypeBLevel(game);
            return game.flow.typeBLevel;
        };
        EXPECT_EQ(moved(Action::MenuRight), v == 9 ? 9 : v + 1) << "B level right " << int(v);
        EXPECT_EQ(moved(Action::MenuLeft), v == 0 ? 0 : v - 1) << "B level left " << int(v);
        EXPECT_EQ(moved(Action::MenuUp), v >= 5 ? v - 5 : v) << "B level up " << int(v);
        EXPECT_EQ(moved(Action::MenuDown), v < 5 ? v + 5 : v) << "B level down " << int(v);
    }
    // Type B height grid (slot 1), 0-5.
    for (std::uint8_t v = 0; v <= 5; ++v) {
        auto moved = [&](Action dir) {
            GameContext game = menuContext();
            game.flow.typeBStartHeight = v;
            press(game, {dir});
            kirpich::systems::selectTypeBHeight(game);
            return game.flow.typeBStartHeight;
        };
        EXPECT_EQ(moved(Action::MenuRight), v == 5 ? 5 : v + 1) << "B height right " << int(v);
        EXPECT_EQ(moved(Action::MenuLeft), v == 0 ? 0 : v - 1) << "B height left " << int(v);
        EXPECT_EQ(moved(Action::MenuUp), v >= 3 ? v - 3 : v) << "B height up " << int(v);
        EXPECT_EQ(moved(Action::MenuDown), v < 3 ? v + 3 : v) << "B height down " << int(v);
    }

    // Coordinate + digit-sprite wiring on a level move (Data_16D2).
    {
        GameContext game = menuContext();
        game.flow.typeBLevel = 3;
        press(game, {Action::MenuRight});  // -> 4
        kirpich::systems::selectTypeBLevel(game);
        EXPECT_EQ(game.spriteRenderer.slots[0].x, kTypeBLevelCursorCoordinates[4].x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 4));
    }
    // Coordinate wiring on a height move (Data_1741, slot 1).
    {
        GameContext game = menuContext();
        game.flow.typeBStartHeight = 1;
        press(game, {Action::MenuRight});  // -> 2
        kirpich::systems::selectTypeBHeight(game);
        EXPECT_EQ(game.spriteRenderer.slots[1].x, kTypeBStartHeightCursorCoordinates[2].x);
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 2));
    }

    // $13 transitions, each unhiding slot 0.
    struct T { Action act; GameState next; };
    for (const T t : std::array<T, 3>{{{Action::Start, GameState::INIT_GAME},
                                       {Action::Confirm, GameState::TYPE_B_HEIGHT_SELECTION},
                                       {Action::Back, GameState::INIT_TYPE_SELECTION}}}) {
        GameContext game = menuContext();
        game.spriteRenderer.slots[0].hidden = true;
        press(game, {t.act});
        kirpich::systems::selectTypeBLevel(game);
        EXPECT_EQ(game.flow.gameState, t.next);
        EXPECT_FALSE(game.spriteRenderer.slots[0].hidden);
    }
    // $14 transitions, each unhiding slot 1.
    for (const T t : std::array<T, 3>{{{Action::Start, GameState::INIT_GAME},
                                       {Action::Confirm, GameState::INIT_GAME},
                                       {Action::Back, GameState::TYPE_B_LEVEL_SELECTION}}}) {
        GameContext game = menuContext();
        game.spriteRenderer.slots[1].hidden = true;
        press(game, {t.act});
        kirpich::systems::selectTypeBHeight(game);
        EXPECT_EQ(game.flow.gameState, t.next);
        EXPECT_FALSE(game.spriteRenderer.slots[1].hidden);
    }
}

// ── Test 6: DifficultyInitVectors ───────────────────────────────────────────────────────────────
// GameState_10 / GameState_12 (tetris.asm:3317-3441): sprite loads, cursor seeding (both Type B
// cursors), the menu-move cue on entry (the init screens enter updateDigitCursor at its top), the
// refresh hook, and the new-top-score fork.
TEST(MenuScreens, DifficultyInitVectors) {
    // Type A init: one cursor loaded from Data_26DB, seeded at the current level, cue + refresh, -> $11.
    {
        int refreshes = 0;
        auto probe = [&](GameContext&) { ++refreshes; };
        GameContext game;
        game.flow.typeALevel = 6;
        game.engine.oam[3] = OamEntry{.y = 0x22};
        kirpich::systems::initTypeADifficultyScreen(game, probe);
        EXPECT_EQ(game.engine.oam[3], OamEntry{});  // object buffer cleared
        EXPECT_EQ(game.spriteRenderer.slots[0].x, kTypeALevelCursorCoordinates[6].x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 6));
        EXPECT_TRUE(game.spriteRenderer.slots[1].hidden);  // one sprite + terminator hides slot 1
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);  // init enters UpdateDigitCursor at its top
        EXPECT_EQ(refreshes, 1);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_A_LEVEL_SELECTION);
    }
    // Type B init: two cursors seeded (level slot 0, height slot 1), cue + refresh, -> $13.
    {
        int refreshes = 0;
        auto probe = [&](GameContext&) { ++refreshes; };
        GameContext game;
        game.flow.typeBLevel = 2;
        game.flow.typeBStartHeight = 4;
        kirpich::systems::initTypeBDifficultyScreen(game, probe);
        EXPECT_EQ(game.spriteRenderer.slots[0].x, kTypeBLevelCursorCoordinates[2].x);
        EXPECT_EQ(game.spriteRenderer.slots[0].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 2));
        EXPECT_EQ(game.spriteRenderer.slots[1].x, kTypeBStartHeightCursorCoordinates[4].x);
        EXPECT_EQ(game.spriteRenderer.slots[1].spriteId,
                  static_cast<SpriteId>(kDigitSpriteBase + 4));
        EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        EXPECT_EQ(refreshes, 1);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_B_LEVEL_SELECTION);
    }
    // New-top-score fork routes both inits to name entry instead.
    for (const bool typeB : {false, true}) {
        GameContext game;
        game.highScores.newTopScore = true;
        if (typeB) {
            kirpich::systems::initTypeBDifficultyScreen(game);
        } else {
            kirpich::systems::initTypeADifficultyScreen(game);
        }
        EXPECT_EQ(game.flow.gameState, GameState::ENTER_TOP_SCORE);
    }
    // No top score: the init switches music (a song cue is written).
    {
        GameContext game;
        game.flow.musicType = MusicType::MUSIC_A;
        game.highScores.newTopScore = false;
        kirpich::systems::initTypeADifficultyScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_A_LEVEL_SELECTION);
        EXPECT_EQ(game.audioCues.music, MusicId::TYPE_A);  // $1C - $17 = 5
    }
}

// ── Test 7: BlinkAndActionRows ──────────────────────────────────────────────────────────────────
// The blink law (timer-gated toggle, 16-reload, XOR semantics), the installer covering exactly the
// eight selection slots, and the menu-action held-set adapter.
TEST(MenuScreens, BlinkAndActionRows) {
    // Blink unit law: gated on timer1, reloads 16, toggles (XOR) — a no-op while the timer counts.
    {
        GameContext game;  // timer1 == 0, cursor visible
        kirpich::systems::blinkCursor(game, 0);
        EXPECT_EQ(game.flow.timer1, kBlinkReload);
        EXPECT_TRUE(game.spriteRenderer.slots[0].hidden);
        kirpich::systems::blinkCursor(game, 0);  // timer still counting -> no-op
        EXPECT_EQ(game.flow.timer1, kBlinkReload);
        EXPECT_TRUE(game.spriteRenderer.slots[0].hidden);
        game.flow.timer1 = 0;
        kirpich::systems::blinkCursor(game, 0);  // toggles back
        EXPECT_FALSE(game.spriteRenderer.slots[0].hidden);
    }

    // Blink composes with the frame dispatcher's timer decrement: the cursor toggles once per 16-frame
    // cycle. Drive game-type selection (slot 1) with no input.
    {
        GameStateDispatcher dispatcher;
        kirpich::systems::installMenuScreenHandlers(dispatcher);
        GameContext game;
        game.flow.gameState = GameState::SELECT_GAME_TYPE;  // timer1 starts at 0
        const retropp::ActionSet none;

        dispatcher.tick(game, none);  // timer1 0 -> blink toggles hidden, reload 16, decrement -> 15
        EXPECT_TRUE(game.spriteRenderer.slots[1].hidden);
        for (int i = 0; i < 15; ++i) {
            dispatcher.tick(game, none);  // counts 15 -> 0 with no blink
        }
        EXPECT_TRUE(game.spriteRenderer.slots[1].hidden);
        dispatcher.tick(game, none);  // timer1 0 again -> blink toggles back
        EXPECT_FALSE(game.spriteRenderer.slots[1].hidden);
    }

    // The installer wires the eight selection handlers: a seeded tick reaches selectGameType.
    {
        GameStateDispatcher dispatcher;
        kirpich::systems::installMenuScreenHandlers(dispatcher);
        GameContext game;
        game.flow.gameState = GameState::SELECT_GAME_TYPE;
        retropp::ActionSet held;
        held.set(retropp::actionId(Action::MenuRight), true);
        dispatcher.tick(game, held);
        EXPECT_EQ(game.flow.gameType, GameType::TYPE_B);  // selectGameType ran

        // The bare $09 slot keeps its default stub: a tick there transitions nothing.
        GameContext idle;
        idle.flow.gameState = GameState::STATE_09_UNUSED;
        dispatcher.tick(idle, held);
        EXPECT_EQ(idle.flow.gameState, GameState::STATE_09_UNUSED);
        EXPECT_EQ(idle.flow.gameType, GameType{});  // untouched
    }

    // The held-set adapter carries the six menu actions.
    {
        retropp::InputState state;
        retropp::InputSample sample;
        sample.players[0].held = actionSet({Action::MenuUp, Action::MenuDown, Action::MenuLeft,
                                            Action::MenuRight, Action::Confirm, Action::Back});
        std::array<retropp::ActionSet, retropp::kMaxPlayers> pressed{};
        pressed[0] = sample.players[0].held;
        state.sampleTick(sample, pressed);
        EXPECT_EQ(kirpich::systems::heldActions(state),
                  actionSet({Action::MenuUp, Action::MenuDown, Action::MenuLeft, Action::MenuRight,
                             Action::Confirm, Action::Back}));
    }
}
