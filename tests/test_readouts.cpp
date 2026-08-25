// The number readouts — behavioral tests against docs/contracts/readouts.md.
//
// Device-free. The stats panel beside the playing field shows a score, a level, a line count and,
// in Type B, the height of the starting garbage. One printer draws them all, and most of them write
// both background maps because the second one is the paused screen. Every asserted cell is traced to
// the tetris.asm line that names it.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/tilemaps.h"
#include "retropp/input.h"
#include "state/display_state.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"
#include "systems/line_clear.h"
#include "systems/readouts.h"
#include "systems/scoring.h"

namespace {

using kirpich::Action;
using kirpich::BackgroundMap;
using kirpich::CharTile;
using kirpich::DisplayedMap;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::systems::GameContext;

constexpr std::uint8_t kSpace = static_cast<std::uint8_t>(CharTile::SPACE);
constexpr std::uint8_t kHeart = static_cast<std::uint8_t>(CharTile::HEART);

// The panel cells, from the contract's table.
constexpr std::size_t kScoreRow = 3;
constexpr std::size_t kScoreCol = 13;
constexpr std::size_t kTypeALevelRow = 7;
constexpr std::size_t kTypeALevelCol = 17;
constexpr std::size_t kTypeBLevelRow = 2;
constexpr std::size_t kTypeBLevelCol = 16;
constexpr std::size_t kLinesRow = 10;
constexpr std::size_t kTypeALinesCol = 14;
constexpr std::size_t kTypeBLinesCol = 16;
constexpr std::size_t kStartHeightRow = 5;
constexpr std::size_t kStartHeightCol = 16;

std::function<std::uint8_t()> noDraw() {
    return [] { return std::uint8_t{0}; };
}

// Read a run of cells back out of a map.
std::vector<std::uint8_t> cells(const BackgroundMap& map, std::size_t row, std::size_t col,
                                std::size_t count) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(map[row][col + i]);
    }
    return out;
}

// A round in progress: solo Type A, normal gameplay.
void inGameplay(GameContext& game, GameType type) {
    game.flow.gameType = type;
    game.flow.gameState = GameState::NORMAL_GAMEPLAY;
    game.multiplayer.isMultiplayer = false;
}

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

}  // namespace

// ── Test 1: PrintNumberDigitLaw ───────────────────────────────────────────────────────────────────
// PrintNumber (tetris.asm:6624-6673) draws two digits per pair, most significant first. A leading
// zero is a space (:6637), a zero after the number has started is a zero (:6635), and the final digit
// is drawn whether or not the number has started (:6647-6650) — so zero reads as `0`, not as blanks.
// Digits above the printed width are never looked at, because the original reads a fixed number of
// packed bytes.
TEST(Readouts, PrintNumberDigitLaw) {
    struct Vector {
        std::uint32_t value;
        std::uint8_t pairs;
        std::vector<std::uint8_t> expected;
    };

    const std::vector<Vector> vectors = {
        // Six digits — the score's width.
        {0, 3, {kSpace, kSpace, kSpace, kSpace, kSpace, 0}},
        {5, 3, {kSpace, kSpace, kSpace, kSpace, kSpace, 5}},
        {10, 3, {kSpace, kSpace, kSpace, kSpace, 1, 0}},
        {100, 3, {kSpace, kSpace, kSpace, 1, 0, 0}},
        {9999, 3, {kSpace, kSpace, 9, 9, 9, 9}},
        {999999, 3, {9, 9, 9, 9, 9, 9}},
        // Zeros inside the number are drawn once it has started.
        {90009, 3, {kSpace, 9, 0, 0, 0, 9}},
        // Four digits — the Type A line count.
        {0, 2, {kSpace, kSpace, kSpace, 0}},
        {7, 2, {kSpace, kSpace, kSpace, 7}},
        {1234, 2, {1, 2, 3, 4}},
        // Two digits — the Type B line count.
        {0, 1, {kSpace, 0}},
        {5, 1, {kSpace, 5}},
        {25, 1, {2, 5}},
        // Past the width, the high digits are dropped exactly as the original never reads them.
        {1000000, 3, {kSpace, kSpace, kSpace, kSpace, kSpace, 0}},
        {123456789, 3, {4, 5, 6, 7, 8, 9}},
        {125, 1, {2, 5}},
    };

    for (const Vector& v : vectors) {
        GameContext game;
        const std::size_t width = std::size_t{v.pairs} * 2;

        // A marker either side of the span shows that the printer stays inside it.
        game.display.map[4][3] = 0xEE;
        game.display.map[4][4 + width] = 0xEE;

        kirpich::systems::printNumber(game.display.map, game.flow, 4, 4, v.value, v.pairs);

        EXPECT_EQ(cells(game.display.map, 4, 4, width), v.expected)
            << "value " << v.value << " in " << int{v.pairs} << " pairs";
        EXPECT_EQ(game.display.map[4][3], 0xEE);
        EXPECT_EQ(game.display.map[4][4 + width], 0xEE);
    }

    // Every print clears the flag on its way out (:6657-6658).
    GameContext game;
    game.flow.scorePrintFlag = 1;
    kirpich::systems::printNumber(game.display.map, game.flow, 4, 4, 42, 3);
    EXPECT_EQ(game.flow.scorePrintFlag, 0);
}

// ── Test 2: ScorePrintFlagLaw ─────────────────────────────────────────────────────────────────────
// $FFE0 carries two roles and the second destroys the first: the score is only drawn when the flag is
// set (:6618-6620), and any print clears it (:6657-6658). Sites that draw both maps therefore set it
// again between their two calls (:244-245, :5729-5730). The line-count print clears it too, because
// it goes through the same printer.
TEST(Readouts, ScorePrintFlagLaw) {
    // Clear flag: the score is not drawn at all.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 1234;
        game.flow.scorePrintFlag = 0;

        kirpich::systems::printScore(game, game.display.map);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6),
                  (std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0}));
    }

    // Set flag: drawn, and the flag is spent.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 1234;
        game.flow.scorePrintFlag = 1;

        kirpich::systems::printScore(game, game.display.map);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6),
                  (std::vector<std::uint8_t>{kSpace, kSpace, 1, 2, 3, 4}));
        EXPECT_EQ(game.flow.scorePrintFlag, 0);
    }

    // One flag, two maps: without the set in between, the second draw would be suppressed. This is
    // what redrawScore does and it is why both maps carry the score.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 77;
        game.flow.scorePrintFlag = 1;
        game.engine.scoreRedrawRequested = true;
        game.flow.pieceLockStage = 3;

        kirpich::systems::redrawScore(game);

        const std::vector<std::uint8_t> drawn = {kSpace, kSpace, kSpace, kSpace, 7, 7};
        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), drawn);
        EXPECT_EQ(cells(game.display.secondMap, kScoreRow, kScoreCol, 6), drawn);
    }

    // Printing the line count spends the score's request just as a score print does.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.flow.lines = 12;
        game.flow.scorePrintFlag = 1;

        kirpich::systems::printLines(game);

        EXPECT_EQ(game.flow.scorePrintFlag, 0);
    }

    // Awarding points sets the flag, which is the only thing that ever makes a score appear during
    // play. The flag is not touched by hand here: an award has to be enough on its own.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.flow.level = 0;
        game.flow.wipeCounter = 5;  // the award's own gate
        game.engine.stats.singles = 1;
        ASSERT_EQ(game.flow.scorePrintFlag, 0);

        kirpich::systems::addLineClearScore(game);

        EXPECT_GT(game.engine.score, 0u);
        EXPECT_EQ(game.flow.scorePrintFlag, 1);

        game.engine.scoreRedrawRequested = true;
        game.flow.pieceLockStage = 3;
        kirpich::systems::redrawScore(game);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6),
                  (std::vector<std::uint8_t>{kSpace, kSpace, kSpace, kSpace, 4, 0}));
    }
}

// ── Test 3: ScoreGates ────────────────────────────────────────────────────────────────────────────
// Call_243B (tetris.asm:5814-5820) draws only during normal gameplay and only in a Type A game — the
// Type B panel has no score cells at all, it has HIGH instead.
TEST(Readouts, ScoreGates) {
    const std::vector<std::uint8_t> blank = {0, 0, 0, 0, 0, 0};

    // Type B: never.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_B);
        game.engine.score = 4321;
        game.flow.scorePrintFlag = 1;

        kirpich::systems::printScore(game, game.display.map);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), blank);
    }

    // Outside normal gameplay: never, even in Type A.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.flow.gameState = GameState::GAME_OVER_SCREEN;
        game.engine.score = 4321;
        game.flow.scorePrintFlag = 1;

        kirpich::systems::printScore(game, game.display.map);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), blank);
    }

    // Type A, in gameplay, flag set: drawn.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 4321;
        game.flow.scorePrintFlag = 1;

        kirpich::systems::printScore(game, game.display.map);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6),
                  (std::vector<std::uint8_t>{kSpace, kSpace, 4, 3, 2, 1}));
    }
}

// ── Test 4: VerticalBlankRedrawGates ──────────────────────────────────────────────────────────────
// The frame's last beat redraws the score only when one has been requested and the lock process is at
// stage 3 (tetris.asm:236-241), and clears the request when it has done so (:248-249).
TEST(Readouts, VerticalBlankRedrawGates) {
    const std::vector<std::uint8_t> blank = {0, 0, 0, 0, 0, 0};

    // No request: nothing happens, and the piece-lock stage is irrelevant.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 500;
        game.flow.scorePrintFlag = 1;
        game.engine.scoreRedrawRequested = false;
        game.flow.pieceLockStage = 3;

        kirpich::systems::redrawScore(game);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), blank);
        EXPECT_EQ(game.flow.scorePrintFlag, 1);
    }

    // Requested, but the lock process is elsewhere: the request survives for a later frame.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 500;
        game.flow.scorePrintFlag = 1;
        game.engine.scoreRedrawRequested = true;
        game.flow.pieceLockStage = 2;

        kirpich::systems::redrawScore(game);

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), blank);
        EXPECT_TRUE(game.engine.scoreRedrawRequested);
    }

    // Both: drawn into both maps, and the request is spent.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 500;
        game.flow.scorePrintFlag = 1;
        game.engine.scoreRedrawRequested = true;
        game.flow.pieceLockStage = 3;

        kirpich::systems::redrawScore(game);

        const std::vector<std::uint8_t> drawn = {kSpace, kSpace, kSpace, 5, 0, 0};
        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), drawn);
        EXPECT_EQ(cells(game.display.secondMap, kScoreRow, kScoreCol, 6), drawn);
        EXPECT_FALSE(game.engine.scoreRedrawRequested);
    }
}

// ── Test 5: WipeSeams ─────────────────────────────────────────────────────────────────────────────
// The field wipe carries the readouts on three of its steps: 17 draws the score into the second map
// and arms the flag (:5727-5731), 18 draws it into the live map on the frame after (:5740-5741), and
// 19 redraws the line count into the live map alone, with the digit count forking on game type
// (:5760-5771).
TEST(Readouts, WipeSeams) {
    // Steps 17 then 18: the second map first, the live map next frame.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.engine.score = 640;
        game.flow.scorePrintFlag = 1;
        game.flow.wipeCounter = 17;

        kirpich::systems::playingFieldWipeTick(game, noDraw());

        const std::vector<std::uint8_t> drawn = {kSpace, kSpace, kSpace, 6, 4, 0};
        EXPECT_EQ(cells(game.display.secondMap, kScoreRow, kScoreCol, 6), drawn);
        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6),
                  (std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0}));
        // Armed for the next step, which is the whole reason the live map gets its turn.
        EXPECT_EQ(game.flow.scorePrintFlag, 1);
        EXPECT_EQ(game.flow.wipeCounter, 18);

        kirpich::systems::playingFieldWipeTick(game, noDraw());

        EXPECT_EQ(cells(game.display.map, kScoreRow, kScoreCol, 6), drawn);
    }

    // Step 19, Type A: four digits from column 14, live map only.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.flow.lines = 137;
        game.flow.wipeCounter = 19;

        kirpich::systems::playingFieldWipeTick(game, noDraw());

        EXPECT_EQ(cells(game.display.map, kLinesRow, kTypeALinesCol, 4),
                  (std::vector<std::uint8_t>{kSpace, 1, 3, 7}));
        // The second map keeps whatever it had: only pausing ever copies a line count across.
        EXPECT_EQ(cells(game.display.secondMap, kLinesRow, kTypeALinesCol, 4),
                  (std::vector<std::uint8_t>{0, 0, 0, 0}));
    }

    // Step 19, Type B: two digits from column 16.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_B);
        game.flow.lines = 8;
        game.flow.wipeCounter = 19;

        kirpich::systems::playingFieldWipeTick(game, noDraw());

        EXPECT_EQ(cells(game.display.map, kLinesRow, kTypeBLinesCol, 2),
                  (std::vector<std::uint8_t>{kSpace, 8}));
    }
}

// ── Test 6: LevelStep ─────────────────────────────────────────────────────────────────────────────
// When the level increases the digit is redrawn in both maps, and the tens digit appears in the cell
// to its left once the level reaches ten (tetris.asm:5853-5870). The routine that does it returns
// early for Type B (:5829-5831), whose level does not move during a round.
TEST(Readouts, LevelStep) {
    // A single digit leaves the tens cell alone.
    {
        GameContext game;
        game.flow.level = 6;

        kirpich::systems::printLevelStep(game);

        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol], 6);
        EXPECT_EQ(game.display.secondMap[kTypeALevelRow][kTypeALevelCol], 6);
        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol - 1], 0);
    }

    // Two digits fill both cells, in both maps.
    {
        GameContext game;
        game.flow.level = 14;

        kirpich::systems::printLevelStep(game);

        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol], 4);
        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol - 1], 1);
        EXPECT_EQ(game.display.secondMap[kTypeALevelRow][kTypeALevelCol], 4);
        EXPECT_EQ(game.display.secondMap[kTypeALevelRow][kTypeALevelCol - 1], 1);
    }

    // Through the level check: Type A crossing the threshold redraws.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_A);
        game.flow.level = 0;
        game.flow.lines = 10;

        kirpich::systems::checkForLevelUp(game);

        EXPECT_EQ(game.flow.level, 1);
        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol], 1);
    }

    // Type B never reaches the redraw, because the check returns before it.
    {
        GameContext game;
        inGameplay(game, GameType::TYPE_B);
        game.flow.level = 0;
        game.flow.lines = 10;

        kirpich::systems::checkForLevelUp(game);

        EXPECT_EQ(game.flow.level, 0);
        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol], 0);
    }
}

// ── Test 7: RoundInitDrawsThePanel ────────────────────────────────────────────────────────────────
// The round init fills the panel and builds the paused screen: the level in its game type's cell in
// both maps with a heart beside it in heart mode (tetris.asm:4162-4175), the opening line count in
// the live map (:4183-4194), the Type B height under HIGH in both maps (:4214-4218), and the whole
// backdrop plus the pause message in the second map (:4155-4161).
TEST(Readouts, RoundInitDrawsThePanel) {
    const auto init = [](GameContext& game) {
        kirpich::systems::initGame(game, noDraw(), {});
    };

    // Type A: level at row 7, a single `0` for the line count, no height.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.typeALevel = 3;

        init(game);

        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol], 3);
        EXPECT_EQ(game.display.secondMap[kTypeALevelRow][kTypeALevelCol], 3);
        EXPECT_EQ(game.display.map[kLinesRow][kTypeBLinesCol + 1], 0);
    }

    // Type B: level at row 2, the count's two digits, and the start height under HIGH in both maps.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_B;
        game.flow.typeBLevel = 2;
        game.flow.typeBStartHeight = 4;

        init(game);

        EXPECT_EQ(game.display.map[kTypeBLevelRow][kTypeBLevelCol], 2);
        EXPECT_EQ(game.display.secondMap[kTypeBLevelRow][kTypeBLevelCol], 2);
        EXPECT_EQ(cells(game.display.map, kLinesRow, kTypeBLinesCol, 2),
                  (std::vector<std::uint8_t>{2, 5}));
        EXPECT_EQ(game.display.map[kStartHeightRow][kStartHeightCol], 4);
        EXPECT_EQ(game.display.secondMap[kStartHeightRow][kStartHeightCol], 4);
    }

    // Heart mode puts the glyph in the cell after the level, in both maps.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.typeALevel = 1;
        game.flow.heartMode = 1;

        init(game);

        EXPECT_EQ(game.display.map[kTypeALevelRow][kTypeALevelCol + 1], kHeart);
        EXPECT_EQ(game.display.secondMap[kTypeALevelRow][kTypeALevelCol + 1], kHeart);
    }

    // The second map is the paused screen: the same backdrop, with the pause message over it.
    {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;

        init(game);

        // A panel cell the readouts do not touch is the backdrop's own.
        EXPECT_EQ(game.display.secondMap[1][13], kirpich::kTypeAGameplayTilemap[1][13]);
        // The message sits at $9C63 — row 3, column 3.
        EXPECT_EQ(game.display.secondMap[3][3], kirpich::kPauseMessageTilemap[0][0]);
        EXPECT_EQ(game.display.secondMap[3 + 9][3 + 7], kirpich::kPauseMessageTilemap[9][7]);
        // The live map does not get the message.
        EXPECT_EQ(game.display.map[3][3], kirpich::kTypeAGameplayTilemap[3][3]);
    }
}

// ── Test 8: PauseShowsTheSecondMap ────────────────────────────────────────────────────────────────
// Pausing switches the display to the second map (tetris.asm:4461) and copies the four line-count
// digits across (:4464-4476); unpausing switches back (:4487). The copy is the only thing that ever
// puts a line count in the second map.
TEST(Readouts, PauseShowsTheSecondMap) {
    GameContext game;
    inGameplay(game, GameType::TYPE_A);

    // A line count drawn during play reaches the live map only.
    game.flow.lines = 42;
    kirpich::systems::printLines(game);
    EXPECT_EQ(cells(game.display.map, kLinesRow, kTypeALinesCol, 4),
              (std::vector<std::uint8_t>{kSpace, kSpace, 4, 2}));
    EXPECT_EQ(cells(game.display.secondMap, kLinesRow, kTypeALinesCol, 4),
              (std::vector<std::uint8_t>{0, 0, 0, 0}));

    press(game, {Action::Start});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));

    EXPECT_TRUE(game.flow.paused);
    EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);
    EXPECT_EQ(&game.display.displayedMap(), &game.display.secondMap);
    EXPECT_EQ(cells(game.display.secondMap, kLinesRow, kTypeALinesCol, 4),
              (std::vector<std::uint8_t>{kSpace, kSpace, 4, 2}));

    press(game, {Action::Start});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));

    EXPECT_FALSE(game.flow.paused);
    EXPECT_EQ(game.display.displayed, DisplayedMap::FIRST);
    EXPECT_EQ(&game.display.displayedMap(), &game.display.map);
}

// ── Test 8b: PauseCarriesTheRiseCount ─────────────────────────────────────────────────────────────
// Type C's RISE readout follows the count on the live map, and the paused screen has to show the
// same number: the second map's panel was stamped at the round init with the starting count, so
// without a pause-time copy the paused screen reads 10 whatever the round has done since — the same
// staleness the line-count copy exists to prevent, on the readout the cartridge never had.
TEST(Readouts, PauseCarriesTheRiseCount) {
    GameContext game;
    inGameplay(game, GameType::TYPE_C);

    // A count the round has moved off its starting value, drawn during play: live map only. The
    // readout is two cells from column 16; a single digit blanks its leading cell.
    game.flow.riseCounter = 7;
    kirpich::systems::printRise(game, game.display.map);
    EXPECT_EQ(game.display.map[8][17], 7) << "the live RISE cell follows the count";

    press(game, {Action::Start});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));

    ASSERT_TRUE(game.flow.paused);
    EXPECT_EQ(game.display.secondMap[8][17], 7)
        << "the paused screen must show the count as it stands, not as the init stamped it";
    // The leading cell travels too: a banked count holds two digits, and a one-digit count blanks
    // the cell the init's two-digit 10 wrote.
    EXPECT_EQ(game.display.secondMap[8][16], game.display.map[8][16]);
}

// ── Test 9: SelectStillTogglesThePreviewWhilePaused ───────────────────────────────────────────────
// A preserved quirk. Nothing between HandleStartSelect and handleSelect checks whether the game is
// paused (tetris.asm:4448-4450), so Select keeps working while the paused screen is up: it toggles the
// preview-hide flag and redraws the descriptor (:4423-4438). Pausing selects the other background map
// (:4461) and leaves object display alone, so the preview that comes back is drawn over the paused
// screen — where there is no playing field for it to sit in.
//
// This is what the original does and it stays. Do not add a pause gate to handleSelect.
TEST(Readouts, SelectStillTogglesThePreviewWhilePaused) {
    GameContext game;
    inGameplay(game, GameType::TYPE_A);
    game.engine.hidePreviewPiece = true;

    press(game, {Action::Start});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));
    ASSERT_TRUE(game.flow.paused);
    ASSERT_EQ(game.display.displayed, DisplayedMap::SECOND);
    // The pause hides both pieces, so nothing is drawn over the paused screen yet.
    EXPECT_TRUE(game.spriteRenderer.slots[kirpich::kPreviewPieceSlot].hidden);

    // Select, still paused: the preview comes back and the paused screen stays up.
    press(game, {Action::Select});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));

    EXPECT_FALSE(game.engine.hidePreviewPiece);
    EXPECT_FALSE(game.spriteRenderer.slots[kirpich::kPreviewPieceSlot].hidden);
    EXPECT_TRUE(game.flow.paused);
    EXPECT_EQ(game.display.displayed, DisplayedMap::SECOND);

    // Pressing it again puts the preview away, still without leaving the pause.
    press(game, {Action::Select});
    EXPECT_TRUE(kirpich::systems::handleStartSelect(game));

    EXPECT_TRUE(game.engine.hidePreviewPiece);
    EXPECT_TRUE(game.spriteRenderer.slots[kirpich::kPreviewPieceSlot].hidden);
    EXPECT_TRUE(game.flow.paused);
}
