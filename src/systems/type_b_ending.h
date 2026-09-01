#pragma once

// The Type B ending: what a player sees after clearing the last line of a Type B round. Each screen is
// one game state — the frame dispatcher runs its handler once per frame — so these are free functions
// on GameContext, the same shape as the piece, line-clear, scoring, and screen systems; they own no
// state of their own.
//
// The flow forks on the level the round was played at. A round below level 9 goes straight to
// typeBVictoryJingle, which draws the scoreboard, prints what each kind of line clear was worth, and
// hands off to the results count-up. A round on level 9 earns the dance first: initBonusEnding lays out
// the backdrop and the ten performers and starts a jingle, dancers animates them until that jingle ends,
// and then the round either launches the Buran (a round started at garbage height 5) or drops back to
// the scoreboard. The exact per-state effects, the screen-load order, the animation law, and the
// transitions — with source line anchors — are in docs/contracts/type-b-ending.md.
//
// What these do NOT do: draw anything. Redrawing the piece sprites and compiling the performers into the
// display are the renderer's job; the handlers mutate the board, the sprite-object slots, the object
// buffer, the audio cue mailbox, and the game-flow timers, counters, and state. Whether a song is still
// playing is a seam the sound system fills (see MusicPlayingQuery below).

#include <functional>

#include "systems/game_context.h"
#include "systems/stats.h"  // NowNanos

namespace kirpich::systems {

class GameStateDispatcher;

// Whether the sound driver reports a song still playing. The dance holds until it stops, so this is the
// one thing these handlers need that is not on GameContext; the sound system supplies it as
// `[&sound] { return sound.currentMusic().has_value(); }` when the two are wired together.
//
// The default — no query, meaning no song — ends the dance rather than holding it. That direction is
// the safe one: a build with no sound wired shows a short dance, where the opposite default would
// animate forever and never finish the round.
using MusicPlayingQuery = std::function<bool()>;

// ── State handlers ──────────────────────────────────────────────────────────────────────────────────

// GameState_05 — the scoreboard: once the frame timer expires, draw the scoreboard screen into the
// field, print each line-clear kind's value for this round's level (levels above 0 only — the drawn
// screen already carries the level-0 values), zero the score, hide both piece sprites, re-initialise the
// sound driver, and hand off to the results count-up.
//
// Winning is one of the two ways a Type B round ends, so `now` closes the round's record here. It is
// read on entry, ahead of the timer gate, because that is the frame on which play stopped.
void typeBVictoryJingle(GameContext& game, const NowNanos& now = {});

// GameState_22 — lay out the dance: once the frame timer expires, draw the dance backdrop into the
// field, load the ten performers into the first ten sprite slots, seed each with its own animation
// period, reveal one more than the starting garbage height (all ten at height 5), and cue that height's
// jingle. This is the other way a Type B round ends - a level 9 win comes here instead of to the
// scoreboard - so `now` closes the round's record here too.
void initBonusEnding(GameContext& game, const NowNanos& now = {});

// GameState_23 — the dance: step each performer's animation counter, flipping its sprite to the other
// frame when the counter runs out (and moving the jumping cossack up and down with it). Runs until
// `musicPlaying` reports the jingle has ended, then leaves for the Buran launch (starting garbage
// height 5) or the scoreboard.
void dancers(GameContext& game, const MusicPlayingQuery& musicPlaying = {});

// ── Installer ───────────────────────────────────────────────────────────────────────────────────────

// Install the three Type B ending handlers into their dispatch slots ($05, $22, $23). `musicPlaying` is
// the seam the dance exits on; without it the dance ends on its first eligible frame. `now` is the
// clock the two entry states close the round's record against; without it the round is still recorded,
// at a length of zero.
void installTypeBEndingHandlers(GameStateDispatcher& dispatcher, MusicPlayingQuery musicPlaying = {},
                                NowNanos now = {});

}  // namespace kirpich::systems
