#pragma once

// The game-flow state: the block of high RAM the original keeps at $FF80 and the main loop lives in,
// expressed as one plain C++ struct. Where EngineState (src/state/engine_state.h) holds the $C000
// gameplay globals - the score, the sprite buffer, the piece ring - GameFlowState holds the
// bookkeeping the main loop itself reads each frame: the state-machine index it dispatches on, the
// frame and wipe counters, the drop-timing scalars, the menu selections, and the piece-pipeline
// counters. Every gameplay system dispatches on, times against, or transitions one of these.
//
// The struct is idiomatic, not a byte image of HRAM. It carries only the bytes that are game-flow
// state: the sprite-renderer scratch, the serial/multiplayer bytes, the demo timers, the top-score
// pointer, and the pure engine-mechanism bytes (the VBlank latch, the DMA routine copy, the boot
// clear) live elsewhere or nowhere. `lines` is a decimal integer though the ROM stores it as packed
// BCD; the two menu-selection bytes are typed enums though the ROM leaves them zero until a menu
// writes them. The exact byte-by-byte mapping back to HRAM - including the bytes deliberately left to
// other units, the ones reached only by a raw numeric operand, and the one byte this struct shares
// with the top-score pointer - is written up in docs/contracts/game-state-machine-state.md.
//
// Every member is zero-initialised, so a default-constructed GameFlowState is the boot state (the
// original clears all of HRAM at startup); reset() returns a live instance to it. Filling the state
// for a new game - choosing a level, seeding the drop timing - is the job of the systems that own
// those fields, not of this struct. It is a sibling of EngineState, not a member of it — each state
// block is its own type.

#include <cstdint>

#include <kirpich/game_state.h>
#include <kirpich/game_type.h>
#include <kirpich/music_type.h>
#include <kirpich/piece.h>
#include <kirpich/sprite_id.h>

namespace kirpich {

struct GameFlowState {
    // --- Piece-drop pipeline -------------------------------------------------------------------
    uint8_t pieceLockStage = 0;      // $FF98: lock-delay stage of the piece coming to rest
    uint8_t dropTimer = 0;           // $FF99: frames until the piece steps down one row
    uint8_t framesPerDrop = 0;       // $FF9A: gravity period for the current level (frames per step)
    uint8_t blinkCounter = 0;        // $FF9C: cursor / entry blink phase

    // Lines cleared, as a decimal integer (ROM hLines is a two-byte packed-decimal count: it caps at
    // 9999 in Type A and counts down to zero in Type B; the cap and down-count live with the
    // line-clear code, not here).
    uint16_t lines = 0;              // $FF9E (2 bytes)

    uint8_t completedRowCount = 0;   // $FFA0: rows completed by the current lock (feeds tally, SFX, garbage)

    // --- Frame timers --------------------------------------------------------------------------
    uint8_t timer1 = 0;              // $FFA6: general frame timer, saturating auto-decrement each pass
    uint8_t timer2 = 0;              // $FFA7: second frame timer, decremented alongside timer1
    uint8_t level = 0;              // $FFA9: current level (drives gravity and scoring)
    uint8_t keyRepeatTimer = 0;      // $FFAA: DAS auto-repeat frame counter

    bool paused = false;             // $FFAB: pause flag, toggled by START (domain {0,1})

    Piece nextPreviewPiece{};        // $FFAE: the piece after the current preview (invisible to the player)
    uint8_t numPiecesPlayed = 0;     // $FFB0: pieces played, incremented only when determinism matters

    // --- Menu selections -----------------------------------------------------------------------
    GameType gameType{};             // $FFC0: Type A / Type B (boot value 0 is "unset until the menu writes it")
    MusicType musicType{};           // $FFC1: music choice (boot value 0 is "unset until the menu writes it")
    uint8_t typeALevel = 0;          // $FFC2: chosen Type A starting level
    uint8_t typeBLevel = 0;          // $FFC3: chosen Type B starting level
    uint8_t typeBStartHeight = 0;    // $FFC4: chosen Type B starting garbage height
    uint8_t coarseCountdown = 0;     // $FFC6: counts timer1 expiries (demo launch, victory/defeat blink
                                     // cycles). Shared byte: during top-score entry this is the
                                     // name-entry column, carried as HighScoreState::nameEntryColumn
                                     // (src/state/high_score_state.h); the two uses are disjoint in time.

    // How far the Buran ending's congratulations message has printed: the second-map column the next
    // letter goes in, seeded to 2 when the shuttle clears the screen and terminal at 18
    // (tetris.asm:2851-2854, :2880-2912). Progress through the sixteen letters is recorded nowhere
    // else, so it has to survive the frame.
    //
    // The original stores a full destination address across $FFC9/$FFCA, big-endian. The port carries
    // the low half only, as a column: the high byte is $9C for the whole sequence, and which map is
    // being drawn is already known from DisplayState. Shared byte: $FFCA's other role is the low half
    // of the top-score name-entry cursor, which that screen recomputes each frame rather than storing,
    // and the two screens cannot run at once. See docs/contracts/launch-scenes.md.
    uint8_t congratulationsColumn = 0;  // $FFCA (shared byte; $FFC9 is the constant page byte, not carried)

    // One byte carrying two roles, both of which the readouts depend on ($FFE0).
    //
    // Outside a number print it means "the score has changed": every addition to the score sets it
    // (tetris.asm:187-188), and the score is only drawn when it is set (:6618-6620). Inside a number
    // print it is the printer's own "a nonzero digit has been drawn" flag, cleared on entry and again
    // on exit (:6625-6626, :6657-6658).
    //
    // The second role clears the first, so a print suppresses the next one. Every site that draws the
    // score into both maps sets this back to 1 between its two calls to compensate (:244-245,
    // :5729-5730). It is one field rather than two because separating the roles would remove that
    // interference and the second map would stop updating. See docs/contracts/readouts.md.
    uint8_t scorePrintFlag = 0;      // $FFE0

    // --- Main-loop state machine ---------------------------------------------------------------
    GameState gameState{};           // $FFE1: the state the main loop dispatches on (boot value NORMAL_GAMEPLAY)
    uint8_t frameCounter = 0;        // $FFE2: incremented every VBlank
    uint8_t wipeCounter = 0;         // $FFE3: playing-field wipe animation step
    uint8_t softDropCounter = 0;     // $FFE5: frames the piece has been soft-dropped

    // --- Bonus / rocket scenes -----------------------------------------------------------------
    SpriteId rocketSpriteIndex{};    // $FFF3: score-tier rocket sprite id, copied into a sprite object's id slot
    uint8_t heartMode = 0;           // $FFF4: 0 = normal, non-zero = heart mode (stores the raw joypad byte; see contract)

    // The piece after the preview, staged during the piece pipeline (ROM hTempPreviewPiece). This byte
    // is physically shared with the top-score pointer's low byte (hTopScorePointerLo at $FFFC, owned by
    // the top-score state); the two uses are disjoint in time. See the contract.
    Piece tempPreviewPiece{};        // $FFFC (shared byte)

    // Counts pieces that lock at the spawn position; the game tops out when the count reaches two. The
    // byte is labelled hTopScorePointerHi, whose top-score-pointer use belongs to the top-score state;
    // this counter reaches it by its raw address $FFFB. The two uses are disjoint in time, the same
    // split as tempPreviewPiece. See the contract.
    uint8_t topOutLockCount = 0;     // $FFFB (shared byte)

    // Return every field to its boot (all-zero) value.
    void reset() { *this = GameFlowState{}; }

    friend bool operator==(const GameFlowState&, const GameFlowState&) = default;
};

}  // namespace kirpich
