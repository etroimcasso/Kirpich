#pragma once

// The scoring pipeline: how a line clear, a soft drop, and a completed level turn into points, and
// how the Type B results screen counts those points up at the end of a round. Five free functions on
// GameContext, the same shape as the piece and line-clear systems — they own no state of their own;
// everything they read or write lives on the GameContext passed by reference.
//
// The three award paths run in three different points of the frame. addLineClearScore is the Type A
// live award: it runs in the gameplay handler beat, at one exact step of the field wipe, and folds a
// finished line clear into the score. updateScoreboard is the Type B end-of-round tally: it runs in
// the vertical-blank pass and drains the per-kind clear counts and the soft-drop total into the score
// one unit at a time, animating the count-up. checkForLevelUp is the Type A level-up: it runs from
// one step of the field wipe (the line-clear system calls it there) and bumps the level and gravity
// when the line count has passed the next ten. The exact execution-context law, with source line
// anchors, is in docs/contracts/scoring-system.md.
//
// The award math itself lives in the data layer (src/data/scoring.h): lineClearAward, softDropAward,
// and shouldLevelUp are pure functions these handlers call. The score is a decimal integer with a
// 999,999 ceiling; the original's packed-decimal shadow is a print-time detail the render bridge
// owns, not carried here.

#include <cstdint>

#include <kirpich/line_clear_kind.h>

#include "systems/game_context.h"

namespace kirpich::systems {

// The Type A live line-clear award. Runs in the gameplay handler beat and does nothing unless a Type A
// game is in normal gameplay at wipe step 5. Consumes the first non-empty per-kind clear stat (single
// before double before triple before tetris), zeroing it, and adds base x (level + 1) to the score,
// saturating at the 999,999 ceiling. With no pending clear it does nothing.
void addLineClearScore(GameContext& game);

// One vertical-blank step of the Type B results count-up. Does nothing unless the tally phase is armed
// (the Type B scoreboard handler re-arms it on its timer). Drives a small per-kind state machine over
// the four clear counts and then the soft-drop total: each armed step moves one unit from a pending
// count into the on-screen display and folds its points into the score, and when a count reaches zero
// it advances to the next kind — reaching the game-over screen after the last. Two frames animate each
// unit for the per-kind counts; the soft-drop drain speeds itself up to one unit per frame.
void updateScoreboard(GameContext& game);

// The Type A level-up check. The line-clear system calls this from wipe step 16. Does nothing unless a
// Type A game is in normal gameplay below the level cap and the line count has passed the next ten
// (the data layer's shouldLevelUp law, which also stops levelling once 1000 lines are reached). When
// it fires it advances the level by one, cues the level-up sound, and reloads the gravity countdown
// for the new level (heart mode included) — one level per call.
void checkForLevelUp(GameContext& game);

// Print one line-clear kind's Type B scoreboard row: the value base x (typeBLevel + 1) written as
// decimal digits, left-aligned with leading zeros suppressed, into consecutive field cells starting at
// (fieldRow, fieldCol) — one raw digit byte (0-9) per cell, cells beyond the digits left untouched.
// The base is the kind's award-table entry. The score is not touched. A zero value writes nothing
// (unreachable for the real award bases).
void printLineClearScores(GameContext& game, LineClearKind kind, std::uint8_t fieldRow,
                          std::uint8_t fieldCol);

// Zero the whole scoring state: the score, the per-kind clear stats and their display counts, the
// soft-drop total and its count-up display, and the scoreboard state-machine bytes. The post-lock
// soft-drop latch and the score-redraw flag are left alone (they sit just past the original's zeroed
// span). Called from the game-init paths that start a fresh game.
void clearScoreAndStats(GameContext& game);

}  // namespace kirpich::systems
