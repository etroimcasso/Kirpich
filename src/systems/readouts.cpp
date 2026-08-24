#include "systems/readouts.h"

#include <array>

#include "kirpich/char_tile.h"
#include "kirpich/game_state.h"
#include "kirpich/game_type.h"

namespace kirpich::systems {
namespace {

// Where each readout's leftmost cell sits, by game type. Derived in docs/contracts/readouts.md and
// cross-checked against the blank cells the two gameplay backdrops leave for them.
constexpr std::size_t kScoreRow = 3;
constexpr std::size_t kScoreCol = 13;
constexpr std::uint8_t kScoreDigitPairs = 3;

constexpr std::size_t kTypeALevelRow = 7;
constexpr std::size_t kTypeALevelCol = 17;
constexpr std::size_t kTypeBLevelRow = 2;
constexpr std::size_t kTypeBLevelCol = 16;

// Type C's panel is the port's own screen (src/data/type_c_tilemap.h), so its cells are its own: the
// score sits a row higher than Type A's, and the level and the rise countdown share the box below it.
// The line count is the exception - it lands on the same row and columns every screen puts it on, so
// printLines, printLinesSeed and copyLinesToSecondMap reach Type C without knowing it exists.
constexpr std::size_t kTypeCScoreRow = 2;
constexpr std::size_t kTypeCLevelRow = 6;
constexpr std::size_t kTypeCLevelCol = 16;
constexpr std::size_t kTypeCRiseRow = 8;
constexpr std::size_t kTypeCRiseCol = 16;
constexpr std::uint8_t kTypeCRiseDigitPairs = 1;

constexpr std::size_t kLinesRow = 10;
constexpr std::size_t kTypeALinesCol = 14;
constexpr std::size_t kTypeBLinesCol = 16;
constexpr std::uint8_t kTypeALinesDigitPairs = 2;
constexpr std::uint8_t kTypeBLinesDigitPairs = 1;
constexpr std::size_t kLinesCopyWidth = 4;

constexpr std::size_t kStartHeightRow = 5;
constexpr std::size_t kStartHeightCol = 16;

// A digit's tile index is the digit itself: the font puts 0-9 in the first ten slots.
constexpr std::uint8_t digitTile(std::uint32_t digit) {
    return static_cast<std::uint8_t>(digit);
}

constexpr std::uint8_t kSpaceTile = static_cast<std::uint8_t>(CharTile::SPACE);

bool typeB(const GameFlowState& flow) {
    return flow.gameType == GameType::TYPE_B;
}

// A cell in the background map, as a row and a column.
struct Cell {
    std::size_t row;
    std::size_t col;
};

// Where the level digit goes, per game type. Each mode's panel puts it somewhere different.
Cell levelCell(const GameFlowState& flow) {
    switch (flow.gameType) {
        case GameType::TYPE_B: return {kTypeBLevelRow, kTypeBLevelCol};
        case GameType::TYPE_C: return {kTypeCLevelRow, kTypeCLevelCol};
        default:               return {kTypeALevelRow, kTypeALevelCol};
    }
}

// Which row the score's six digits land on. Type B has no score cells and never asks.
std::size_t scoreRow(const GameFlowState& flow) {
    return flow.gameType == GameType::TYPE_C ? kTypeCScoreRow : kScoreRow;
}

}  // namespace

void printNumber(BackgroundMap& map, GameFlowState& flow, std::size_t row, std::size_t col,
                 std::uint32_t value, std::uint8_t digitPairs) {
    const std::size_t digits = std::size_t{digitPairs} * 2;

    // Only the digits that fit are drawn. The original reads `digitPairs` packed-decimal bytes and
    // anything above them is simply not looked at.
    std::uint32_t scale = 1;
    for (std::size_t i = 0; i < digits; ++i) {
        scale *= 10;
    }
    const std::uint32_t shown = value % scale;

    bool started = false;
    std::uint32_t place = scale / 10;
    for (std::size_t i = 0; i < digits; ++i, place /= 10) {
        const std::uint32_t digit = (shown / place) % 10;
        const bool last = (i + 1 == digits);

        if (digit != 0) {
            started = true;
        }
        // A zero prints once the number has started, and the last digit prints whether or not it
        // has - which is what makes a value of zero read as `0` rather than as blanks.
        map[row][col + i] = (started || last) ? digitTile(digit) : kSpaceTile;
    }

    flow.scorePrintFlag = 0;
}

void printScore(GameContext& game, BackgroundMap& map) {
    GameFlowState& flow = game.flow;

    if (flow.gameState != GameState::NORMAL_GAMEPLAY) {
        return;
    }
    // Type B's panel has no score cells; Type A's and Type C's do.
    if (flow.gameType == GameType::TYPE_B) {
        return;
    }
    if (flow.scorePrintFlag == 0) {
        return;
    }

    printNumber(map, flow, scoreRow(flow), kScoreCol, game.engine.score, kScoreDigitPairs);
}

void redrawScore(GameContext& game) {
    if (!game.engine.scoreRedrawRequested) {
        return;
    }
    if (game.flow.pieceLockStage != 3) {
        return;
    }

    printScore(game, game.display.map);

    // The draw above cleared the flag, so the second map needs it set again or it would be skipped.
    game.flow.scorePrintFlag = 1;
    printScore(game, game.display.secondMap);

    game.engine.scoreRedrawRequested = false;
}

void printLevel(GameContext& game) {
    GameFlowState& flow = game.flow;
    const auto [row, col] = levelCell(flow);

    // The level is drawn as a raw value rather than through the printer: a starting level is a single
    // digit, and the font's digit tiles are the digits themselves.
    game.display.map[row][col] = flow.level;
    game.display.secondMap[row][col] = flow.level;

    if (flow.heartMode != 0) {
        const std::uint8_t heart = static_cast<std::uint8_t>(CharTile::HEART);
        game.display.secondMap[row][col + 1] = heart;
        game.display.map[row][col + 1] = heart;
    }
}

void printLevelStep(GameContext& game) {
    const std::uint8_t level = game.flow.level;
    const auto [row, col] = levelCell(game.flow);

    game.display.map[row][col] = digitTile(level % 10);
    game.display.secondMap[row][col] = digitTile(level % 10);

    const std::uint8_t tens = static_cast<std::uint8_t>(level / 10);
    if (tens != 0) {
        game.display.map[row][col - 1] = digitTile(tens);
        game.display.secondMap[row][col - 1] = digitTile(tens);
    }
}

void printRise(GameContext& game, BackgroundMap& map) {
    printNumber(map, game.flow, kTypeCRiseRow, kTypeCRiseCol, game.flow.riseCounter,
                kTypeCRiseDigitPairs);
}

void printLinesSeed(GameContext& game) {
    const std::uint32_t ones = game.flow.lines % 10;
    game.display.map[kLinesRow][kTypeBLinesCol + 1] = digitTile(ones);

    // The tens cell is written only when the ones digit is non-zero, and the original writes a
    // literal 2 there rather than a computed digit - the only count that reaches it is Type B's 25.
    if (ones != 0) {
        game.display.map[kLinesRow][kTypeBLinesCol] = digitTile(2);
    }
}

void printLines(GameContext& game) {
    GameFlowState& flow = game.flow;

    if (typeB(flow)) {
        printNumber(game.display.map, flow, kLinesRow, kTypeBLinesCol, flow.lines,
                    kTypeBLinesDigitPairs);
    } else {
        printNumber(game.display.map, flow, kLinesRow, kTypeALinesCol, flow.lines,
                    kTypeALinesDigitPairs);
    }
}

void printStartHeight(GameContext& game) {
    const std::uint8_t height = game.flow.typeBStartHeight;
    game.display.map[kStartHeightRow][kStartHeightCol] = height;
    game.display.secondMap[kStartHeightRow][kStartHeightCol] = height;
}

void copyLinesToSecondMap(GameContext& game) {
    for (std::size_t i = 0; i < kLinesCopyWidth; ++i) {
        game.display.secondMap[kLinesRow][kTypeALinesCol + i] =
            game.display.map[kLinesRow][kTypeALinesCol + i];
    }
}

}  // namespace kirpich::systems
