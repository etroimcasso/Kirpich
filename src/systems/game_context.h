#pragma once

// The game's whole in-memory image as one aggregate: the ten mutable state structs the port has
// ported so far, plus this tick's joypad snapshot. Every state handler the main loop dispatches to
// receives a GameContext& and reads or writes through it — it is the single argument that carries
// the game's state into and out of a frame.
//
// The aggregate owns its members by value: default construction is boot (each struct applies its own
// boot law), and value semantics make it trivial to snapshot and compare in tests. It holds only
// state — no engine types beyond the action set JoypadState already carries, no virtual machine, no
// renderer, no audio driver. Systems that need those receive them separately when their units land.
// The one audio member, audioCues, is the game's half of the game-to-driver interface (the cue
// mailbox a handler writes and the audio tick drains); the hosted sound driver's own working RAM has
// no member here by design — it lives on the VM side, not in the game's state image (see
// docs/contracts/audio-state.md).

#include <functional>

#include <kirpich/game_state.h>

#include "state/achievement_notice_state.h"
#include "state/achievement_state.h"
#include "state/achievements_screen_state.h"
#include "state/demo_state.h"
#include "state/display_state.h"
#include "state/engine_state.h"
#include "state/game_flow_state.h"
#include "state/high_score_state.h"
#include "state/multiplayer_state.h"
#include "state/playing_field_state.h"
#include "state/screen_ui_state.h"
#include "state/sprite_renderer_state.h"
#include "state/stats_state.h"
#include "systems/audio_cues.h"
#include "systems/input.h"
#include "systems/oam_source.h"

namespace kirpich::systems {

struct GameContext {
    EngineState         engine;          // $C000 gameplay globals
    GameFlowState       flow;            // HRAM game-flow block (dispatch index, timers, selections)
    PlayingFieldState   field;           // $C800 board + $C400 attack row
    SpriteRendererState spriteRenderer;  // $C200 sprite-object array
    MultiplayerState    multiplayer;     // link-cable HRAM
    DemoState           demo;            // attract-mode demo HRAM
    HighScoreState      highScores;      // top-score tables + entry bytes
    DisplayState        display;         // which tile art the background draws through
    ScreenUiState       screens;         // the port's own screens (no cartridge counterpart)
    StatsState          stats;           // what has been played, per difficulty combination
    AchievementState    achievements;    // what has been earned, and this round's observations

    // The achievements screen's own state - which section page is up, where its cursor is, which
    // badge is open. Its own struct rather than more fields on ScreenUiState: that screen owns its
    // whole state space in one place (see state/achievements_screen_state.h).
    AchievementScreenState achievementScreen;

    // The end-of-round notice's own state - what a finished round just earned and has still to show,
    // and where it was going when the notice took the frame (see state/achievement_notice_state.h).
    AchievementNoticeState achievementNotice;

    JoypadState joypad;                // this tick's held/pressed snapshot
    AudioCues   audioCues;               // the frame's pending audio cues (game -> driver mailbox)

    // Not machine state: a record of what the renderer drew into each object-buffer entry, so the
    // render bridge can tell one frame's objects from the last frame's. See systems/oam_source.h.
    OamSourceTable oamSources;

    // Whole-image reset — every member returns to its own boot state. This is the cold-boot reset,
    // not the soft-reset chord: the original's soft reset preserves the top-score tables (its init
    // path enters below the work-RAM-bank-1 clear), so the boot-path unit composes that reset
    // separately and it does not run this. The port also fills highScores from disk at startup after
    // construction (top scores persist across launches); that load is boot-path wiring, not part of
    // this reset.
    void reset() { *this = GameContext{}; }

    friend bool operator==(const GameContext&, const GameContext&) = default;
};

// Whether heart mode governs the round on screen. Heart mode is a persistent title-screen toggle in
// this port (the cartridge armed it transiently, by a held button at the moment of Start, so its
// attract demo never inherited it). The demo, though, runs the same round pipeline a player does, and
// its recordings assume normal gravity — playing them faster tops the field out, which loses a demo
// that is meant never to lose. So a round belongs to heart mode only when it is a real round, not an
// attract demo. The toggle itself is left set, so it survives a demo and greets the player as they
// left it; only the round's difficulty ignores it while a demo is running.
[[nodiscard]] inline bool heartModeActive(const GameContext& game) noexcept {
    return game.flow.heartMode != 0 && game.demo.activeDemo == ActiveDemo::NONE;
}

// How a finished round leaves. Called at the points a round truly ends - the game-over screen's exit
// and the rocket scene's exit - once the round's numbers are final and any bonus scene has run. It is
// handed the state the caller was about to go to and returns the state to go to instead, so a
// consumer can put a screen between the round and its destination without either handler knowing what
// that screen is. The achievement round-end check and the end-of-round notice hang off this; the host
// supplies the closure, and an unset seam sends the player to the destination unchanged.
using RoundExit = std::function<GameState(GameContext& game, GameState destination)>;

}  // namespace kirpich::systems
