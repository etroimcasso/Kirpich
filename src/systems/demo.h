#pragma once

// Attract-mode demo playback: the two recorded rounds the game plays to itself when the title screen
// is left alone, and the dead recording path that produced them.
//
// A demo is not a cutscene. It runs the ordinary game — the same round init, the same frame, the same
// piece and line-clear logic — with recorded input substituted for the player's. Three things follow
// from that, and they are the whole design:
//
//   - The recordings are run-length encoded. Each step of a timeline (DemoInputRecord, src/data/demo.h)
//     holds a set of actions and a frame count, so a demo advances its cursor only on the frames the
//     input changes. demoSimulateJoypad walks that timeline and writes the tick's joypad snapshot.
//   - The pieces are not random. While a demo runs, the piece pipeline reads the fixed shared list
//     instead of the randomizer (src/systems/piece.cpp), so the same recording produces the same round
//     every time.
//   - The player still owns the buttons. The substitution lasts one frame beat:
//     restoreDemoSavedJoypad puts the real held set back before the frame ends, which is what lets a
//     player press Start to leave, and lets the soft-reset chord work while a demo plays.
//
// The two demos alternate, and they are different game types: the Type A recording plays first, the
// Type B recording second. startDemo configures whichever is next and enters the round init.
//
// These are free functions on GameContext, the same shape as the gameplay, menu, and title systems.
// They own no state: every byte a demo persists between frames lives on DemoState
// (src/state/demo_state.h). The exact per-routine effects, the gate conditions, the edge derivation,
// and the terminals — with source line anchors — are in docs/contracts/demo-playback.md.
//
// Wiring: demoHooks() supplies the four per-frame seams the gameplay frame calls, and startDemo is
// itself the seam the title screen's attract countdown fires. Both are bound in main().

#include <cstdint>
#include <span>

#include "data/demo.h"  // DemoInputRecord
#include "state/demo_state.h"
#include "systems/game_context.h"
#include "systems/gameplay.h"  // GameplayDemoHooks

namespace kirpich::systems {

// The piece counts each demo stops at. The Type A recording plays pieces 0-15 and ends as the count
// reaches 16; the Type B recording starts its count at 17 and ends at 29.
inline constexpr std::uint8_t kTypeADemoEndPieceCount = 16;
inline constexpr std::uint8_t kTypeBDemoEndPieceCount = 29;

// The starting garbage height and level the Type B demo is configured with, and the level both demos
// run at. Demos play fast.
inline constexpr std::uint8_t kDemoLevel = 9;
inline constexpr std::uint8_t kTypeBDemoStartHeight = 2;

// The piece count the Type B demo starts from, which is where its recording picks the shared list up.
inline constexpr std::uint8_t kTypeBDemoFirstPiece = 17;

// The recorded timeline a demo replays, or an empty span when no demo is running. Which of the two
// recordings is live follows from the demo that is running, so the cursor on DemoState is an index
// into this rather than an address.
[[nodiscard]] std::span<const DemoInputRecord> demoTimeline(ActiveDemo demo);

// ── Launch ──────────────────────────────────────────────────────────────────────────────────────────

// StartDemo — configure and enter the next demo.
//
// The two recordings alternate, and the choice is made from the demo that ran *last*: after the Type A
// recording the Type B one is next, and after anything else (including a cold start) the Type A one is.
// So the sequence from power-on is Type A, Type B, Type A, and so on.
//
// Sets the game type, level and starting height the chosen recording was made at, rewinds the timeline
// cursor, loads the screen the demo starts from, and enters the round init — which then runs exactly as
// it does for a player.
void startDemo(GameContext& game);

// ── The frame ───────────────────────────────────────────────────────────────────────────────────────
//
// These four run inside the gameplay frame, in this order, and only while a demo is running. The order
// is load-bearing: the end check reads the player's real buttons because it runs before the
// substitution, and the restore runs after everything that consumes input.

// CheckForEndOfDemo — end the demo and return to the title screen, either because the player pressed
// Start or because the recording's last piece has been played.
void checkForEndOfDemo(GameContext& game);

// DemoSimulateJoypad — advance the recording and substitute its input for the player's.
//
// While the current step still has frames left, it counts one off and the tick reports no new presses.
// On the frame the count runs out it loads the next step, derives that step's newly-pressed actions,
// and arms the new frame count. Either way it parks the player's real held set and puts the demo's in
// the tick's snapshot.
void demoSimulateJoypad(GameContext& game);

// RecordDemo — the recording path, which nothing enables.
//
// It is here because the original has it and calls it every frame; its gate is shut in the shipped game
// (see startRecordingDemo). Given a live recording flag it extends the current step while the input
// holds steady, and starts a new one when it changes.
void recordDemo(GameContext& game);

// RestoreDemoSavedJoypad — put the player's real held set back after the frame's input consumers have
// run, so a demo cannot swallow the buttons that end it.
void restoreDemoSavedJoypad(GameContext& game);

// StartRecordingDemo — arm the recording path. Nothing calls it: the shipped game has no way to reach
// it, so the recording flag is never set and recordDemo never does anything. It exists because the
// original does.
void startRecordingDemo(GameContext& game);

// ── Wiring ──────────────────────────────────────────────────────────────────────────────────────────

// The four per-frame seams, bound to the functions above, ready to hand to the gameplay installer.
[[nodiscard]] GameplayDemoHooks demoHooks();

}  // namespace kirpich::systems
