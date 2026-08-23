#pragma once

// New mode's own state: which piece set the player picked, and which one the round is actually
// being played with.
//
// Like DisplayState and ScreenUiState, and unlike every other struct in src/state/, this mirrors no
// block of the original's RAM — the cartridge has no such setting and no byte to hold it. It
// deliberately does NOT live on GameFlowState or EngineState: those two are tiled byte for byte by
// the layout fixtures, which resolve every labelled and unlabelled byte of their windows to exactly
// one owner, and a synthetic field with no address would break that guard.
//
// The master enable is not here either. That is a saved setting (kirpich::Settings) — it outlives a
// launch, so it belongs to the player's document rather than to the machine's state image — and it
// reaches the handlers through a seam rather than through this struct.
//
// Two fields rather than one, because they answer different questions at different times:
//
//   choice          what the config screen's PIECE TYPE section last selected. Survives a round,
//                   because it is the player's standing preference for the next one.
//   roundPieceType  what the round in progress was started with. Latched once, by the round init,
//                   and then left alone: turning the master off from a paused round must not change
//                   the pieces falling in it, and a demo or a two-player round never takes the fork
//                   however the settings are left.
//
// They come apart in exactly one corner, which is why both exist: a New round paused into settings
// with the master switched off finishes as a New round — its score belongs in the New tables — while
// the difficulty screen behind it is already browsing Classic.

#include <cstdint>

namespace kirpich {

// Which set of pieces a round is played with.
enum class PieceType : std::uint8_t {
    CLASSIC = 0,  // the cartridge's seven, and nothing else
    NEW     = 1,  // all thirteen: the cartridge's seven plus the six New shapes
};

struct NewModeState {
    // The config screen's selection. Boots to CLASSIC, which is what a player who has never turned
    // the mode on sees and what every existing screen and test expects.
    PieceType choice = PieceType::CLASSIC;

    // What the running round latched at its init. Boots to CLASSIC so a build with no New-mode
    // wiring at all plays exactly the game it played before.
    PieceType roundPieceType = PieceType::CLASSIC;

    // Return every field to its boot value.
    void reset() { *this = NewModeState{}; }

    friend bool operator==(const NewModeState&, const NewModeState&) = default;
};

}  // namespace kirpich
