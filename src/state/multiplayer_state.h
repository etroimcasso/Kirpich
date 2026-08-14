#pragma once

// The serial-link / two-player state: every byte the original communicates through when two Game Boys
// play head to head over the link cable, expressed as one plain C++ struct. Where EngineState
// (src/state/engine_state.h) holds the $C000 gameplay globals, GameFlowState (src/state/game_flow_state.h)
// holds the main-loop bookkeeping, and SpriteRendererState (src/state/sprite_renderer_state.h) holds the
// sprite descriptors, MultiplayerState holds the link-mode state: the master/slave role and the serial
// protocol bytes, the in-round status the two sides exchange (stack height, garbage attacks, round-end
// codes), the received-garbage pipeline, the match win tally, and the pause save slots. The bytes live
// scattered through the original's high RAM ($FFAC-$FFF2); this struct gathers the ones the link mode
// owns into one type.
//
// The struct is idiomatic, not a byte image of HRAM. Bytes the game reads only as zero / non-zero become
// bool; the two role/protocol bytes become the existing SerialRole / SerialState enums; the round-end
// code becomes the RoundOutcome enum minted here; the wire buffers (tx/rx) stay raw uint8_t because they
// carry protocol codes AND raw board / piece-list payload during the bulk transfer. The exact byte-by-byte
// mapping back to HRAM, the full two-player protocol narrative, the wire-code vocabulary, and the
// upstream quirks this surface preserves are written up in docs/contracts/serial-multiplayer-state.md.
//
// This carries state only. The serial interrupt handler, the handshake that elects master/slave, the
// VBlank send path, the in-round status exchange, the garbage bit-math, the win promotion, and the pause
// sync are transport and game logic that read and write these fields; they are re-implemented against
// this struct when the two-player systems are built, not here.
//
// Every member is zero-initialised, so a default-constructed MultiplayerState is the boot state (the
// original clears all of HRAM at startup); reset() returns a live instance to it. It is a sibling of the
// other state structs, not a member of any; aggregating the state blocks into the running game is later
// wiring.

#include <cstdint>

#include <kirpich/serial_role.h>
#include <kirpich/serial_state.h>

namespace kirpich {

// The round-end code a side crosses to its opponent, held in $FFD1. A closed three-value identity domain
// with no upstream constant. The stored value has inverted wire semantics relative to what is sent:
// a side that finishes by clearing its line goal SENDS $77 ("I won by lines"), which the receiver stores
// as WE_LOST; a side that tops out SENDS $AA ("I topped out"), stored as WE_WON. The round state machine
// then shows victory on WE_WON and defeat otherwise. See the contract for the send/store adjudication.
enum class RoundOutcome : uint8_t {
    NONE    = 0x00,  // no round-end code crossed yet
    WE_LOST = 0x77,  // opponent cleared their line goal first
    WE_WON  = 0xAA,  // opponent topped out
};

struct MultiplayerState {
    // --- Start-height negotiation --------------------------------------------------------------
    uint8_t marioStartHeight = 0;    // $FFAC: master's chosen Type-B start garbage height, 0-5
    uint8_t luigiStartHeight = 0;    // $FFAD: slave's chosen Type-B start garbage height, 0-5

    // --- In-round status wire byte -------------------------------------------------------------
    // The polymorphic status this side sends each in-round exchange: a stack height 0-18, or $80|rows
    // for a garbage attack, or a RoundOutcome end code ($77 / $AA). One byte, decoded by context.
    uint8_t outgoingStatus = 0;      // $FFB1

    // --- Multiplayer mode flag -----------------------------------------------------------------
    bool isMultiplayer = false;      // $FFC5: link mode active (domain {0,1})

    // --- Serial link protocol ------------------------------------------------------------------
    SerialRole role{};               // $FFCB: master / slave; boot 0 is "unset until the handshake elects"
    // The serial interrupt sets this to 1 after every transfer; consumers zero-test and clear it, and the
    // WAIT_FOR_SERIAL_INTERRUPT spin polls it. Two upstream sites write non-standard non-zero values
    // ($1B on multiplayer game-over, $1F on a state exit), so the domain is {0, 1, $1B, $1F}, not a bool.
    uint8_t transferCompleted = 0;   // $FFCC
    SerialState protocolState{};     // $FFCD: serial dispatch phase; boot 0 == SerialState::HANDSHAKE
    // Non-zero requests the master to send tx at the next VBlank (the VBlank path zero-tests, sends, then
    // clears it). The slave sets it too, harmlessly — only the master branch consumes it. The written
    // value is incidental (the request is "non-zero", not a specific code).
    uint8_t sendPending = 0;         // $FFCE
    uint8_t tx = 0;                  // $FFCF: serial transmit buffer (protocol codes and raw payload)
    uint8_t rx = 0;                  // $FFD0: serial receive buffer (protocol codes and raw payload)

    // --- Round outcome + received-garbage pipeline ---------------------------------------------
    RoundOutcome roundOutcome{};     // $FFD1: the round-end code crossed this round
    uint8_t garbageRowsReceived = 0; // $FFD2: rows of attack garbage taken off the wire (masked & $7F), 1-4
    uint8_t garbageRowsPending = 0;  // $FFD3: staged received garbage; bit 7 = apply at the next piece
    bool garbageWipeActive = false;  // $FFD4: a garbage-driven field wipe is running (domain {0,2})
    bool linesGoalReached = false;   // $FFD5: this side cleared its Type-B line goal (domain {0,1})
    bool subsequentRound = false;    // $FFD6: this is not the first round of the match (domain {0,1})

    // --- Match tally + advantage / deuce -------------------------------------------------------
    uint8_t ourWins = 0;             // $FFD7: rounds won by this side (first to 4, displayed as 5 stamps)
    uint8_t theirWins = 0;           // $FFD8: rounds won by the opponent
    // The advantage / deuce trio is a dead display path: read by the victory screen, but every non-zero
    // writer is gated on one of the trio already being non-zero, so no reachable code ever sets any of
    // them. They stay fields for a future victory-screen port; see the contract's dead-path note.
    uint8_t advantageOurs = 0;       // $FFD9 (dead)
    uint8_t advantageTheirs = 0;     // $FFDA (dead)
    uint8_t deuce = 0;               // $FFDB (dead)
    uint8_t garbageRowsToSend = 0;   // $FFDC: rows of garbage to send this clear, by clear kind {0,1,2,4}

    // --- Round flags ---------------------------------------------------------------------------
    bool winDoesNotCount = false;    // $FFEF: both sides ended simultaneously; suppress the tally (domain {0,1})
    bool musicSelectionChanged = false; // $FFF0: slave-side music-select redraw flag (domain {0,1})

    // --- Pause save slots ----------------------------------------------------------------------
    uint8_t savedTx = 0;             // $FFF1: tx saved across a pause
    uint8_t savedRx = 0;             // $FFF2: rx saved across a pause

    // Return every field to its boot (all-zero) value.
    void reset() { *this = MultiplayerState{}; }

    friend bool operator==(const MultiplayerState&, const MultiplayerState&) = default;
};

}  // namespace kirpich
