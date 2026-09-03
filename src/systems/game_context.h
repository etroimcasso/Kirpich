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

    JoypadState joypad;                  // this tick's held/pressed snapshot
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

}  // namespace kirpich::systems
