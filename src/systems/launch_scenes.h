#pragma once

// The launch scenes: the two bonus endings the game hides behind its hardest achievements. Winning a
// Type B round started at garbage height 5 launches the Buran shuttle; scoring 100 000 points in Type A
// launches a rocket, and which of three rockets it is depends on the score. Each screen is one game
// state — the frame dispatcher runs its handler once per frame — so these are free functions on
// GameContext, the same shape as the piece, line-clear, scoring, menu, title, gameplay, and Type B
// ending systems; they own no state of their own.
//
// Both chains are pure spectacle. Nothing here reads the joypad, so neither sequence can be skipped or
// hurried, and nothing here changes a score, a level, or a line count. Each is a fixed run of timed
// steps: build a launch pad, hold, reveal the smoke, ignite, fly the vehicle off the top of the screen,
// and hand back to a screen that already exists — the Buran to the Type B scoreboard, the rocket to the
// Type A difficulty screen.
//
// The two chains are near-mirrors of each other but they are not the same, and every place they differ
// is preserved: the rocket pad has no left tower and no umbilicals, its ignition swaps no smoke art,
// it has no congratulations screen, and its exit re-initialises the sound driver where the Buran's does
// not. The exact per-state effects, the pad geometry, the climb law, and those asymmetries — with
// source line anchors — are in docs/contracts/launch-scenes.md.
//
// Everything both scenes draw goes into the second background map, never the first, so a playing field
// left underneath survives the whole sequence.
//
// What these do NOT do: draw anything. Compiling the objects into the display and copying tile art are
// the renderer's job; the handlers mutate the second background map, the sprite-object slots, the audio
// cue mailbox, and the game-flow timers, cursor, and state.

#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// ── The Buran chain ─────────────────────────────────────────────────────────────────────────────────

// GameState_26 — build the Buran pad: clear the second map, lay down the backdrop and both towers,
// paint the umbilicals and crew tunnel, place the shuttle and its two (hidden) smoke plumes, switch the
// display to the second map, and start the launch music.
void initBuran(GameContext& game);

// GameState_27 — once the frame timer expires, reveal both smoke plumes and hold for the maximum a
// single timer can count.
void prepareBuranLaunch(GameContext& game);

// GameState_28 — hold, flickering the smoke each time the second timer runs out. On expiry, swap both
// plumes to their second frame, clear the playing field, and hold again.
void buranIgnition(GameContext& game);

// GameState_29 — hold, still flickering. On expiry, swing the umbilicals and crew tunnel away by
// overwriting their four cells with spaces.
void buranIgnition2(GameContext& game);

// GameState_02 — raise the shuttle one pixel per step until it reaches the ignition height exactly,
// then light the exhaust, hide the second plume, and cue the flight sound.
void buranLiftoff(GameContext& game);

// GameState_03 — fly the shuttle and its exhaust up together, alternating the exhaust's two frames,
// until the shuttle's coordinate wraps past zero to the terminal height; then seed the congratulations
// cursor.
void buranRising(GameContext& game);

// GameState_2C — print the congratulations message one letter every six frames, each with a fixed tile
// beneath it and a sound, until the cursor passes the last column.
void printCongratulations(GameContext& game);

// GameState_2D — once the frame timer expires, restore the gameplay tile art, clear the line-clear
// list, switch back to the first map, and hand off to the Type B scoreboard. Does not re-initialise
// the sound driver; the rocket chain's exit does.
void congratulations(GameContext& game);

// ── The rocket chain ────────────────────────────────────────────────────────────────────────────────

// GameState_34 — hold on the game-over screen until the frame timer expires, then enter the rocket
// chain. The game-over chain sets that timer before writing this state.
void gameOverToBonusEnding(GameContext& game);

// GameState_2E — build the rocket pad: the shared pad only, with no left tower and no umbilicals. Place
// the rocket the score earned (consuming the recorded tier), switch to the second map, and start the
// launch music.
void initRocketLaunch(GameContext& game);

// GameState_2F — once the frame timer expires, reveal both smoke plumes and hold.
void rocket(GameContext& game);

// GameState_30 — hold, flickering the smoke. On expiry, clear the playing field and hold again. Unlike
// the Buran's ignition, this sets no smoke art.
void rocketIgnition(GameContext& game);

// GameState_31 — raise the rocket one pixel per step until it reaches its ignition height exactly, then
// light the exhaust, hide the second plume, and cue the flight sound.
void rocketLiftoff(GameContext& game);

// GameState_32 — fly the rocket and its exhaust up together, alternating the exhaust's two frames,
// until the rocket's coordinate wraps past zero to its terminal height. Seeds no cursor — the rocket
// chain has no congratulations screen.
void rocketMainEngineFire(GameContext& game);

// GameState_33 — restore the gameplay tile art, re-initialise the sound driver, clear the line-clear
// list, switch back to the first map, and hand off to the Type A difficulty screen. Runs on its first
// frame: unlike every other handler here, it has no timer gate.
void endOfBonusScene(GameContext& game);

// ── Installer ───────────────────────────────────────────────────────────────────────────────────────

// Install all fifteen launch-scene handlers into their dispatch slots. Both entry states — the Buran
// fork out of the ending dance and the game-over chain's bonus state — resolve to real handlers only
// after this runs.
void installLaunchSceneHandlers(GameStateDispatcher& dispatcher);

}  // namespace kirpich::systems
