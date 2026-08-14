#pragma once

// The attract-mode demo state: every byte the recorded-input machinery persists across frames while a
// demo plays, expressed as one plain C++ struct. Where EngineState (src/state/engine_state.h) holds the
// $C000 gameplay globals, GameFlowState (src/state/game_flow_state.h) holds the main-loop bookkeeping,
// SpriteRendererState (src/state/sprite_renderer_state.h) holds the sprite descriptors, and
// MultiplayerState (src/state/multiplayer_state.h) holds the link-mode state, DemoState holds the seven
// high-RAM bytes ($FFE4, $FFE9-$FFEE) the demo player reads and writes as it replays a recording: which
// demo is running, the dead recording flag, the run-length countdown, the cursor into the active
// timeline, and the two held-button sets (the demo's own held buttons and the player's real held state
// parked while the demo drives).
//
// The struct is idiomatic, not a byte image of HRAM. hDemoNumber becomes the ActiveDemo enum; the two
// pointer-half bytes collapse into one uint16_t record index (the port's demo stream is the composed
// DemoInputRecord array from src/data/demo.h, not a GB address); the two held-joypad bytes become
// retropp::ActionSet, the same action vocabulary a DemoInputRecord carries, because the port's live input
// surface is action-based. recording stays a raw uint8_t: its enable value is $FF (a magic, not 1) and
// consumers split between comparing == $FF and testing any non-zero. The exact byte-by-byte mapping back
// to HRAM, the playback/recording narrative, the pointer<->cursor relation, and the upstream quirks this
// surface preserves are written up in docs/contracts/demo-state.md; the record/stream/piece-list data
// semantics are in docs/contracts/demo.md (the demo-data unit).
//
// This carries state only. The playback loop, the pressed-edge derivation ((new ^ old) & new), the RLE
// decode/encode, the demo alternation, the end-of-demo piece-count checks, and the save/substitute of the
// real joypad are the demo state machine's job; they are re-implemented against this struct when the demo
// systems are built, not here.
//
// Every member is zero-initialised, so a default-constructed DemoState is the boot state (the original
// clears all of HRAM at startup); reset() returns a live instance to it. It is a sibling of the other
// state structs, not a member of any; aggregating the state blocks into the running game is later wiring.

#include <cstdint>

#include <retropp/input.h>

namespace kirpich {

// Which attract-mode demo is running, held in $FFE4 (hDemoNumber). A closed three-value identity domain
// with no upstream constant. The enumerator values carry the game type because that is the identity role;
// the demo-*order* is a separate quirk: the game plays demo 2 (Type A) first, then demo 1 (Type B), so
// the numbers run 0 -> 2 -> 1 -> 2 -> 1 ... (StartDemo, tetris.asm:596-614). NONE (0) is "no demo
// running", set by the startup HRAM clear and by the mode-select path when a real game starts. See the
// contract for the numbering-inversion adjudication.
enum class ActiveDemo : uint8_t {
    NONE   = 0,  // no demo running
    TYPE_B = 1,  // demo 1 — the Type B recording (plays second)
    TYPE_A = 2,  // demo 2 — the Type A recording (plays first)
};

struct DemoState {
    // $FFE4 hDemoNumber: which demo is running (and the deterministic-play gate for garbage init,
    // start/select suppression, and piece choice while non-zero).
    ActiveDemo activeDemo = ActiveDemo::NONE;

    // $FFE9 hDemoRecording: domain {0, $FF} where $FF == kDemoRecordingEnabledMagic (from the misc data)
    // enables the dead recording path. Not a bool: the enable value is $FF, not 1, and consumers split
    // between comparing == $FF (DemoSimulateJoypad, RecordDemo) and testing any non-zero
    // (RestoreDemoSavedJoypad). Both gate styles are contract-recorded.
    uint8_t recording = 0;

    // $FFEA hDemoJoypadTimer: frames until the next RLE record loads. Playback decrements it; the dead
    // recording path increments it as the run-length being written. Matches DemoInputRecord::frames width.
    uint8_t framesRemaining = 0;

    // $FFEB/$FFEC hDemoJoypadDataHi/Lo: the two GB pointer halves collapse into one index of the next
    // DemoInputRecord to load in the active timeline. The GB address relation is pointer = blobBase +
    // 2 * nextRecord (blobBase = TypeADemoData / TypeBDemoData per activeDemo); StartDemo's pointer init
    // is nextRecord = 0. uint16_t holds both timelines' counts (128 / 80). See the contract for the
    // pointer-to-cursor collapse.
    uint16_t nextRecord = 0;

    // $FFED hDemoJoypadHeld: the demo's currently-held buttons, as the game action vocabulary a
    // DemoInputRecord carries (retropp::ActionSet over kirpich::Action). The pressed edge is derived at
    // replay, not stored.
    retropp::ActionSet demoHeld;

    // $FFEE hSavedJoyHeld: the player's real held state, saved before the demo substitutes its own input
    // and restored by RestoreDemoSavedJoypad. Same action-based surface as demoHeld.
    retropp::ActionSet savedHeld;

    // Return every field to its boot (all-zero) value.
    void reset() { *this = DemoState{}; }

    friend bool operator==(const DemoState&, const DemoState&) = default;
};

}  // namespace kirpich
