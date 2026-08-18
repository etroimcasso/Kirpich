#pragma once

// The display state: which tile art the background is currently drawn from.
//
// Every other state struct in src/state/ mirrors a block of the original's RAM. This one does not.
// It models video RAM - specifically, which of the game's tile sets currently occupies the tile
// block the background reads through. The original answers that question by having run one of two
// load routines; there is no byte anywhere that records the answer, because on hardware the answer
// IS the contents of $8000-$8FFF. A port that draws needs the answer as a value, because a tile
// index alone does not name a picture: index $30 is one glyph under the copyright-and-title art and
// a different one under the gameplay art.
//
// It deliberately does NOT live on GameFlowState or EngineState. Those two are tiled byte for byte
// by the layout fixtures in tests/fixtures/, which resolve every labelled and unlabelled byte of
// their windows to exactly one owner; a synthetic field with no address would break that guard. The
// sheet regime has no address, so it gets its own struct.
//
// The regime is written by the screens that load art (the copyright and title screens load one set;
// the config, difficulty, and gameplay screens load the other) and read by the render bridge when it
// resolves a board cell's tile index to a picture. See docs/contracts/display-state.md.

#include <cstdint>

namespace kirpich {

// Which tile set is loaded in the background tile block.
//
// The original has two solo-flow loaders, and both begin by expanding the 39-tile font into the
// first tile block, so the font's indices mean the same picture under either regime; they differ in
// what follows the font. LoadCopyrightAndTitleScreenTiles (tetris.asm:6394-6398) lays the
// copyright-and-title art down directly after the font; LoadGameplayTiles (:6368-6376) lays down the
// first nine tiles of that same art and then the config-and-gameplay art. The exact index-to-picture
// relation both regimes produce is derived in src/render/tile_atlas.h.
//
// A third set (the multiplayer and Buran art) exists in the ROM and is extracted, but no solo screen
// selects it: the screens that do are the link-cable and launch scenes, which are not ported yet. It
// gets no enumerator until one of them needs it.
enum class TileSheet : std::uint8_t {
    COPYRIGHT_TITLE = 0,  // LoadCopyrightAndTitleScreenTiles - the copyright and title screens
    GAMEPLAY        = 1,  // LoadGameplayTiles - the config, difficulty, and gameplay screens
};

struct DisplayState {
    // The tile set currently loaded. Boots to COPYRIGHT_TITLE because that is what the first screen
    // the game runs loads: GameState_24, the copyright screen (tetris.asm:481).
    TileSheet sheet = TileSheet::COPYRIGHT_TITLE;

    // Return every field to its boot value.
    void reset() { *this = DisplayState{}; }

    friend bool operator==(const DisplayState&, const DisplayState&) = default;
};

}  // namespace kirpich
