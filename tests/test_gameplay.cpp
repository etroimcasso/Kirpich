// Gameplay session — behavioral tests against docs/contracts/gameplay.md.
//
// Device-free: the seven state handlers and the pause family are pure logic over the game-state
// aggregate. Every asserted value is traced to the tetris.asm lines named in the contract. The piece
// randomizer is a deterministic stand-in so the pipeline is reproducible; the demo, garbage, and
// soft-reset seams are probes.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/serial_role.h>
#include <kirpich/sprite_id.h>

#include "data/gravity.h"
#include "data/music.h"
#include "data/playing_field.h"
#include "data/tilemaps.h"
#include "retropp/input.h"
#include "state/demo_state.h"
#include "state/playing_field_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/audio_cues.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"

namespace {

using kirpich::ActiveDemo;
using kirpich::Action;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::MusicId;
using kirpich::SerialRole;
using kirpich::SpriteId;
using kirpich::systems::AudioPauseCommand;
using kirpich::systems::GameContext;
using kirpich::systems::GameplayDemoHooks;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kCurtainTile = 0x87;
constexpr std::uint8_t kWipeStartStep = 2;
constexpr std::uint8_t kCurtainFrames = 70;
constexpr std::uint8_t kMultiplayerFrames = 63;
constexpr std::uint8_t kBonusFrames = 144;
constexpr std::uint8_t kScoreboardFrames = 5;
constexpr std::uint16_t kTypeBLines = 25;
constexpr std::uint8_t kTypeBDropTimer = 52;
constexpr std::uint8_t kUnpauseCommand = 0x94;
constexpr std::size_t kActive = kirpich::kActivePieceSlot;
constexpr std::size_t kPreview = kirpich::kPreviewPieceSlot;

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held = game.joypad.pressed;
}

void hold(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.held = actionSet(as);
    game.joypad.pressed = retropp::ActionSet{};
}

// A reproducible stand-in for the piece randomizer: a fixed cycle over the seven spawn orientations.
std::function<std::uint8_t()> cyclingDraw() {
    auto counter = std::make_shared<std::uint8_t>(0);
    return [counter]() -> std::uint8_t {
        const std::uint8_t v = static_cast<std::uint8_t>((*counter % 7) * 4);
        ++*counter;
        return v;
    };
}

// A game set up far enough that a round can be initialised.
GameContext readyToStart(GameType type, std::uint8_t level, std::uint8_t height = 0) {
    GameContext game;
    game.flow.gameType = type;
    game.flow.typeALevel = level;
    game.flow.typeBLevel = level;
    game.flow.typeBStartHeight = height;
    game.flow.gameState = GameState::INIT_GAME;
    return game;
}

}  // namespace

// ── Test 0: PausedScreenCarriesTheSettingsHint ──────────────────────────────────────────────────────
// The round init stamps the port's own hint into the paused screen's field area, which that screen
// leaves blank. It says which button opens the settings screen, because the cartridge has no such
// screen and so leaves no habit to lean on. It goes in the SECOND map only - the live map is the
// playing field, and a hint written there would be sitting in it.
TEST(Gameplay, PausedScreenCarriesTheSettingsHint) {
    using C = kirpich::CharTile;
    constexpr std::size_t kHintRow    = 14;
    constexpr std::size_t kButtonCol  = 4;  // "hit a"
    constexpr std::size_t kWordCol    = 3;  // "settings"

    for (const GameType type : {GameType::TYPE_A, GameType::TYPE_B}) {
        GameContext game = readyToStart(type, 0);
        kirpich::systems::initGame(game, cyclingDraw());

        const auto glyph = [&](std::size_t row, std::size_t col) {
            return game.display.secondMap[row][col];
        };
        const std::array<C, 5> button{C::LETTER_H, C::LETTER_I, C::LETTER_T, C::SPACE, C::LETTER_A};
        for (std::size_t i = 0; i < button.size(); ++i) {
            EXPECT_EQ(glyph(kHintRow, kButtonCol + i), static_cast<std::uint8_t>(button[i]))
                << "hint button cell " << i;
        }
        const std::array<C, 8> word{C::LETTER_S, C::LETTER_E, C::LETTER_T, C::LETTER_T,
                                    C::LETTER_I, C::LETTER_N, C::LETTER_G, C::LETTER_S};
        for (std::size_t i = 0; i < word.size(); ++i) {
            EXPECT_EQ(glyph(kHintRow + 1, kWordCol + i), static_cast<std::uint8_t>(word[i]))
                << "hint word cell " << i;
        }

        // The live map keeps the playing field there, not the hint.
        EXPECT_NE(game.display.map[kHintRow][kButtonCol], static_cast<std::uint8_t>(C::LETTER_H));
        EXPECT_NE(game.display.map[kHintRow + 1][kWordCol], static_cast<std::uint8_t>(C::LETTER_S));
    }
}

// ── Test 1: InitGameVectors ─────────────────────────────────────────────────────────────────────────
// GameState_0A (tetris.asm:4124-4238): the shared init, swept over both game types and heart mode.
TEST(Gameplay, InitGameVectors) {
    for (const GameType type : {GameType::TYPE_A, GameType::TYPE_B}) {
        for (const std::uint8_t heart : {std::uint8_t{0}, std::uint8_t{1}}) {
            for (const std::uint8_t level : {std::uint8_t{0}, std::uint8_t{5}, std::uint8_t{9}}) {
                GameContext game = readyToStart(type, level);
                game.flow.heartMode = heart;

                // Dirty every field the init is contracted to clear, so a missing clear is visible.
                game.flow.pieceLockStage = 3;
                game.flow.blinkCounter = 9;
                game.flow.topOutLockCount = 2;
                game.flow.lines = 4321;
                game.flow.completedRowCount = 4;
                game.engine.score = 123456;
                game.field.fieldCell(4, 4) = 0x81;

                kirpich::systems::initGame(game, cyclingDraw());

                EXPECT_EQ(game.flow.pieceLockStage, 0);
                EXPECT_EQ(game.flow.blinkCounter, 0);
                EXPECT_EQ(game.flow.topOutLockCount, 0);
                EXPECT_EQ(game.flow.completedRowCount, 0);
                EXPECT_EQ(game.engine.score, 0u);

                // The whole visible field is spaces (:4133-4134).
                for (std::size_t r = 0; r < kirpich::kPlayingFieldRows; ++r) {
                    for (std::size_t c = 0; c < kirpich::kPlayingFieldCols; ++c) {
                        ASSERT_EQ(game.field.fieldCell(r, c), kSpace) << "cell " << r << "," << c;
                    }
                }
                // The two rows below the field are cleared too (Call_1FF2, :5061-5075).
                for (std::size_t r = 30; r < 32; ++r) {
                    for (std::size_t c = 0; c < kirpich::kPlayingFieldCols; ++c) {
                        ASSERT_EQ(game.field.board[r][kirpich::kPlayingFieldOriginCol + c], kSpace);
                    }
                }

                // The fill armed the wipe and the init disarmed it (:4137-4138) — see contract section 3.
                EXPECT_EQ(game.flow.wipeCounter, 0);

                EXPECT_EQ(game.flow.level, level);
                EXPECT_EQ(game.flow.lines, type == GameType::TYPE_B ? kTypeBLines : 0);
                EXPECT_EQ(game.flow.framesPerDrop, kirpich::framesPerDrop(level, heart != 0));
                EXPECT_EQ(game.flow.dropTimer, type == GameType::TYPE_B
                                                   ? kTypeBDropTimer
                                                   : kirpich::framesPerDrop(level, heart != 0));
                EXPECT_EQ(game.flow.gameState, GameState::NORMAL_GAMEPLAY);
            }
        }
    }

    // The preview sprite follows the player's hide choice (:4197-4201).
    for (const bool hidden : {false, true}) {
        GameContext game = readyToStart(GameType::TYPE_A, 0);
        game.engine.hidePreviewPiece = hidden;
        kirpich::systems::initGame(game, cyclingDraw());
        EXPECT_EQ(game.spriteRenderer.slots[kPreview].hidden, hidden);
    }

    // Three draws fill the one-stage pipeline (:4203-4205).
    {
        GameContext game = readyToStart(GameType::TYPE_A, 0);
        auto inner = cyclingDraw();
        int draws = 0;
        kirpich::systems::initGame(game, [&]() -> std::uint8_t {
            ++draws;
            return inner();
        });
        // At least one draw per pipeline stage. The randomizer retries a draw it rejects, so the exact
        // count depends on what the stand-in returns; three is the floor, and zero or one would mean the
        // init never filled the pipeline.
        EXPECT_GE(draws, 3);
    }

    // Type B fires the garbage seam only when a start height was chosen (:4219-4232), and tells it
    // whether the fixed demo table applies (:4222-4225).
    {
        GameContext game = readyToStart(GameType::TYPE_B, 0, /*height=*/0);
        int calls = 0;
        kirpich::systems::initGame(game, cyclingDraw(),
                                   [&calls](GameContext&, std::uint8_t, bool) { ++calls; });
        EXPECT_EQ(calls, 0);
    }
    for (const ActiveDemo demo : {ActiveDemo::NONE, ActiveDemo::TYPE_B}) {
        GameContext game = readyToStart(GameType::TYPE_B, 0, /*height=*/3);
        game.demo.activeDemo = demo;
        std::uint8_t rows = 0;
        bool useDemoTable = false;
        int calls = 0;
        kirpich::systems::initGame(game, cyclingDraw(),
                                   [&](GameContext&, std::uint8_t r, bool d) {
                                       rows = r;
                                       useDemoTable = d;
                                       ++calls;
                                   });
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(rows, 3);
        EXPECT_EQ(useDemoTable, demo != ActiveDemo::NONE);
    }

    // Type A never fires it (:4209-4211).
    {
        GameContext game = readyToStart(GameType::TYPE_A, 0, /*height=*/3);
        int calls = 0;
        kirpich::systems::initGame(game, cyclingDraw(),
                                   [&calls](GameContext&, std::uint8_t, bool) { ++calls; });
        EXPECT_EQ(calls, 0);
    }
}

// ── Test 2: NormalGameplayBeatOrder ─────────────────────────────────────────────────────────────────
// GameState_00 (tetris.asm:4406-4421): the frame's twelve steps, their order, and the pause early-out.
TEST(Gameplay, NormalGameplayBeatOrder) {
    // The four demo seams fire in their contracted positions, around the piece work.
    {
        GameContext game;
        std::vector<std::string> order;
        GameplayDemoHooks demo;
        demo.checkForEndOfDemo = [&order](GameContext&) { order.emplace_back("checkForEndOfDemo"); };
        demo.simulateJoypad = [&order](GameContext&) { order.emplace_back("simulateJoypad"); };
        demo.recordDemo = [&order](GameContext&) { order.emplace_back("recordDemo"); };
        demo.restoreSavedJoypad = [&order](GameContext&) { order.emplace_back("restoreSavedJoypad"); };

        kirpich::systems::normalGameplay(game, demo);

        const std::vector<std::string> expected{"checkForEndOfDemo", "simulateJoypad", "recordDemo",
                                                "restoreSavedJoypad"};
        EXPECT_EQ(order, expected);
    }

    // Paused: step 2 stops the frame, so none of the demo seams run (:4408-4410).
    {
        GameContext game;
        game.flow.paused = true;
        int fired = 0;
        GameplayDemoHooks demo;
        demo.checkForEndOfDemo = [&fired](GameContext&) { ++fired; };
        demo.simulateJoypad = [&fired](GameContext&) { ++fired; };
        demo.recordDemo = [&fired](GameContext&) { ++fired; };
        demo.restoreSavedJoypad = [&fired](GameContext&) { ++fired; };

        kirpich::systems::normalGameplay(game, demo);
        EXPECT_EQ(fired, 0);
    }

    // Start unpauses through step 1, and the rest of that same frame then runs (:4407 before :4408).
    {
        GameContext game;
        game.flow.paused = true;
        press(game, {Action::Start});
        int fired = 0;
        GameplayDemoHooks demo;
        demo.recordDemo = [&fired](GameContext&) { ++fired; };

        kirpich::systems::normalGameplay(game, demo);
        EXPECT_FALSE(game.flow.paused);
        EXPECT_EQ(fired, 1);
    }

    // The piece steps really run inside an unpaused frame: gravity counts the drop timer down (:4415).
    // The steps after it are the piece, line-clear, and scoring systems' own contracts, exercised by
    // their own tests; what this asserts is that the frame composes them at all.
    {
        GameContext game;
        game.flow.dropTimer = 5;
        kirpich::systems::normalGameplay(game);
        EXPECT_EQ(game.flow.dropTimer, 4);
    }
    // Paused, the same frame leaves the drop timer alone.
    {
        GameContext game;
        game.flow.dropTimer = 5;
        game.flow.paused = true;
        kirpich::systems::normalGameplay(game);
        EXPECT_EQ(game.flow.dropTimer, 5);
    }

    // Absent hooks are skipped rather than called (every default is empty).
    {
        GameContext game;
        EXPECT_NO_THROW(kirpich::systems::normalGameplay(game));
    }
}

// ── Test 3: PauseVectors ────────────────────────────────────────────────────────────────────────────
// HandleStartSelect / handleSelect (tetris.asm:4423-4494): the chord, demo suppression, pause, preview.
TEST(Gameplay, PauseVectors) {
    // The soft-reset chord fires the seam and stops there (:4441-4444). The original gets there with a
    // jump, so the frame ends too — false is that reaching the caller.
    {
        GameContext game;
        hold(game, {Action::Start, Action::Select, Action::RotateClockwise,
                    Action::RotateCounterClockwise});
        int resets = 0;
        EXPECT_FALSE(kirpich::systems::handleStartSelect(game, [&resets]() { ++resets; }));
        EXPECT_EQ(resets, 1);
        EXPECT_FALSE(game.flow.paused);
    }
    // Three of the four is not the chord, and the frame carries on.
    {
        GameContext game;
        hold(game, {Action::Start, Action::Select, Action::RotateClockwise});
        int resets = 0;
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game, [&resets]() { ++resets; }));
        EXPECT_EQ(resets, 0);
    }

    // A running demo suppresses everything below the chord (:4445-4447).
    {
        GameContext game;
        game.demo.activeDemo = ActiveDemo::TYPE_A;
        press(game, {Action::Start});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_FALSE(game.flow.paused);
    }

    // Start pauses: the command goes out and both piece sprites hide (:4460-4483).
    {
        GameContext game;
        press(game, {Action::Start});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_TRUE(game.flow.paused);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::PAUSE);
        EXPECT_TRUE(game.spriteRenderer.slots[kActive].hidden);
        EXPECT_TRUE(game.spriteRenderer.slots[kPreview].hidden);
    }

    // Start again unpauses; the preview returns only if the player has not hidden it (:4486-4494).
    for (const bool playerHidPreview : {false, true}) {
        GameContext game;
        game.flow.paused = true;
        game.engine.hidePreviewPiece = playerHidPreview;
        game.spriteRenderer.slots[kActive].hidden = true;
        game.spriteRenderer.slots[kPreview].hidden = true;
        press(game, {Action::Start});

        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_FALSE(game.flow.paused);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::UNPAUSE);
        EXPECT_FALSE(game.spriteRenderer.slots[kActive].hidden);
        EXPECT_EQ(game.spriteRenderer.slots[kPreview].hidden, playerHidPreview);
    }

    // Select toggles the preview both ways (:4423-4438).
    {
        GameContext game;
        press(game, {Action::Select});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_TRUE(game.engine.hidePreviewPiece);
        EXPECT_TRUE(game.spriteRenderer.slots[kPreview].hidden);

        press(game, {Action::Select});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_FALSE(game.engine.hidePreviewPiece);
        EXPECT_FALSE(game.spriteRenderer.slots[kPreview].hidden);
    }

    // Neither button: nothing moves.
    {
        GameContext game;
        const GameContext before = game;
        press(game, {Action::MoveLeft});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_EQ(game.flow.paused, before.flow.paused);
        EXPECT_EQ(game.engine.hidePreviewPiece, before.engine.hidePreviewPiece);
    }
}

// ── Test 4: PauseMultiplayerVectors ─────────────────────────────────────────────────────────────────
// The two-player pause path (tetris.asm:4496-4559), including both preserved oddities.
TEST(Gameplay, PauseMultiplayerVectors) {
    // Only the master may pause (:4497-4499).
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.multiplayer.role = SerialRole::SLAVE;
        press(game, {Action::Start});
        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_FALSE(game.flow.paused);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::NONE);
    }

    // The master pauses and banks the serial buffers across it (:4504-4511).
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.multiplayer.role = SerialRole::MASTER;
        game.multiplayer.rx = 0x11;
        game.multiplayer.tx = 0x22;
        press(game, {Action::Start});

        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_TRUE(game.flow.paused);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::PAUSE);
        EXPECT_EQ(game.multiplayer.savedRx, 0x11);
        EXPECT_EQ(game.multiplayer.savedTx, 0x22);
    }

    // The master takes the short path out: pressing Start again restores and resumes immediately,
    // without waiting for the protocol (:4500-4503).
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.multiplayer.role = SerialRole::MASTER;
        game.flow.paused = true;
        game.multiplayer.savedRx = 0x33;
        game.multiplayer.savedTx = 0x44;
        press(game, {Action::Start});

        EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
        EXPECT_FALSE(game.flow.paused);
        EXPECT_EQ(game.multiplayer.rx, 0x33);
        EXPECT_EQ(game.multiplayer.tx, 0x44);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::UNPAUSE);
    }

    // Not paused: the routine reports that the caller may continue (:4516-4518).
    {
        GameContext game;
        EXPECT_FALSE(kirpich::systems::handlePausedMultiplayer(game));
    }

    // The serial-flag test can never be taken, so the routine proceeds and clears the flag even when it
    // is already zero (:4519-4522) — see contract section 5.
    {
        GameContext game;
        game.flow.paused = true;
        game.multiplayer.role = SerialRole::MASTER;
        game.multiplayer.transferCompleted = 0;

        EXPECT_TRUE(kirpich::systems::handlePausedMultiplayer(game));
        EXPECT_EQ(game.multiplayer.tx, kUnpauseCommand);
        EXPECT_EQ(game.multiplayer.sendPending, kUnpauseCommand);
    }

    // The master keeps sending the command and tells its caller to return (:4526-4530).
    {
        GameContext game;
        game.flow.paused = true;
        game.multiplayer.role = SerialRole::MASTER;
        game.multiplayer.transferCompleted = 1;

        EXPECT_TRUE(kirpich::systems::handlePausedMultiplayer(game));
        EXPECT_EQ(game.multiplayer.transferCompleted, 0);
        EXPECT_TRUE(game.flow.paused);
    }

    // The slave's test reads inverted against the command's name: reading the unpause command leaves it
    // paused, any other value unpauses it (:4532-4546) — preserved, see contract section 5.
    {
        GameContext game;
        game.flow.paused = true;
        game.multiplayer.role = SerialRole::SLAVE;
        game.multiplayer.transferCompleted = 1;
        game.multiplayer.rx = kUnpauseCommand;

        EXPECT_TRUE(kirpich::systems::handlePausedMultiplayer(game));
        EXPECT_TRUE(game.flow.paused);
        EXPECT_EQ(game.multiplayer.tx, 0);
    }
    {
        GameContext game;
        game.flow.paused = true;
        game.multiplayer.role = SerialRole::SLAVE;
        game.multiplayer.transferCompleted = 1;
        game.multiplayer.rx = 0x00;
        game.multiplayer.savedRx = 0x55;
        game.multiplayer.savedTx = 0x66;

        EXPECT_FALSE(kirpich::systems::handlePausedMultiplayer(game));
        EXPECT_FALSE(game.flow.paused);
        EXPECT_EQ(game.multiplayer.rx, 0x55);
        EXPECT_EQ(game.multiplayer.tx, 0x66);
        EXPECT_EQ(game.audioCues.pause, AudioPauseCommand::UNPAUSE);
    }
}

// ── Test 5: GameOverChainVectors ────────────────────────────────────────────────────────────────────
// GameState_01 / _0D (tetris.asm:4577-4593, 4917-4942): the curtain and the screen it paints.
TEST(Gameplay, GameOverChainVectors) {
    // $01: hide the pieces, clear the list, paint the curtain, arm the timer (:4578-4592).
    {
        GameContext game;
        game.engine.lineClears = kirpich::BoundedVec<std::uint8_t, 4>{4};
        game.flow.pieceLockStage = 2;
        game.flow.blinkCounter = 6;

        kirpich::systems::initGameOver(game);

        EXPECT_TRUE(game.spriteRenderer.slots[kActive].hidden);
        EXPECT_TRUE(game.spriteRenderer.slots[kPreview].hidden);
        EXPECT_EQ(game.flow.pieceLockStage, 0);
        EXPECT_EQ(game.flow.blinkCounter, 0);
        EXPECT_TRUE(game.engine.lineClears.empty());
        EXPECT_EQ(game.field.fieldCell(0, 0), kCurtainTile);
        EXPECT_EQ(game.field.fieldCell(17, 9), kCurtainTile);
        // This caller lets the wipe run — unlike the init (contract section 3).
        EXPECT_EQ(game.flow.wipeCounter, kWipeStartStep);
        EXPECT_EQ(game.flow.timer1, kCurtainFrames);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_CURTAIN);
    }

    // $0D holds while the timer runs (:4918-4920).
    {
        GameContext game;
        game.flow.gameState = GameState::GAME_OVER_CURTAIN;
        game.flow.timer1 = 1;
        kirpich::systems::gameOverCurtain(game);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_CURTAIN);
        EXPECT_EQ(game.audioCues.music, MusicId::NONE);
    }

    // $0D solo: cue the music, clear the field, print both blocks, go to the screen (:4921-4958).
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.gameState = GameState::GAME_OVER_CURTAIN;
        game.engine.score = 0;

        kirpich::systems::gameOverCurtain(game);

        EXPECT_EQ(game.audioCues.music, MusicId::GAME_OVER);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_SCREEN);
        // The game-over frame lands at field row 2, column 1 ($C843).
        for (std::size_t r = 0; r < kirpich::kGameOverTilemap.size(); ++r) {
            for (std::size_t c = 0; c < kirpich::kGameOverTilemap[r].size(); ++c) {
                ASSERT_EQ(game.field.fieldCell(2 + r, 1 + c), kirpich::kGameOverTilemap[r][c]);
            }
        }
        // The "try again" message lands at field row 12, column 1 ($C983).
        for (std::size_t r = 0; r < kirpich::kTryAgainTilemap.size(); ++r) {
            for (std::size_t c = 0; c < kirpich::kTryAgainTilemap[r].size(); ++c) {
                ASSERT_EQ(game.field.fieldCell(12 + r, 1 + c), kirpich::kTryAgainTilemap[r][c]);
            }
        }
        // A cell outside both blocks was cleared by the fill.
        EXPECT_EQ(game.field.fieldCell(0, 0), kSpace);
    }

    // $0D two-player: a different timer, the serial flag, the end jingle (:4923-4930).
    {
        GameContext game;
        game.multiplayer.isMultiplayer = true;
        game.flow.gameState = GameState::GAME_OVER_CURTAIN;

        kirpich::systems::gameOverCurtain(game);

        EXPECT_EQ(game.audioCues.music, MusicId::GAME_OVER);
        EXPECT_EQ(game.flow.timer1, kMultiplayerFrames);
        EXPECT_EQ(game.multiplayer.transferCompleted, 0x1B);
        EXPECT_EQ(game.flow.gameState, GameState::TWO_PLAYER_END_JINGLE);
        // The two-player path never paints the solo screen.
        EXPECT_EQ(game.field.fieldCell(2, 1), 0);
    }
}

// ── Test 6: BonusEndingTierVectors ──────────────────────────────────────────────────────────────────
// The Type A ending fork (tetris.asm:4943-4970): the three score tiers and their boundaries.
TEST(Gameplay, BonusEndingTierVectors) {
    struct Case {
        std::uint32_t score;
        bool earnsRocket;
        SpriteId rocket;
    };
    const Case cases[] = {
        {0, false, {}},
        {99'999, false, {}},
        {100'000, true, SpriteId::ROCKET_S},
        {149'999, true, SpriteId::ROCKET_S},
        {150'000, true, SpriteId::ROCKET_M},
        {199'999, true, SpriteId::ROCKET_M},
        {200'000, true, SpriteId::ROCKET_L},
        {999'999, true, SpriteId::ROCKET_L},
    };

    for (const Case& c : cases) {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.gameState = GameState::GAME_OVER_CURTAIN;
        game.engine.score = c.score;

        kirpich::systems::gameOverCurtain(game);

        if (c.earnsRocket) {
            EXPECT_EQ(game.flow.rocketSpriteIndex, c.rocket) << "score " << c.score;
            EXPECT_EQ(game.flow.timer1, kBonusFrames) << "score " << c.score;
            EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_TO_BONUS) << "score " << c.score;
        } else {
            EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_SCREEN) << "score " << c.score;
        }
    }

    // Type B never earns an ending, however high the score (:4943-4945).
    for (const std::uint32_t score : {std::uint32_t{100'000}, std::uint32_t{999'999}}) {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.gameState = GameState::GAME_OVER_CURTAIN;
        game.engine.score = score;

        kirpich::systems::gameOverCurtain(game);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_SCREEN) << "score " << score;
    }
}

// ── Test 7: GameOverScreenAndScoreboardVectors ──────────────────────────────────────────────────────
// GameState_04 / _0B / _0C (tetris.asm:4595-4615, 4708-4716, 4909-4915).
TEST(Gameplay, GameOverScreenAndScoreboardVectors) {
    // $04 waits for A or Start; anything else leaves it alone (:4596-4600).
    {
        GameContext game;
        game.flow.gameState = GameState::GAME_OVER_SCREEN;
        game.flow.wipeCounter = 7;
        press(game, {Action::MoveLeft});
        kirpich::systems::gameOverScreen(game);
        EXPECT_EQ(game.flow.gameState, GameState::GAME_OVER_SCREEN);
        EXPECT_EQ(game.flow.wipeCounter, 7);
    }

    // Both accepted buttons, and the three-way exit (:4601-4614).
    struct Exit {
        bool multiplayer;
        GameType type;
        GameState expected;
    };
    const Exit exits[] = {
        {true, GameType::TYPE_A, GameState::INIT_2P_DIFFICULTY},
        {true, GameType::TYPE_B, GameState::INIT_2P_DIFFICULTY},
        {false, GameType::TYPE_A, GameState::INIT_TYPE_A_DIFFICULTY},
        {false, GameType::TYPE_B, GameState::INIT_TYPE_B_DIFFICULTY},
    };
    for (const Exit& e : exits) {
        for (const Action button : {Action::RotateClockwise, Action::Start}) {
            GameContext game;
            game.flow.gameState = GameState::GAME_OVER_SCREEN;
            game.flow.wipeCounter = 7;
            game.multiplayer.isMultiplayer = e.multiplayer;
            game.flow.gameType = e.type;
            press(game, {button});

            kirpich::systems::gameOverScreen(game);
            EXPECT_EQ(game.flow.gameState, e.expected);
            EXPECT_EQ(game.flow.wipeCounter, 0);
        }
    }

    // $0B holds while the timer runs, then advances the count-up and reloads (:4709-4715).
    {
        GameContext game;
        game.flow.timer1 = 3;
        kirpich::systems::initTypeBScoreboard(game);
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 0);
        EXPECT_EQ(game.flow.timer1, 3);

        game.flow.timer1 = 0;
        kirpich::systems::initTypeBScoreboard(game);
        EXPECT_EQ(game.engine.scoreboardTallyPhase, 1);
        EXPECT_EQ(game.flow.timer1, kScoreboardFrames);
    }

    // $0C: any press advances, nothing else does (:4910-4914). The state is unreachable in play — the
    // handler exists because the dispatch table has the entry (contract section 8).
    {
        GameContext game;
        game.flow.gameState = GameState::STATE_0C_UNKNOWN;
        kirpich::systems::state0CUnknown(game);
        EXPECT_EQ(game.flow.gameState, GameState::STATE_0C_UNKNOWN);

        press(game, {Action::Select});
        kirpich::systems::state0CUnknown(game);
        EXPECT_EQ(game.flow.gameState, GameState::BURAN_LIFTOFF);
    }
}
