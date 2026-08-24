#pragma once

// Top-score recording: comparing a finished round's score against the stored table, inserting it,
// staging the three ranked rows for display, flushing them to the background map, and running the
// name-entry screen where the player spells a name for a score that made the table.
//
// The flow. Each difficulty screen refreshes its table by calling one of the two slice walks
// (updateTypeATopScores / updateTypeBTopScores), which pick the (level) or (level, height) group of
// three ranked entries, compare the round's score against them, insert it if it beat one, re-stage
// all three rows, and ask for a redraw. An insert also seeds a blank name and sets the new-top-score
// flag, which routes the difficulty screen into name entry instead of level selection. enterTopScore
// runs that screen: a blinking cursor over the six name cells, a letter wheel on up and down, A and B
// to move across the columns, and Start to submit. Submitting hands the finished table to the save
// seam and returns to the level picker.
//
// Two grids, written differently. The three staged rows live in the board (PlayingFieldState), which
// is the game's own copy of the background; drawTopScoresToVram copies them into the displayed map on
// the frame a redraw was asked for. Name entry does not go through the board at all - it writes the
// cursor glyph straight into the map, so the board keeps whatever the staged row last held for that
// cell. Both destinations, and why they differ, are tabulated in docs/contracts/screen.md section 5.
//
// Scores are decimal here, as they are everywhere in the port; the packed-decimal wire form belongs
// to the persistence codec. The full behaviour - slice geometry, the rank inversion, the display
// quirks preserved verbatim, and the letter wheel's exact domain - is in
// docs/contracts/high-score-state.md.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "state/high_score_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// ── Where the three ranked rows sit ─────────────────────────────────────────────────────────────
//
// The staged rows occupy the same coordinates in the board and in the map: three rows from row 13,
// each fourteen cells wide from column 4 - a six-cell name, a two-cell gap, then a six-digit score.
// The board and map addresses the original uses ($C9A4 and $99A4) are one $3000 offset apart, the
// same fixed relation every other board-to-map carry uses.

inline constexpr std::size_t kTopScoreRowCount   = 3;   // three ranked entries per slice
inline constexpr std::size_t kTopScoreTopRow     = 13;  // $C9A4 / $99A4
inline constexpr std::size_t kTopScoreNameCol    = 4;
inline constexpr std::size_t kTopScoreNameLength = 6;   // six name glyphs
inline constexpr std::size_t kTopScoreScoreCol   = 12;  // $C9AC / $99AC - after a two-cell gap
inline constexpr std::size_t kTopScoreDigits     = 6;   // three packed-decimal pairs
inline constexpr std::size_t kTopScoreFieldWidth = 14;  // name + gap + score

// The frame interval the name-entry cursor blinks on, and the reload the wheel's key repeat shares
// with the piece shift (systems/input.h owns the repeat constants themselves).
inline constexpr std::uint8_t kNameEntryBlinkInterval = 7;

// ── The save seam ───────────────────────────────────────────────────────────────────────────────
//
// Called with the finished table when a name is submitted. The port persists top scores across
// launches, which the original cannot do; keeping it a seam is what leaves this layer free of the
// engine's save types. The default is empty, so a build with no save store still runs name entry.
using TopScoreSaved = std::function<void(const HighScoreState&)>;

// ── The routines ────────────────────────────────────────────────────────────────────────────────

// Fill all three staged rows with the empty-cell glyph. Both slice walks begin with this, so a rank
// whose score prints nothing leaves the ellipses showing.
void clearTopScoreFields(GameContext& game);

// Refresh the Type A table for the chosen level: clear the staged rows, compare and possibly insert
// the round's score, re-stage all three rows, clear the score and statistics, and ask for a redraw.
void updateTypeATopScores(GameContext& game);

// The Type B equivalent, for the chosen level and starting height.
void updateTypeBTopScores(GameContext& game);

// The same, for Type C's table: the slice for the level its own difficulty screen has chosen.
void updateTypeCTopScores(GameContext& game);

// Copy the staged names and scores from the board into the displayed map, then clear the redraw
// request. The two-cell gap between each name and its score is stepped over, not copied, so whatever
// the screen's backdrop put there survives. Does nothing unless a redraw was asked for. The frame
// loop calls this on every tick as part of the frame's last beat, where the original runs it.
void drawTopScoresToVram(GameContext& game);

// The name-entry screen, run once per frame while it is the current state. Blinks the cursor, walks
// the letter wheel on up and down, moves across the six columns on A and B, and submits on Start -
// which cues the menu music, clears the new-top-score flag, hands the table to `saved`, and returns
// to the level picker for the game type just played.
void enterTopScore(GameContext& game, const TopScoreSaved& saved = {});

// Install the name-entry handler into its dispatch slot, binding the save seam.
void installHighScoreHandlers(GameStateDispatcher& dispatcher, const TopScoreSaved& saved = {});

}  // namespace kirpich::systems
