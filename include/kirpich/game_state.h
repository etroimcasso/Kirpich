#pragma once

// The main loop's state ID. Every frame, MainLoop reads this byte and jumps through a 55-entry
// pointer table to the handler for the current state; the handler runs one frame's worth of that
// state and may write a new value here to transition. The first 54 values below are the labelled
// handlers (upstream GameState_00..GameState_35, contiguous $00..$35); the seven after them are the
// port's own screens, which the cartridge has no counterpart for.
//
// The values are transcribed from the dispatch table; the names are port-authored from the
// upstream jump-table comments, because the disassembly labels carry no name beyond their hex
// index. A handful of states the disassembler could not explain keep an index-based name
// (STATE_09_UNUSED, STATE_0C_UNKNOWN). tests/test_core_enums.cpp drift-checks these values against
// the parser-scanned fixture (tests/fixtures/core_enums_expected.h).
//
// The pointer table has a 55th slot at index $36 that holds a raw address ($27EA), not a handler.
// It is a dispatch over-read, not a state, and has no enumerator here.

#include <cstdint>

namespace kirpich {

enum class GameState : uint8_t {
    NORMAL_GAMEPLAY          = 0x00,  // Normal gameplay
    INIT_GAME_OVER           = 0x01,  // Init game over
    BURAN_LIFTOFF            = 0x02,  // Buran liftoff
    BURAN_RISING             = 0x03,  // Buran rising
    GAME_OVER_SCREEN         = 0x04,  // Game over screen
    TYPE_B_VICTORY_JINGLE    = 0x05,  // Type B victory jingle
    INIT_TITLE_SCREEN        = 0x06,  // Init title screen
    TITLE_SCREEN             = 0x07,  // Title screen
    INIT_TYPE_SELECTION      = 0x08,  // Init Game Type / Music Type selection screen
    STATE_09_UNUSED          = 0x09,  // upstream: "just points to a random RET... what?"
    INIT_GAME                = 0x0A,  // upstream: "Init game?"
    INIT_TYPE_B_SCOREBOARD   = 0x0B,  // Init Type B scoreboard
    STATE_0C_UNKNOWN         = 0x0C,  // upstream: "?"
    GAME_OVER_CURTAIN        = 0x0D,  // Game over curtain
    SELECT_GAME_TYPE         = 0x0E,  // Select Game Type
    SELECT_MUSIC_TYPE        = 0x0F,  // Select Music Type
    INIT_TYPE_A_DIFFICULTY   = 0x10,  // Init Type A difficulty selection
    TYPE_A_LEVEL_SELECTION   = 0x11,  // Type A level selection
    INIT_TYPE_B_DIFFICULTY   = 0x12,  // Init Type B difficulty selection
    TYPE_B_LEVEL_SELECTION   = 0x13,  // Type B level selection
    TYPE_B_HEIGHT_SELECTION  = 0x14,  // Type B start height selection
    ENTER_TOP_SCORE          = 0x15,  // Entering topscore for either game type
    INIT_2P_DIFFICULTY       = 0x16,  // Init 2P game difficulty selection
    SELECT_2P_HEIGHT         = 0x17,  // Select 2P game start height
    INIT_2P_GAME             = 0x18,  // Init 2P game
    INIT_2P_GAME_2           = 0x19,  // Init 2P game (2x)
    TWO_PLAYER_GAME          = 0x1A,  // 2P game
    TWO_PLAYER_END_JINGLE    = 0x1B,  // 2P end of game jingle?
    PREPARE_GARBAGE          = 0x1C,  // Prepare garbage?
    INIT_2P_VICTORY          = 0x1D,  // Init 2P victory screen?
    INIT_2P_DEFEAT           = 0x1E,  // Init 2P defeat screen?
    INIT_2P_GAME_3           = 0x1F,  // Init 2P game (3x)
    TWO_PLAYER_VICTORY       = 0x20,  // 2P victory screen
    TWO_PLAYER_DEFEAT        = 0x21,  // 2P defeat screen
    INIT_TYPE_B_BONUS        = 0x22,  // Init Type B bonus ending
    DANCERS                  = 0x23,  // Dancers
    INIT_COPYRIGHT           = 0x24,  // Init copyright screen
    COPYRIGHT_SCREEN         = 0x25,  // Copyright screen
    INIT_BURAN               = 0x26,  // Init Buran
    PREPARE_BURAN_LAUNCH     = 0x27,  // Prepare Buran launch
    BURAN_IGNITION           = 0x28,  // Buran ignition
    BURAN_IGNITION_2         = 0x29,  // Buran ignition "for real this time"
    INIT_2P_MUSIC_SELECTION  = 0x2A,  // Init 2P music selection?
    SELECT_2P_MUSIC          = 0x2B,  // 2P select music
    PRINT_CONGRATULATIONS    = 0x2C,  // Print congratulations
    CONGRATULATIONS          = 0x2D,  // Congratulations
    INIT_ROCKET_LAUNCH       = 0x2E,  // Init rocket launch
    ROCKET                   = 0x2F,  // Rocket
    ROCKET_IGNITION          = 0x30,  // Rocket ignition
    ROCKET_LIFTOFF           = 0x31,  // Rocket liftoff
    ROCKET_MAIN_ENGINE_FIRE  = 0x32,  // Rocket main engine fire
    END_OF_BONUS_SCENE       = 0x33,  // End of bonus scene
    GAME_OVER_TO_BONUS       = 0x34,  // Game over screen leading to bonus ending
    COPYRIGHT_SCREEN_SKIPPABLE = 0x35,  // Copyright screen, but skippable

    // ── The port's own states ──────────────────────────────────────────────────────────────────
    // Screens the cartridge never had. The byte holds 0..255 and the cartridge names 54 of them, so
    // the rest are free; these start at $40 rather than at the first unused value so that "from $40
    // up" is all anyone has to remember about where the port's own range begins.
    INIT_SETTINGS         = 0x40,  // Lay out the settings screen
    SETTINGS              = 0x41,  // Settings screen
    INIT_RESET_CONFIRM    = 0x42,  // Lay out the erase-scores confirm
    RESET_CONFIRM         = 0x43,  // Erase-scores confirm
    INIT_MODE_SCREEN      = 0x44,  // Lay out a mode's own settings screen
    MODE_SCREEN           = 0x45,  // A mode's own settings screen
    SELECT_MODE_OPTION    = 0x46,  // The config screen's third section
    INIT_TYPE_C_DIFFICULTY = 0x47,  // Lay out the Type C level selection
    TYPE_C_LEVEL_SELECTION = 0x48,  // Type C level selection
};

}  // namespace kirpich
