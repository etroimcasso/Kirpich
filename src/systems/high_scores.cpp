#include "systems/high_scores.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <kirpich/action.h>
#include <kirpich/char_tile.h>
#include <kirpich/game_state.h>
#include <kirpich/game_type.h>

#include "data/music.h"
#include "data/sfx.h"
#include "retropp/input.h"  // actionId
#include "state/high_score_state.h"
#include "systems/game_context.h"
#include "systems/game_state_dispatcher.h"
#include "systems/input.h"         // keyRepeatFire
#include "systems/menu_screens.h"  // switchMusic
#include "systems/scoring.h"       // clearScoreAndStats

namespace kirpich::systems {

namespace {

// One slice: the three ranked entries for a (level) or (level, height) group. Index 0 is the best
// score and prints on the top row.
using Slice = std::array<TopScoreEntry, kTopScoreRowCount>;

bool pressed(const GameContext& game, Action action) {
    return game.joypad.pressed.test(retropp::actionId(action));
}

bool held(const GameContext& game, Action action) {
    return game.joypad.held.test(retropp::actionId(action));
}

std::uint8_t tile(CharTile glyph) {
    return static_cast<std::uint8_t>(glyph);
}

// PrintTopScore (tetris.asm:3694-3722). Print a score's six digits into one staged row.
//
// Leading zeros are skipped rather than blanked - the destination advances over them and the cell
// keeps whatever it already held, which is the empty-cell glyph the field clear left there. The
// disassembly marks this a bug; it is what the screen shows, so it is what the port does. Once the
// first non-zero digit is reached every remaining digit prints, zeros included, and a score of zero
// prints nothing at all.
void printTopScore(GameContext& game, std::uint32_t score, std::size_t row) {
    std::array<std::uint8_t, kTopScoreDigits> digits{};
    std::uint32_t remaining = score;
    for (std::size_t i = kTopScoreDigits; i-- > 0;) {
        digits[i] = static_cast<std::uint8_t>(remaining % 10);
        remaining /= 10;
    }

    bool printing = false;
    for (std::size_t i = 0; i < kTopScoreDigits; ++i) {
        if (!printing && digits[i] == 0) {
            continue;  // skipped, not blanked
        }
        printing = true;
        game.field.board[row][kTopScoreScoreCol + i] = digits[i];
    }
}

// UpdateTopScores .printTopScores (tetris.asm:3835-3885). Stage all three ranks: each score into its
// row's digit cells, each name into its row's six name cells. A name is delimited by a zero glyph,
// and the cells past the delimiter keep the empty-cell glyph.
void printSlice(GameContext& game, const Slice& slice) {
    for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
        printTopScore(game, slice[rank].score, kTopScoreTopRow + rank);
    }

    for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
        const std::size_t row = kTopScoreTopRow + rank;
        for (std::size_t col = 0; col < kTopScoreNameLength; ++col) {
            const std::uint8_t glyph = tile(slice[rank].name[col]);
            if (glyph == 0) {
                break;
            }
            game.field.board[row][kTopScoreNameCol + col] = glyph;
        }
    }
}

// UpdateTopScores (tetris.asm:3737-3890). Compare the round's score against the slice's three
// entries, insert it if it beat one, stage all three rows, and ask for a redraw.
//
// The comparison is strictly greater: the original walks the packed-decimal pairs high pair first
// and takes the new-top-score branch only on a borrow, so an equal score falls through every pair to
// the next rank and ultimately does not displace anything (:3748-3756).
void updateTopScores(GameContext& game, Slice& slice) {
    const std::uint32_t score = game.engine.score;

    std::size_t insertAt = kTopScoreRowCount;  // past the end - no insert
    for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
        if (score > slice[rank].score) {
            insertAt = rank;
            break;
        }
    }

    if (insertAt < kTopScoreRowCount) {
        // Shift the entries this one displaces down a rank, last first, dropping the old worst
        // (:3780-3812 shifts scores and names in two passes; the port moves whole entries).
        for (std::size_t rank = kTopScoreRowCount - 1; rank > insertAt; --rank) {
            slice[rank] = slice[rank - 1];
        }

        slice[insertAt].score = score;

        // The seed name: "a" in the first cell and the empty-cell glyph in the other five
        // (:3814-3823). The first cell starts on "a" because that is where the wheel starts.
        slice[insertAt].name.fill(CharTile::ELLIPSIS);
        slice[insertAt].name[0] = CharTile::LETTER_A;

        // The rank is the original's countdown, kept verbatim: 3 while checking against the best
        // entry, so beating the best records a 3 (:3797-3798).
        game.highScores.newScoreRank =
            static_cast<std::uint8_t>(kTopScoreRowCount - insertAt);
        game.highScores.nameEntryColumn = 0;  // :3830
        game.flow.blinkCounter          = 0;  // :3829
        game.audioCues.music            = MusicId::TOP_SCORE;  // :3831-3832
        game.highScores.newTopScore     = true;                // :3833
    }

    printSlice(game, slice);
    clearScoreAndStats(game);                          // :3887
    game.highScores.topScoresRedrawRequested = true;   // :3888-3889
}

// ── Name entry ──────────────────────────────────────────────────────────────────────────────────

// The glyph the wheel jumps to the space from, going up, and lands on from the space, going down.
// Heart mode swaps the multiplication sign for a heart, which is what makes a heart typeable
// (tetris.asm:4027-4031, :4059-4063) - the last of heart mode's effects.
CharTile skipGlyph(const GameContext& game) {
    return game.flow.heartMode != 0 ? CharTile::HEART : CharTile::MULTIPLICATION_SIGN;
}

// The wheel going up (tetris.asm:4025-4049): the skip glyph jumps to the space, the space wraps back
// to "a", everything else advances one glyph.
CharTile wheelUp(CharTile current, CharTile skip) {
    if (current == skip) {
        return CharTile::SPACE;
    }
    if (current == CharTile::SPACE) {
        return CharTile::LETTER_A;
    }
    return static_cast<CharTile>(tile(current) + 1);
}

// The wheel going down (tetris.asm:4051-4077): the exact reverse - "a" drops to the space, the space
// drops to the skip glyph, everything else retreats one glyph.
CharTile wheelDown(CharTile current, CharTile skip) {
    if (current == CharTile::LETTER_A) {
        return CharTile::SPACE;
    }
    if (current == CharTile::SPACE) {
        return skip;
    }
    return static_cast<CharTile>(tile(current) - 1);
}

// The entry being named. It is not stored anywhere: the original keeps a pointer to the name cell
// across frames, but the pointer is fully derived from the game type, the chosen level and height,
// and the rank - so the port recomputes it (:3967-3970 reads the pointer back, :4095-4099 rewrites
// it as the column moves).
//
// The rank is the inverted counter, so rank 3 is the best entry and index 0. A rank outside 1..3
// cannot occur here: this state is only entered behind the new-top-score flag, which is only ever
// set together with a rank. Returning null on one keeps a corrupt rank from indexing the table.
TopScoreEntry* namedEntry(GameContext& game) {
    const std::uint8_t rank = game.highScores.newScoreRank;
    if (rank < 1 || rank > kTopScoreRowCount) {
        return nullptr;
    }
    const std::size_t index = kTopScoreRowCount - rank;

    switch (game.flow.gameType) {
        case GameType::TYPE_B:
            return &game.highScores.typeB[game.flow.typeBLevel][game.flow.typeBStartHeight][index];
        case GameType::TYPE_C:
            return &game.highScores.typeC[game.flow.typeCLevel][index];
        default:
            return &game.highScores.typeA[game.flow.typeALevel][index];
    }
}

// The map row the cursor sits on. The original starts at the bottom of the three staged rows and
// steps up once per rank (:3952-3960), so rank 3 - the best entry - is the top row.
std::size_t cursorRow(std::uint8_t rank) {
    return kTopScoreTopRow + (kTopScoreRowCount - rank);
}

// PrintCharacter (tetris.asm:4114-4122): write one glyph into the displayed map. The original waits
// for the scanline blanking interval first, which is timing, not state.
void printCharacter(GameContext& game, std::size_t row, std::size_t col, CharTile glyph) {
    game.display.map[row][col] = tile(glyph);
}

// .submitName (tetris.asm:4004-4017): print the glyph the cursor is on so a blink cannot leave a
// space behind, restore the menu music, clear the flag that routed here, persist the table, and
// return to the level picker for the game type just played.
void submitName(GameContext& game, TopScoreEntry& entry, std::size_t row, std::size_t col,
                const TopScoreSaved& saved) {
    printCharacter(game, row, kTopScoreNameCol + col, entry.name[col]);
    switchMusic(game);
    game.highScores.newTopScore = false;

    if (saved) {
        saved(game.highScores);
    }

    // Back to the level picker for the mode just played - each mode has its own.
    switch (game.flow.gameType) {
        case GameType::TYPE_B:
            game.flow.gameState = GameState::TYPE_B_LEVEL_SELECTION;
            break;
        case GameType::TYPE_C:
            game.flow.gameState = GameState::TYPE_C_LEVEL_SELECTION;
            break;
        default:
            game.flow.gameState = GameState::TYPE_A_LEVEL_SELECTION;
            break;
    }
}

}  // namespace

void clearTopScoreFields(GameContext& game) {
    // ClearTopScoreFields (tetris.asm:3934-3950): fourteen empty cells on each of the three rows -
    // the name field, the gap, and the score field together.
    for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
        for (std::size_t col = 0; col < kTopScoreFieldWidth; ++col) {
            game.field.board[kTopScoreTopRow + rank][kTopScoreNameCol + col] =
                tile(CharTile::ELLIPSIS);
        }
    }
}

void updateTypeATopScores(GameContext& game) {
    // UpdateTypeATopScores (tetris.asm:3641-3659): one slice of three entries per level.
    clearTopScoreFields(game);
    updateTopScores(game, game.highScores.typeA[game.flow.typeALevel]);
}

void updateTypeCTopScores(GameContext& game) {
    // Type C's own slice, one of three entries per level - the shape Type A's table has.
    clearTopScoreFields(game);
    updateTopScores(game, game.highScores.typeC[game.flow.typeCLevel]);
}

void updateTypeBTopScores(GameContext& game) {
    // UpdateTypeBTopScores (tetris.asm:3661-3689): one slice per level and starting height.
    clearTopScoreFields(game);
    updateTopScores(
        game, game.highScores.typeB[game.flow.typeBLevel][game.flow.typeBStartHeight]);
}

void drawTopScoresToVram(GameContext& game) {
    // DrawTopScoresToVRAM (tetris.asm:3893-3932), run from the frame's last beat (:235). The staged
    // rows live in the board and the displayed rows in the map, at the same coordinates.
    if (!game.highScores.topScoresRedrawRequested) {
        return;
    }

    // Two fields per row, six cells each, and the two-cell gap between them is stepped over rather
    // than copied (:3910-3913 advances both pointers twice without a write). The gap keeps whatever
    // the screen's backdrop put there; only the name and the score come from the board.
    for (std::size_t rank = 0; rank < kTopScoreRowCount; ++rank) {
        const std::size_t row = kTopScoreTopRow + rank;
        for (const std::size_t start : {kTopScoreNameCol, kTopScoreScoreCol}) {
            for (std::size_t col = start; col < start + kTopScoreNameLength; ++col) {
                game.display.map[row][col] = game.field.board[row][col];
            }
        }
    }

    game.highScores.topScoresRedrawRequested = false;
}

void enterTopScore(GameContext& game, const TopScoreSaved& saved) {
    // GameState_15 (tetris.asm:3952-4112).
    TopScoreEntry* entry = namedEntry(game);
    if (entry == nullptr) {
        return;
    }

    const std::uint8_t rank = game.highScores.newScoreRank;
    const std::size_t  row  = cursorRow(rank);
    const std::size_t  col  = game.highScores.nameEntryColumn;

    // The cursor blink (:3971-3983). On the frames the frame timer reaches zero the phase flips and
    // the cell shows the glyph or a space, whichever the new phase selects; the timer reloads seven.
    if (game.flow.timer1 == 0) {
        game.flow.timer1 = kNameEntryBlinkInterval;
        game.flow.blinkCounter ^= 1;
        printCharacter(game, row, kTopScoreNameCol + col,
                       game.flow.blinkCounter == 0 ? entry->name[col] : CharTile::SPACE);
    }

    // The input walk (:3984-4003) tests up before down and both before the buttons, and every branch
    // returns - so a held direction is the whole frame's input.
    const bool upPressed   = pressed(game, Action::MenuUp);
    const bool upHeld      = held(game, Action::MenuUp);
    const bool downPressed = pressed(game, Action::MenuDown);
    const bool downHeld    = held(game, Action::MenuDown);

    if (upPressed || upHeld) {
        if (keyRepeatFire(game.flow.keyRepeatTimer, upPressed, upHeld)) {
            entry->name[col]        = wheelUp(entry->name[col], skipGlyph(game));
            game.audioCues.square   = SquareSfxId::TINK;
        }
        return;
    }

    if (downPressed || downHeld) {
        if (keyRepeatFire(game.flow.keyRepeatTimer, downPressed, downHeld)) {
            entry->name[col]        = wheelDown(entry->name[col], skipGlyph(game));
            game.audioCues.square   = SquareSfxId::TINK;
        }
        return;
    }

    if (pressed(game, Action::Confirm)) {
        // .pressedA (:4079-4100): settle the current glyph, then step right. Stepping off the last
        // column submits instead - and the original leaves the column where it was when it does.
        printCharacter(game, row, kTopScoreNameCol + col, entry->name[col]);
        game.audioCues.square = SquareSfxId::CHANGE_SCREEN;

        const std::size_t next = col + 1;
        if (next == kTopScoreNameLength) {
            submitName(game, *entry, row, col, saved);
            return;
        }

        game.highScores.nameEntryColumn = static_cast<std::uint8_t>(next);
        if (entry->name[next] == CharTile::ELLIPSIS) {
            entry->name[next] = CharTile::LETTER_A;  // nothing entered here yet - start at "a"
        }
        return;
    }

    if (pressed(game, Action::Back)) {
        // .pressedB (:4102-4112): settle the current glyph and step left. The first column is the
        // left edge; there is no sound on this one and no seeding.
        if (col == 0) {
            return;
        }
        printCharacter(game, row, kTopScoreNameCol + col, entry->name[col]);
        game.highScores.nameEntryColumn = static_cast<std::uint8_t>(col - 1);
        return;
    }

    if (pressed(game, Action::Start)) {
        submitName(game, *entry, row, col, saved);
    }
}

void installHighScoreHandlers(GameStateDispatcher& dispatcher, const TopScoreSaved& saved) {
    dispatcher.setHandler(GameState::ENTER_TOP_SCORE,
                          [saved](GameContext& g) { enterTopScore(g, saved); });
}

}  // namespace kirpich::systems
