#pragma once

// The gameplay session: the states a round passes through from setup to game over. Each is one game
// state — the frame dispatcher runs its handler once per frame — so these are free functions on
// GameContext, the same shape as the piece, line-clear, scoring, and screen systems; they own no state
// of their own.
//
// The flow: initGame sets a round up and enters normalGameplay, which runs one frame of play — the
// piece, line-clear, and scoring systems in a fixed order — until the stack tops out. Topping out
// enters initGameOver, which starts the curtain; gameOverCurtain paints the game-over screen and picks
// the ending (a high enough Type A score earns a rocket); gameOverScreen waits for the player and
// returns to the menu the round came from. initTypeBScoreboard drives the Type B results count-up.
//
// One init serves every mode. A Type A round, a Type B round, and an attract demo all run initGame and
// normalGameplay, forking internally on the game type; only a two-player round runs its own gameplay
// state. The pause routines below are shared with that two-player round.
//
// The exact per-state effects, the frame's step order, the pause and unpause laws, the ending
// thresholds, and the transitions — with source line anchors — are in docs/contracts/gameplay.md.
//
// What these do NOT do: draw anything. Turning the screen on, loading tilemaps, printing the level and
// pause text, and compiling the piece sprites into the display are the renderer's job; the handlers
// mutate the board, the sprite-object slots, the object buffer, the audio cue mailbox, and the
// game-flow timers, counters, and state. Demo playback and recording, garbage filling, and the whole
// reset are seams other systems fill (see the hooks below and the contract).

#include <cstdint>
#include <functional>

#include "systems/game_context.h"
#include "systems/stats.h"  // NowNanos

namespace kirpich::systems {

class GameStateDispatcher;

// The four seams the gameplay frame calls where the original checks for the end of a demo, substitutes
// the demo's recorded input, records input, and restores the player's real input. All four do nothing
// during ordinary play, so every default is empty and an empty hook is skipped; demo playback and
// recording install the real ones, and tests pass probes to confirm each fires at its step.
struct GameplayDemoHooks {
    std::function<void(GameContext&)> checkForEndOfDemo;
    std::function<void(GameContext&)> simulateJoypad;
    std::function<void(GameContext&)> recordDemo;
    std::function<void(GameContext&)> restoreSavedJoypad;
};

// The seam the Type B init fires to fill the starting garbage: `rows` rows of it, taken from the fixed
// demo table when `useDemoTable` is set and generated otherwise. The default is a no-op — a build
// without the Type B work starts on an empty field — and the Type B unit installs the real fill.
using InitGarbageHook = std::function<void(GameContext& game, std::uint8_t rows, bool useDemoTable)>;

// The seam the Start+Select+B+A chord fires. The frame dispatcher carries the same seam and tests the
// same chord each tick; the gameplay frame tests it a second time, as the original does.
using SoftResetHook = std::function<void()>;

// ── State handlers ──────────────────────────────────────────────────────────────────────────────────

// GameState_0A — set a round up: clear the entry state, the board, and the score; pick the starting
// level and line count for the game type; load the gravity period; fill the piece pipeline; lay the
// Type B starting garbage; and enter play. `draw` supplies the piece randomizer.
//
// `now` reads the clock the round is timed against; it starts the round's own recording before the
// score is cleared, so a round left open by an earlier abandonment is closed with the score it
// earned. Without it the round still counts, at a length of zero.
void initGame(GameContext& game, const std::function<std::uint8_t()>& draw,
              const InitGarbageHook& initGarbage = {}, const NowNanos& now = {});

// GameState_00 — one frame of play: handle Start and Select, then (unless paused) run the demo input
// substitution, the piece, the line-clear scan and compaction, and the score award, in that order.
void normalGameplay(GameContext& game, const GameplayDemoHooks& demo = {},
                    const SoftResetHook& softReset = {}, const NowNanos& now = {});

// GameState_01 — start the game-over curtain: hide the piece sprites, clear the line-clear list, fill
// the field with the curtain tile (which starts the wipe), and arm the curtain timer. Topping out is
// where a round ends, so this is where its counts reach the tables.
void initGameOver(GameContext& game, const NowNanos& now = {});

// GameState_0D — the game-over curtain: once the timer expires, cue the game-over music and either
// hand off to the two-player end jingle or paint the solo game-over screen and pick its ending.
void gameOverCurtain(GameContext& game);

// GameState_04 — the game-over screen: wait for A or Start, then return to the difficulty screen the
// round came from.
void gameOverScreen(GameContext& game);

// GameState_0B — re-arm one unit of the Type B results count-up: once the timer expires, set the
// count-up phase and reload the timer.
void initTypeBScoreboard(GameContext& game);

// GameState_0C — waits for any button and advances to the first bonus-ending scene. Nothing reaches
// this state: no code path can write its index (the proof is in the contract). It is carried because it
// is a real entry in the dispatch table. The name is the one the state enum uses, because the original
// gives no clue what the state was for.
void state0CUnknown(GameContext& game);

// ── Pause ───────────────────────────────────────────────────────────────────────────────────────────

// HandleStartSelect — the soft-reset chord, then (unless a demo is running) Start to pause or unpause
// and Select to toggle the preview piece. Shared with the two-player round.
//
// Returns whether the frame continues. The original reaches its reset with a jump, not a call
// (tetris.asm:4444), so a matched chord abandons the rest of the frame outright — the caller runs
// nothing further. Every other path returns true. See docs/contracts/boot.md §10.
// `now` banks the round's played time when the player pauses and re-stamps it when they resume, so
// paused time - and time on a screen opened from a pause - is not counted as play.
[[nodiscard]] bool handleStartSelect(GameContext& game, const SoftResetHook& softReset = {},
                                     const NowNanos& now = {});

// HandlePausedMultiplayer — run the two-player unpause protocol: the master sends the unpause command,
// the slave unpauses when it reads one. Returns true when the caller must return immediately without
// running its remaining work — the original expresses that by discarding the caller's return address
// (tetris.asm:4529 / :4557), which has no direct equivalent here.
[[nodiscard]] bool handlePausedMultiplayer(GameContext& game);

// ── Shared board helpers ────────────────────────────────────────────────────────────────────────────

// FillPlayingFieldAndWipe — start a field wipe, then fill every visible field cell with `fill`. Both
// halves matter: the init disarms the wipe immediately afterwards, the two game-over callers let it
// run. See the contract.
void fillPlayingFieldAndWipe(GameContext& game, std::uint8_t fill);

// ── Installer ───────────────────────────────────────────────────────────────────────────────────────

// Everything the gameplay handlers need from systems that are not the gameplay session itself. `draw`
// is required — the piece pipeline cannot run without a randomizer; the rest default to inert.
struct GameplayWiring {
    std::function<std::uint8_t()> draw;
    GameplayDemoHooks             demo{};
    InitGarbageHook               initGarbage{};
    SoftResetHook                 softReset{};

    // The clock a round is timed against. Without it every round is recorded at a length of zero;
    // everything else about the round is still counted.
    NowNanos now{};
};

// Install the seven gameplay handlers into their dispatch slots ($0A, $00, $01, $0D, $04, $0B, $0C).
void installGameplayHandlers(GameStateDispatcher& dispatcher, GameplayWiring wiring);

}  // namespace kirpich::systems
