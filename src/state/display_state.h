#pragma once

// The display state: what the background actually shows, and which tile art it shows it through.
//
// Every other state struct in src/state/ mirrors a block of the original's RAM. This one does not.
// It models video RAM, and it holds the two things about video RAM the game's own memory does not.
//
// The first is the background maps: 32 x 32 grids of tile indices, of which the hardware displays one
// at a time. They are a separate thing from the board (PlayingFieldState), which is the game's own
// copy of the playing field, and the two kinds are written separately throughout - a backdrop load
// writes the map alone (LoadTilemap, tetris.asm:6410-6431), the title screen's space fill, walls and
// floor write the board alone (:538-554), a field-shaped screen writes the board and arms the wipe
// (:4621-4623), and the wipe then carries the board into the map a row per frame
// (WipePlayingFieldRow, :5896-5908, at the fixed $3000 offset between them).
//
// Three of the game's effects exist only in the gap between the board and the map, which is why the
// port keeps both: the field wipe reveals the board a row at a time, the line-clear flash alternates
// the clearing rows in the map while the board holds the blocks, and pausing displays the second map
// entirely.
//
// There are two maps because the hardware has two ($9800 and $9C00) and the game uses the second as
// its paused screen: the round init writes the gameplay backdrop into it as well and stamps the pause
// message on top (:4155-4161), pausing switches the display to it (:4461) and unpausing switches back
// (:4487). The panel readouts write both maps as they go, which is what leaves the paused screen
// showing the numbers that were current. The same second map serves the link-cable round init
// (:1245-1250) and the launch scenes (:2696-2801).
//
// The second is which of the game's tile sets currently occupies the tile block the background reads
// through. The original answers that question by having run one of two load routines; there is no
// byte anywhere that records the answer, because on hardware the answer IS the contents of
// $8000-$8FFF. A port that draws needs the answer as a value, because a tile index alone does not
// name a picture: index $30 is one glyph under the copyright-and-title art and a different one under
// the gameplay art.
//
// It deliberately does NOT live on GameFlowState or EngineState. Those two are tiled byte for byte
// by the layout fixtures in tests/fixtures/, which resolve every labelled and unlabelled byte of
// their windows to exactly one owner; a synthetic field with no address would break that guard.
// Neither member here has an address, so they get their own struct.
//
// The regime is written by the screens that load art (the copyright and title screens load one set;
// the config, difficulty, and gameplay screens load the other), and both members are read by the
// render bridge when it resolves a map cell's tile index to a picture. See
// docs/contracts/display-state.md.

#include <array>
#include <cstddef>
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
// A third set (the multiplayer and Buran art) is loaded by InitRocketLaunchGraphics (:2729-2733),
// which both bonus-ending launch scenes call to build their pad. The link-cable screens load the same
// set, and will select it through this enumerator when they are ported.
enum class TileSheet : std::uint8_t {
    COPYRIGHT_TITLE   = 0,  // LoadCopyrightAndTitleScreenTiles - the copyright and title screens
    GAMEPLAY          = 1,  // LoadGameplayTiles - the config, difficulty, and gameplay screens
    MULTIPLAYER_BURAN = 2,  // InitRocketLaunchGraphics - the launch scenes and the link-cable screens
};

// A background map is the same shape as the board it is carried from, and the visible screen is its
// top-left corner.
inline constexpr std::size_t kBackgroundMapRows = 32;
inline constexpr std::size_t kBackgroundMapCols = 32;

// One background map: a grid of tile indices, each naming a picture in the loaded tile set.
using BackgroundMap =
    std::array<std::array<std::uint8_t, kBackgroundMapCols>, kBackgroundMapRows>;

// Which of the two maps the display reads. The hardware selects it with a bit of its control
// register (tetris.asm:4461 sets it, :4487 clears it; the launch scenes set it at :2718 and :2951 and
// clear it at :2926 and :3061).
enum class DisplayedMap : std::uint8_t {
    FIRST  = 0,  // $9800 - every screen except the paused one and the launch scenes
    SECOND = 1,  // $9C00 - the paused screen, and both bonus-ending launch scenes
};

struct DisplayState {
    // The first background map, which is what every screen but the paused one shows. Boots to
    // all-zero, which is what the hardware's video memory holds before anything writes it; the first
    // screen fills what it needs.
    BackgroundMap map{};

    // The second background map. The round init fills it with the gameplay backdrop and the pause
    // message (tetris.asm:4155-4161), and the panel readouts write it alongside the first, so it is
    // ready to show whenever the game pauses.
    BackgroundMap secondMap{};

    // Which map the display reads. Boots to FIRST, the map the game shows outside a pause.
    DisplayedMap displayed = DisplayedMap::FIRST;

    // The tile set currently loaded. Boots to COPYRIGHT_TITLE because that is what the first screen
    // the game runs loads: GameState_24, the copyright screen (tetris.asm:481).
    TileSheet sheet = TileSheet::COPYRIGHT_TITLE;

    // The map the display is reading. This is what the render bridge composes; writing to the other
    // one changes nothing on screen until `displayed` selects it.
    const BackgroundMap& displayedMap() const {
        return displayed == DisplayedMap::SECOND ? secondMap : map;
    }

    // Return every field to its boot value.
    void reset() { *this = DisplayState{}; }

    friend bool operator==(const DisplayState&, const DisplayState&) = default;
};

}  // namespace kirpich
