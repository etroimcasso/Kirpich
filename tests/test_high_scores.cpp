// Top-score recording — behavioral tests against docs/contracts/high-score-state.md.
//
// Device-free. A finished round's score is compared against the stored table for the level (and, in
// Type B, the starting height) it was played at, inserted if it beat one of the three ranked entries,
// and staged for display; a score that made the table sends the player to name entry. The staged rows
// live in the board and are copied into the displayed map; name entry writes the map alone. Every
// asserted value is traced to the tetris.asm line that produces it.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>

#include "data/music.h"
#include "data/sfx.h"
#include "retropp/input.h"
#include "state/high_score_state.h"
#include "systems/game_context.h"
#include "systems/high_scores.h"

namespace {

using kirpich::Action;
using kirpich::CharTile;
using kirpich::GameState;
using kirpich::GameType;
using kirpich::HighScoreState;
using kirpich::MusicId;
using kirpich::MusicType;
using kirpich::SquareSfxId;
using kirpich::TopScoreEntry;
using kirpich::systems::GameContext;
using kirpich::systems::kTopScoreDigits;
using kirpich::systems::kTopScoreFieldWidth;
using kirpich::systems::kTopScoreNameCol;
using kirpich::systems::kTopScoreNameLength;
using kirpich::systems::kTopScoreRowCount;
using kirpich::systems::kTopScoreScoreCol;
using kirpich::systems::kTopScoreTopRow;

constexpr std::uint8_t tile(CharTile g) { return static_cast<std::uint8_t>(g); }

constexpr std::uint8_t kEmpty = tile(CharTile::ELLIPSIS);

retropp::ActionSet actionSet(std::initializer_list<Action> as) {
    retropp::ActionSet s;
    for (const Action a : as) {
        s.set(retropp::actionId(a), true);
    }
    return s;
}

// A fresh press this tick (held mirrors it, as a real first frame does).
void press(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet(as);
    game.joypad.held = game.joypad.pressed;
}

// A sustained hold with no new press — the frames key repeat counts down on.
void hold(GameContext& game, std::initializer_list<Action> as) {
    game.joypad.pressed = actionSet({});
    game.joypad.held = actionSet(as);
}

// A context sitting in name entry: a score already inserted at `rank` (the inverted counter, 3 = best),
// the cursor on column 0, and the blink suppressed so input laws are read on their own.
GameContext nameEntryContext(std::uint8_t rank = 3, GameType type = GameType::TYPE_A) {
    GameContext game;
    game.flow.gameType = type;
    game.flow.musicType = MusicType::MUSIC_A;
    game.flow.timer1 = 5;  // the blink is a no-op while the frame timer runs
    game.highScores.newTopScore = true;
    game.highScores.newScoreRank = rank;
    game.highScores.nameEntryColumn = 0;
    return game;
}

// The entry name entry is editing, for the context above.
TopScoreEntry& namedEntry(GameContext& game) {
    const std::size_t index = kTopScoreRowCount - game.highScores.newScoreRank;
    if (game.flow.gameType == GameType::TYPE_B) {
        return game.highScores.typeB[game.flow.typeBLevel][game.flow.typeBStartHeight][index];
    }
    return game.highScores.typeA[game.flow.typeALevel][index];
}

// The wheel's reachable domain in order (tetris.asm:4025-4077): "a" up through the skip glyph, then
// the space, which wraps back to "a". Heart mode extends the run by one, making the heart typeable.
std::vector<CharTile> wheelRing(bool heartMode) {
    const std::uint8_t last =
        heartMode ? tile(CharTile::HEART) : tile(CharTile::MULTIPLICATION_SIGN);
    std::vector<CharTile> ring;
    for (std::uint8_t g = tile(CharTile::LETTER_A); g <= last; ++g) {
        ring.push_back(static_cast<CharTile>(g));
    }
    ring.push_back(CharTile::SPACE);
    return ring;
}

// Read one staged row's fourteen cells out of the board.
std::vector<std::uint8_t> boardRow(const GameContext& game, std::size_t rank) {
    std::vector<std::uint8_t> cells;
    for (std::size_t c = 0; c < kTopScoreFieldWidth; ++c) {
        cells.push_back(game.field.board[kTopScoreTopRow + rank][kTopScoreNameCol + c]);
    }
    return cells;
}

// A slice pre-loaded with three descending scores, so insert positions are unambiguous.
void seedSlice(std::array<TopScoreEntry, 3>& slice, std::uint32_t a, std::uint32_t b,
               std::uint32_t c) {
    slice[0] = TopScoreEntry{a, {CharTile::LETTER_A, CharTile::LETTER_A, CharTile::LETTER_A,
                                 CharTile::LETTER_A, CharTile::LETTER_A, CharTile::LETTER_A}};
    slice[1] = TopScoreEntry{b, {CharTile::LETTER_B, CharTile::LETTER_B, CharTile::LETTER_B,
                                 CharTile::LETTER_B, CharTile::LETTER_B, CharTile::LETTER_B}};
    slice[2] = TopScoreEntry{c, {CharTile::LETTER_C, CharTile::LETTER_C, CharTile::LETTER_C,
                                 CharTile::LETTER_C, CharTile::LETTER_C, CharTile::LETTER_C}};
}

}  // namespace

// ── Test 1: SliceWalkVectors ────────────────────────────────────────────────────────────────────
// UpdateTypeATopScores / UpdateTypeBTopScores (tetris.asm:3641-3689). The Type A walk strides one
// three-entry group per level; the Type B walk strides a whole level's six height groups, then one
// group per height. Swept over every level and every height — the full table, not a sample.
TEST(HighScores, SliceWalkVectors) {
    for (std::uint8_t level = 0; level < 10; ++level) {
        GameContext game;
        game.flow.gameType = GameType::TYPE_A;
        game.flow.typeALevel = level;
        game.engine.score = 1234;

        kirpich::systems::updateTypeATopScores(game);

        HighScoreState expected;
        expected.typeA[level][0].score = 1234;
        expected.typeA[level][0].name.fill(CharTile::ELLIPSIS);
        expected.typeA[level][0].name[0] = CharTile::LETTER_A;

        EXPECT_EQ(game.highScores.typeA, expected.typeA) << "Type A level " << int{level};
        EXPECT_EQ(game.highScores.typeB, expected.typeB) << "Type A must not touch Type B";
    }

    for (std::uint8_t level = 0; level < 10; ++level) {
        for (std::uint8_t height = 0; height < 6; ++height) {
            GameContext game;
            game.flow.gameType = GameType::TYPE_B;
            game.flow.typeBLevel = level;
            game.flow.typeBStartHeight = height;
            game.engine.score = 4321;

            kirpich::systems::updateTypeBTopScores(game);

            HighScoreState expected;
            expected.typeB[level][height][0].score = 4321;
            expected.typeB[level][height][0].name.fill(CharTile::ELLIPSIS);
            expected.typeB[level][height][0].name[0] = CharTile::LETTER_A;

            EXPECT_EQ(game.highScores.typeB, expected.typeB)
                << "Type B level " << int{level} << " height " << int{height};
            EXPECT_EQ(game.highScores.typeA, expected.typeA) << "Type B must not touch Type A";
        }
    }
}

// ── Test 2: InsertVectors ───────────────────────────────────────────────────────────────────────
// UpdateTopScores (tetris.asm:3737-3890). The compare is strictly greater, the displaced entries
// shift down a rank, the rank is recorded as the original's inverted counter, and an insert seeds a
// blank name and routes the player into name entry.
TEST(HighScores, InsertVectors) {
    // A tie does not displace. Equality falls through every digit pair and moves to the next rank
    // (:3748-3756), so a score has to be strictly greater than one of them to get in. The vector
    // ties every rank it could take: the first is out of reach and the other two are equal to it.
    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 9000, 5000, 5000);
        game.engine.score = 5000;

        kirpich::systems::updateTypeATopScores(game);

        EXPECT_EQ(game.highScores.typeA[0][0].score, 9000u);
        EXPECT_EQ(game.highScores.typeA[0][1].score, 5000u);
        EXPECT_EQ(game.highScores.typeA[0][2].score, 5000u);
        EXPECT_EQ(game.highScores.typeA[0][1].name[0], CharTile::LETTER_B) << "nothing shifted";
        EXPECT_EQ(game.highScores.typeA[0][2].name[0], CharTile::LETTER_C);
        EXPECT_FALSE(game.highScores.newTopScore);
        EXPECT_EQ(game.highScores.newScoreRank, 0);
        EXPECT_EQ(game.audioCues.music, MusicId::NONE);
        EXPECT_TRUE(game.highScores.topScoresRedrawRequested) << "a no-insert refresh still redraws";
    }

    // One point more does displace, and takes the best rank.
    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 5000, 3000, 1000);
        game.engine.score = 5001;

        kirpich::systems::updateTypeATopScores(game);

        EXPECT_EQ(game.highScores.typeA[0][0].score, 5001u);
        EXPECT_EQ(game.highScores.typeA[0][1].score, 5000u) << "the old best shifts down";
        EXPECT_EQ(game.highScores.typeA[0][2].score, 3000u);
        EXPECT_EQ(game.highScores.typeA[0][1].name[0], CharTile::LETTER_A) << "its name shifts too";
        EXPECT_EQ(game.highScores.typeA[0][2].name[0], CharTile::LETTER_B);

        // The seed name: "a" and five empty cells (:3814-3823).
        const auto& name = game.highScores.typeA[0][0].name;
        EXPECT_EQ(name[0], CharTile::LETTER_A);
        for (std::size_t i = 1; i < kTopScoreNameLength; ++i) {
            EXPECT_EQ(name[i], CharTile::ELLIPSIS) << "seed cell " << i;
        }

        EXPECT_TRUE(game.highScores.newTopScore);                 // :3833
        EXPECT_EQ(game.highScores.newScoreRank, 3) << "inverted"; // :3797-3798
        EXPECT_EQ(game.highScores.nameEntryColumn, 0);            // :3830
        EXPECT_EQ(game.flow.blinkCounter, 0);                     // :3829
        EXPECT_EQ(game.audioCues.music, MusicId::TOP_SCORE);      // :3831-3832
        EXPECT_EQ(game.engine.score, 0u) << "ClearScoreAndStats runs on the way out (:3887)";
    }

    // Middle rank: the worst entry falls off the end.
    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 9000, 1000, 500);
        game.engine.score = 5000;

        kirpich::systems::updateTypeATopScores(game);

        EXPECT_EQ(game.highScores.typeA[0][0].score, 9000u);
        EXPECT_EQ(game.highScores.typeA[0][1].score, 5000u);
        EXPECT_EQ(game.highScores.typeA[0][2].score, 1000u);
        EXPECT_EQ(game.highScores.newScoreRank, 2);
    }

    // Last rank.
    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 9000, 8000, 500);
        game.engine.score = 1000;

        kirpich::systems::updateTypeATopScores(game);

        EXPECT_EQ(game.highScores.typeA[0][2].score, 1000u);
        EXPECT_EQ(game.highScores.newScoreRank, 1);
    }

    // Beaten by all three: nothing changes and no name entry is offered.
    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 9000, 8000, 7000);
        game.engine.score = 100;

        kirpich::systems::updateTypeATopScores(game);

        EXPECT_EQ(game.highScores.typeA[0][2].score, 7000u);
        EXPECT_FALSE(game.highScores.newTopScore);
    }
}

// ── Test 3: PrintTopScoreDigitLaw ───────────────────────────────────────────────────────────────
// PrintTopScore (tetris.asm:3694-3722). Leading zeros are skipped, not blanked — the destination
// advances and the cell keeps the empty glyph the field clear left. From the first non-zero digit on,
// every digit prints, zeros included. A zero score prints nothing at all. The disassembly calls this
// a bug; it is what the screen shows.
TEST(HighScores, PrintTopScoreDigitLaw) {
    struct Vector {
        std::uint32_t score;
        std::array<int, kTopScoreDigits> cells;  // -1 = untouched (empty glyph)
    };
    const std::array<Vector, 6> vectors{{
        {0, {-1, -1, -1, -1, -1, -1}},
        {1, {-1, -1, -1, -1, -1, 1}},
        {99, {-1, -1, -1, -1, 9, 9}},
        {100, {-1, -1, -1, 1, 0, 0}},  // trailing zeros DO print
        {10203, {-1, 1, 0, 2, 0, 3}},
        {999999, {9, 9, 9, 9, 9, 9}},
    }};

    for (const Vector& v : vectors) {
        GameContext game;
        seedSlice(game.highScores.typeA[0], v.score, 0, 0);
        game.engine.score = 0;  // 0 > 0 is false, so nothing inserts and only the print runs

        kirpich::systems::updateTypeATopScores(game);

        for (std::size_t i = 0; i < kTopScoreDigits; ++i) {
            const std::uint8_t actual =
                game.field.board[kTopScoreTopRow][kTopScoreScoreCol + i];
            const std::uint8_t want =
                v.cells[i] < 0 ? kEmpty : static_cast<std::uint8_t>(v.cells[i]);
            EXPECT_EQ(actual, want) << "score " << v.score << " digit cell " << i;
        }
    }
}

// ── Test 4: StagingLayoutAndClear ───────────────────────────────────────────────────────────────
// ClearTopScoreFields (tetris.asm:3934-3950) and the staging loops (:3835-3885). Three rows of
// fourteen cells from row 13, column 4: a six-glyph name, a two-cell gap, a six-digit score. A name
// shorter than six is delimited by a zero glyph and the cells past it keep the empty glyph.
TEST(HighScores, StagingLayoutAndClear) {
    {
        GameContext game;
        kirpich::systems::clearTopScoreFields(game);

        for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
            EXPECT_EQ(boardRow(game, rank),
                      std::vector<std::uint8_t>(kTopScoreFieldWidth, kEmpty))
                << "row " << rank;
        }

        // Nothing outside the three rows or the fourteen columns.
        EXPECT_EQ(game.field.board[kTopScoreTopRow - 1][kTopScoreNameCol], 0);
        EXPECT_EQ(game.field.board[kTopScoreTopRow + kTopScoreRowCount][kTopScoreNameCol], 0);
        EXPECT_EQ(game.field.board[kTopScoreTopRow][kTopScoreNameCol - 1], 0);
        EXPECT_EQ(game.field.board[kTopScoreTopRow][kTopScoreNameCol + kTopScoreFieldWidth], 0);
    }

    {
        GameContext game;
        seedSlice(game.highScores.typeA[0], 12, 0, 0);
        // A two-glyph name: the zero delimiter stops the copy (:3869-3871).
        game.highScores.typeA[0][0].name = {CharTile::LETTER_H, CharTile::LETTER_I,
                                            static_cast<CharTile>(0), CharTile::LETTER_Z,
                                            CharTile::LETTER_Z, CharTile::LETTER_Z};
        game.engine.score = 0;

        kirpich::systems::updateTypeATopScores(game);

        const std::vector<std::uint8_t> row = boardRow(game, 0);
        EXPECT_EQ(row[0], tile(CharTile::LETTER_H));
        EXPECT_EQ(row[1], tile(CharTile::LETTER_I));
        for (std::size_t c = 2; c < kTopScoreNameLength; ++c) {
            EXPECT_EQ(row[c], kEmpty) << "past the delimiter, cell " << c;
        }
        EXPECT_EQ(row[6], kEmpty) << "the gap";
        EXPECT_EQ(row[7], kEmpty) << "the gap";
        // Score 12 occupies the last two digit cells; the four before it stay empty.
        EXPECT_EQ(row[8], kEmpty);
        EXPECT_EQ(row[12], 1);
        EXPECT_EQ(row[13], 2);
    }
}

// ── Test 5: FlushGeometry ───────────────────────────────────────────────────────────────────────
// DrawTopScoresToVRAM (tetris.asm:3893-3932). The staged rows are carried from the board into the
// displayed map at the same coordinates, but only on the frames a redraw was asked for, and the
// request is cleared once served. The board is a source here, never a destination.
TEST(HighScores, FlushGeometry) {
    // Not requested: nothing moves.
    {
        GameContext game;
        kirpich::systems::clearTopScoreFields(game);
        const auto mapBefore = game.display.map;

        kirpich::systems::drawTopScoresToVram(game);

        EXPECT_EQ(game.display.map, mapBefore);
        EXPECT_FALSE(game.highScores.topScoresRedrawRequested);
    }

    // Requested: the name and score fields move, the gap between them does not, and the request
    // clears. The gap is stepped over, so whatever the screen's backdrop put there survives.
    {
        GameContext game;
        kirpich::systems::clearTopScoreFields(game);
        game.field.board[kTopScoreTopRow][kTopScoreNameCol] = 0x11;
        game.field.board[kTopScoreTopRow + 2][kTopScoreNameCol + kTopScoreFieldWidth - 1] = 0x22;
        game.highScores.topScoresRedrawRequested = true;
        const auto boardBefore = game.field.board;

        // A backdrop already occupies the two gap cells on every row.
        constexpr std::uint8_t kBackdrop = 0x77;
        for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
            game.display.map[kTopScoreTopRow + rank][kTopScoreNameCol + 6] = kBackdrop;
            game.display.map[kTopScoreTopRow + rank][kTopScoreNameCol + 7] = kBackdrop;
        }

        kirpich::systems::drawTopScoresToVram(game);

        for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
            const std::size_t row = kTopScoreTopRow + rank;
            for (const std::size_t start : {kTopScoreNameCol, kTopScoreScoreCol}) {
                for (std::size_t c = start; c < start + kTopScoreNameLength; ++c) {
                    EXPECT_EQ(game.display.map[row][c], game.field.board[row][c])
                        << "row " << rank << " col " << c;
                }
            }
            EXPECT_EQ(game.display.map[row][kTopScoreNameCol + 6], kBackdrop)
                << "the gap is stepped over, row " << rank;
            EXPECT_EQ(game.display.map[row][kTopScoreNameCol + 7], kBackdrop)
                << "the gap is stepped over, row " << rank;
        }
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], 0x11);
        EXPECT_EQ(game.display.map[kTopScoreTopRow + 2][kTopScoreNameCol + kTopScoreFieldWidth - 1],
                  0x22);

        // Untouched neighbours, and the board unchanged.
        EXPECT_EQ(game.display.map[kTopScoreTopRow - 1][kTopScoreNameCol], 0);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol - 1], 0);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol + kTopScoreFieldWidth], 0);
        EXPECT_EQ(game.field.board, boardBefore);
        EXPECT_FALSE(game.highScores.topScoresRedrawRequested);
    }
}

// ── Test 6: LetterWheelUp ───────────────────────────────────────────────────────────────────────
// The wheel going up (tetris.asm:4025-4049), swept over its whole reachable domain in both modes:
// thirty glyphs normally, thirty-one in heart mode. The skip glyph jumps to the space and the space
// wraps to "a"; heart mode makes the heart the skip glyph, which is what makes it typeable.
TEST(HighScores, LetterWheelUp) {
    for (const bool heart : {false, true}) {
        const std::vector<CharTile> ring = wheelRing(heart);
        EXPECT_EQ(ring.size(), heart ? 31u : 30u);

        for (std::size_t i = 0; i < ring.size(); ++i) {
            GameContext game = nameEntryContext();
            game.flow.heartMode = heart ? 1 : 0;
            namedEntry(game).name[0] = ring[i];

            press(game, {Action::MenuUp});
            kirpich::systems::enterTopScore(game);

            EXPECT_EQ(namedEntry(game).name[0], ring[(i + 1) % ring.size()])
                << (heart ? "heart mode" : "normal") << " glyph " << int{tile(ring[i])};
            EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        }
    }
}

// ── Test 7: LetterWheelDown ─────────────────────────────────────────────────────────────────────
// The wheel going down (tetris.asm:4051-4077) is the exact reverse of the same ring: "a" drops to
// the space and the space drops to the skip glyph.
TEST(HighScores, LetterWheelDown) {
    for (const bool heart : {false, true}) {
        const std::vector<CharTile> ring = wheelRing(heart);

        for (std::size_t i = 0; i < ring.size(); ++i) {
            GameContext game = nameEntryContext();
            game.flow.heartMode = heart ? 1 : 0;
            namedEntry(game).name[0] = ring[i];

            press(game, {Action::MenuDown});
            kirpich::systems::enterTopScore(game);

            EXPECT_EQ(namedEntry(game).name[0], ring[(i + ring.size() - 1) % ring.size()])
                << (heart ? "heart mode" : "normal") << " glyph " << int{tile(ring[i])};
            EXPECT_EQ(game.audioCues.square, SquareSfxId::TINK);
        }
    }
}

// ── Test 8: NameEntryCursor ─────────────────────────────────────────────────────────────────────
// Moving across the six columns (tetris.asm:4079-4112). Both directions settle the glyph the cursor
// is leaving so a blink cannot strand a space; stepping right onto a cell nothing has been entered
// into seeds it with "a"; the left edge is a hard stop with no sound.
TEST(HighScores, NameEntryCursor) {
    // Right from the first column: the glyph settles into the map, the column advances, and the new
    // cell is seeded because it still holds the empty glyph.
    {
        GameContext game = nameEntryContext();
        TopScoreEntry& entry = namedEntry(game);
        entry.name.fill(CharTile::ELLIPSIS);
        entry.name[0] = CharTile::LETTER_K;

        press(game, {Action::Confirm});
        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], tile(CharTile::LETTER_K));
        EXPECT_EQ(game.highScores.nameEntryColumn, 1);
        EXPECT_EQ(namedEntry(game).name[1], CharTile::LETTER_A) << "seeded (:4091-4094)";
        EXPECT_EQ(game.audioCues.square, SquareSfxId::CHANGE_SCREEN);
    }

    // A cell already carrying a glyph is not re-seeded.
    {
        GameContext game = nameEntryContext();
        TopScoreEntry& entry = namedEntry(game);
        entry.name.fill(CharTile::ELLIPSIS);
        entry.name[0] = CharTile::LETTER_K;
        entry.name[1] = CharTile::LETTER_Z;

        press(game, {Action::Confirm});
        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(namedEntry(game).name[1], CharTile::LETTER_Z);
    }

    // Left from the middle: the glyph settles, the column retreats, and there is no sound.
    {
        GameContext game = nameEntryContext();
        game.highScores.nameEntryColumn = 3;
        namedEntry(game).name[3] = CharTile::LETTER_M;

        press(game, {Action::Back});
        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol + 3],
                  tile(CharTile::LETTER_M));
        EXPECT_EQ(game.highScores.nameEntryColumn, 2);
        EXPECT_EQ(game.audioCues.square, SquareSfxId::NONE) << "B is silent (:4102-4112)";
    }

    // Left at the first column is a no-op: no move, no glyph settled.
    {
        GameContext game = nameEntryContext();
        namedEntry(game).name[0] = CharTile::LETTER_M;

        press(game, {Action::Back});
        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(game.highScores.nameEntryColumn, 0);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], 0) << "nothing written";
        EXPECT_EQ(game.flow.gameState, GameState{}) << "and no transition";
    }

    // The cursor row follows the rank, which is the original's inverted counter: rank 1 is the worst
    // score and sits on the bottom of the three rows (:3952-3960).
    for (const std::uint8_t rank : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
        GameContext game = nameEntryContext(rank);
        namedEntry(game).name[0] = CharTile::LETTER_Q;

        press(game, {Action::Back});  // silent and blocked at column 0, so use Confirm instead
        press(game, {Action::Confirm});
        kirpich::systems::enterTopScore(game);

        const std::size_t row = kTopScoreTopRow + (kTopScoreRowCount - rank);
        EXPECT_EQ(game.display.map[row][kTopScoreNameCol], tile(CharTile::LETTER_Q))
            << "rank " << int{rank};
    }
}

// ── Test 9: NameEntryBlinkAndRepeat ─────────────────────────────────────────────────────────────
// The cursor blink (tetris.asm:3971-3983) and the wheel's key repeat (:3989, :4019-4024). The blink
// is gated on the frame timer and alternates the glyph with a space; the repeat is the same
// twenty-three-then-nine timeline the piece shift uses.
TEST(HighScores, NameEntryBlinkAndRepeat) {
    // While the frame timer runs, nothing blinks.
    {
        GameContext game = nameEntryContext();
        game.flow.timer1 = 3;
        namedEntry(game).name[0] = CharTile::LETTER_N;

        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], 0);
        EXPECT_EQ(game.flow.blinkCounter, 0);
        EXPECT_EQ(game.flow.timer1, 3) << "the frame loop owns the countdown, not the handler";
    }

    // On the frame it reaches zero the phase flips to 1, which shows a space, and the timer reloads.
    {
        GameContext game = nameEntryContext();
        game.flow.timer1 = 0;
        namedEntry(game).name[0] = CharTile::LETTER_N;

        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(game.flow.timer1, 7);
        EXPECT_EQ(game.flow.blinkCounter, 1);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], tile(CharTile::SPACE));

        // The next expiry flips back and restores the glyph.
        game.flow.timer1 = 0;
        kirpich::systems::enterTopScore(game);
        EXPECT_EQ(game.flow.blinkCounter, 0);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], tile(CharTile::LETTER_N));
    }

    // Key repeat: the press fires once and arms twenty-three; the twenty-third held frame fires and
    // reloads nine; every ninth held frame after that fires again.
    {
        GameContext game = nameEntryContext();
        namedEntry(game).name[0] = CharTile::LETTER_A;

        press(game, {Action::MenuUp});
        kirpich::systems::enterTopScore(game);
        EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_B) << "the press fires";
        EXPECT_EQ(game.flow.keyRepeatTimer, 23);

        for (int frame = 1; frame < 23; ++frame) {
            hold(game, {Action::MenuUp});
            kirpich::systems::enterTopScore(game);
            EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_B)
                << "held frame " << frame << " must not fire";
        }

        hold(game, {Action::MenuUp});
        kirpich::systems::enterTopScore(game);
        EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_C) << "the 23rd held frame fires";
        EXPECT_EQ(game.flow.keyRepeatTimer, 9);

        for (int frame = 1; frame < 9; ++frame) {
            hold(game, {Action::MenuUp});
            kirpich::systems::enterTopScore(game);
            EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_C)
                << "repeat frame " << frame << " must not fire";
        }

        hold(game, {Action::MenuUp});
        kirpich::systems::enterTopScore(game);
        EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_D) << "every ninth held frame fires";
    }

    // Up wins over down: the original tests up (pressed, then held) before it looks at down, and
    // every direction branch returns (:3990-3997).
    {
        GameContext game = nameEntryContext();
        namedEntry(game).name[0] = CharTile::LETTER_M;

        press(game, {Action::MenuUp, Action::MenuDown});
        kirpich::systems::enterTopScore(game);

        EXPECT_EQ(namedEntry(game).name[0], CharTile::LETTER_N);
    }
}

// ── Test 10: SubmitForkAndSave ──────────────────────────────────────────────────────────────────
// .submitName (tetris.asm:4004-4017). Start submits from any column, and stepping right off the last
// column submits too. Submitting settles the glyph, restores the menu music, clears the flag that
// routed here, hands the table to the save seam, and returns to the level picker for the game type
// just played. The port's save is an addition — the original has no persistence at all.
TEST(HighScores, SubmitForkAndSave) {
    // Type A, via Start.
    {
        GameContext game = nameEntryContext();
        namedEntry(game).name[0] = CharTile::LETTER_E;
        int saves = 0;
        std::uint32_t savedScore = 0;
        namedEntry(game).score = 7777;

        press(game, {Action::Start});
        kirpich::systems::enterTopScore(game, [&](const kirpich::HighScoreState& s) {
            ++saves;
            savedScore = s.typeA[0][0].score;
        });

        EXPECT_EQ(game.flow.gameState, GameState::TYPE_A_LEVEL_SELECTION);
        EXPECT_FALSE(game.highScores.newTopScore);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol], tile(CharTile::LETTER_E));
        EXPECT_EQ(game.audioCues.music, MusicId::TYPE_A) << "SwitchMusic restores the menu song";
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(savedScore, 7777u) << "the table is saved as it now stands";
    }

    // Type B forks to the other picker.
    {
        GameContext game = nameEntryContext(3, GameType::TYPE_B);
        int saves = 0;

        press(game, {Action::Start});
        kirpich::systems::enterTopScore(game,
                                        [&](const kirpich::HighScoreState&) { ++saves; });

        EXPECT_EQ(game.flow.gameState, GameState::TYPE_B_LEVEL_SELECTION);
        EXPECT_EQ(saves, 1);
    }

    // Stepping right off the last column submits, and the column is left where it was (:4084-4087).
    {
        GameContext game = nameEntryContext();
        game.highScores.nameEntryColumn = 5;
        namedEntry(game).name[5] = CharTile::LETTER_Y;
        int saves = 0;

        press(game, {Action::Confirm});
        kirpich::systems::enterTopScore(game,
                                        [&](const kirpich::HighScoreState&) { ++saves; });

        EXPECT_EQ(game.flow.gameState, GameState::TYPE_A_LEVEL_SELECTION);
        EXPECT_EQ(game.highScores.nameEntryColumn, 5) << "not advanced past the last column";
        EXPECT_EQ(saves, 1);
        EXPECT_EQ(game.display.map[kTopScoreTopRow][kTopScoreNameCol + 5],
                  tile(CharTile::LETTER_Y));
    }

    // A build with no save store still runs name entry: the seam defaults to empty.
    {
        GameContext game = nameEntryContext();
        press(game, {Action::Start});
        kirpich::systems::enterTopScore(game);
        EXPECT_EQ(game.flow.gameState, GameState::TYPE_A_LEVEL_SELECTION);
    }
}

// ── Type C ──────────────────────────────────────────────────────────────────────────────────────────

// Type C keeps its own table, indexed by the pair its round was picked as, and the shipped insert law
// reaches it unchanged: a tie does not displace, one point more takes the best rank and shifts the
// rest down.
TEST(HighScores, TypeCUsesItsOwnSliceByItsOwnLevelAndRise) {
    GameContext game;
    game.flow.gameType   = GameType::TYPE_C;
    game.flow.typeCLevel = 4;
    game.flow.typeCRise  = 2;
    game.flow.typeALevel = 4;  // the same index in Type A's table, which must not be touched
    seedSlice(game.highScores.typeC[4][2], 5000, 3000, 1000);
    seedSlice(game.highScores.typeA[4], 5000, 3000, 1000);
    game.engine.score = 5001;

    kirpich::systems::updateTypeCTopScores(game);

    EXPECT_EQ(game.highScores.typeC[4][2][0].score, 5001u) << "it took the best rank in the slice";
    EXPECT_EQ(game.highScores.typeC[4][2][1].score, 5000u) << "and the old best shifted down";
    EXPECT_EQ(game.highScores.typeC[4][2][2].score, 3000u);
    EXPECT_TRUE(game.highScores.newTopScore);

    EXPECT_EQ(game.highScores.typeA[4][0].score, 5000u) << "Type A's table at the same level is its own";

    // A tie does not displace, in Type C's slice as in every other.
    GameContext tie;
    tie.flow.gameType   = GameType::TYPE_C;
    tie.flow.typeCLevel = 4;
    tie.flow.typeCRise  = 2;
    seedSlice(tie.highScores.typeC[4][2], 9000, 5000, 5000);
    tie.engine.score = 5000;
    kirpich::systems::updateTypeCTopScores(tie);
    EXPECT_FALSE(tie.highScores.newTopScore);

    // Every pair keeps its own three entries: the same level at another rise, and another level at the
    // same rise, are both untouched by a round played at (4, 2).
    GameContext other;
    other.flow.gameType   = GameType::TYPE_C;
    other.flow.typeCLevel = 7;
    other.flow.typeCRise  = 5;
    other.engine.score    = 100;
    kirpich::systems::updateTypeCTopScores(other);
    EXPECT_EQ(other.highScores.typeC[7][5][0].score, 100u);
    EXPECT_EQ(other.highScores.typeC[7][2][0].score, 0u) << "the same level at another rise is its own";
    EXPECT_EQ(other.highScores.typeC[4][5][0].score, 0u) << "and another level at the same rise";
}

// The whole slice space is reachable and disjoint: a round at each of the sixty pairs writes its own
// entry and no other. A walk rather than a spot check, because the two indices are easy to transpose
// and a transposition is invisible on the diagonal.
TEST(HighScores, EveryTypeCLevelAndRiseHasItsOwnSlice) {
    GameContext game;
    game.flow.gameType = GameType::TYPE_C;

    std::uint32_t score = 100;
    for (std::uint8_t level = 0; level < 10; ++level) {
        for (std::uint8_t rise = 0; rise < 6; ++rise) {
            game.flow.typeCLevel = level;
            game.flow.typeCRise  = rise;
            game.engine.score    = score;
            kirpich::systems::updateTypeCTopScores(game);
            score += 100;
        }
    }

    score = 100;
    for (std::uint8_t level = 0; level < 10; ++level) {
        for (std::uint8_t rise = 0; rise < 6; ++rise) {
            EXPECT_EQ(game.highScores.typeC[level][rise][0].score, score)
                << "level " << int{level} << " rise " << int{rise};
            score += 100;
        }
    }
}

// The round's own type is the latch: a Type C round records into Type C's table and returns to Type
// C's level picker, whatever the settings say. Turning the modes off mid-round cannot strand a score
// in the wrong table, because nothing on this path reads the setting.
TEST(HighScores, AFinishedTypeCRoundRecordsToTypeC) {
    GameContext game;
    game.flow.gameType   = GameType::TYPE_C;
    game.flow.typeCLevel = 2;
    game.flow.typeCRise  = 3;
    game.engine.score    = 4242;

    kirpich::systems::updateTypeCTopScores(game);
    ASSERT_TRUE(game.highScores.newTopScore);
    ASSERT_EQ(game.highScores.newScoreRank, kTopScoreRowCount) << "the best rank";

    // Name entry writes into the entry the rank names, which is Type C's.
    game.flow.gameState = GameState::ENTER_TOP_SCORE;
    press(game, {Action::Start});
    kirpich::systems::enterTopScore(game);

    EXPECT_EQ(game.highScores.typeC[2][3][0].score, 4242u) << "the score is in Type C's table";
    EXPECT_EQ(game.highScores.typeA[2][0].score, 0u) << "and in no other";
    EXPECT_FALSE(game.highScores.newTopScore);
    EXPECT_EQ(game.flow.gameState, GameState::TYPE_C_LEVEL_SELECTION)
        << "and it returns to Type C's own level picker";
}
