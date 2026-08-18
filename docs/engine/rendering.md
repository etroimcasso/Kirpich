# Rendering — the background

How Kirpich gets a picture on screen: the two calls a screen makes to set its background up, the
bridge that turns the board into a tile layer, and the host that ties the whole program together.

Backgrounds only. Nothing on the object layer draws yet — the falling piece, the next-piece preview,
the menu cursors, and the ending's dancers are all invisible. See [Status](#status).

## The model in one paragraph

The board is the background. `PlayingFieldState` (see
[playing-field-state.md](playing-field-state.md)) holds a 32 × 32 grid of tile indices, and every
system that changes what is on screen writes it: screens stamp backdrops into it, piece locking
writes blocks, line clears compact it, the printers stamp text. The visible screen is its top-left
18 rows × 20 columns. So drawing is a read: resolve each of those cells' tile indices to a picture
and submit them as one layer. There is no render-side state to keep in step.

## Setting a screen up — `src/systems/screen.h`

Two calls, both writing simulation state. A screen handler makes them before anything else.

```cpp
#include "systems/screen.h"
#include "data/tilemaps.h"

kirpich::systems::loadTileSheet(game.display, kirpich::TileSheet::GAMEPLAY);
kirpich::systems::loadScreenTilemap(game.field, kirpich::kConfigScreenTilemap);
```

### `loadScreenTilemap(PlayingFieldState& field, const ScreenTilemap& tilemap)`

Stamps a full-screen backdrop into `board[0..17][0..19]`. Every other board cell is left alone —
columns 20-31 and rows 18-31 carry the board's off-screen content (the floor row, the wall columns
past the screen, the garbage a link-cable round parks below the floor), and no backdrop reaches
them.

`ScreenTilemap` is `std::array<std::array<std::uint8_t, kTilemapScreenCols>, kTilemapScreenRows>` —
20 × 18, the shape the nine full-screen backdrops share. The other thirteen stored maps
([tilemaps.md](tilemaps.md)) are banners, field overlays, window blocks, tower columns, and the
congratulations strip; they have other shapes and other consumers.

A backdrop overwrites the playing field along with everything else in its region. That is intended:
both gameplay backdrops carry the field's walls in their own columns 1 and 12 and leave columns 2-11
empty, so stamping one lays out an empty field rather than erasing a live one. Anything that must
survive — the Type B starting garbage, for one — is written after.

### `loadTileSheet(DisplayState& display, TileSheet sheet)`

Records which tile art is loaded. `TileSheet` is `COPYRIGHT_TITLE` or `GAMEPLAY`, and it matters
because a tile index names different pictures under the two: index `$30` is one glyph with the
copyright art loaded and another with the gameplay art.

Call it wherever the original calls a tile loader, and with the art that loader loads. Three of the
six screens make no such call — the config screen loads the gameplay art and the screens entered from
it do not reload it — so those three call only `loadScreenTilemap`. The per-screen table is in
[`../contracts/screen.md`](../contracts/screen.md) §4.

`DisplayState` lives in `src/state/display_state.h` and sits on `GameContext` beside the other state
structs. It boots to `COPYRIGHT_TITLE`, the art the game's first screen loads.

## The bridge — `src/render/`

Namespace `kirpich::render`. The only code that knows both Kirpich's types and the engine's.

### Uploading the art

```cpp
#include "render/tile_atlas.h"

const kirpich::render::TileAtlas tiles = kirpich::render::uploadTileAtlas(renderer);
```

Uploads all four extracted sheets and both palettes, once, and returns their handles. Call it after
the assets are present and before the first frame. It throws whatever the engine's loaders throw when
a file is missing or will not decode.

`TileAtlas` holds four `retropp::AtlasId`s and two `retropp::PaletteId`s. Two palettes rather than
one: the extractor writes each decoded sample as its own palette index, and the 1bpp font yields 0-1
where the 2bpp sheets yield 0-3, so each gets the identity ramp for its own depth. Both are the fixed
DMG greys, darkest at index 0.

`multiplayerBuran` is uploaded but unused — no screen that currently draws selects it.

### Resolving a tile index

```cpp
const kirpich::render::ResolvedTile art =
    kirpich::render::resolveTile(index, game.display.sheet, tiles);
// art.atlas, art.cell, art.palette
```

`resolveTile` is `locateTile` carried through to the uploaded handles; `locateTile(index, sheet)` is
the pure part and returns a `TileLocation { TileSource source; std::uint16_t cell; }`. Both are
`noexcept` and neither can fail: an index with no art under the current regime resolves to the empty
cell rather than throwing, because the original draws whatever its tile block happens to hold.

The relation, and the constants that express it:

| Indices | `COPYRIGHT_TITLE` | `GAMEPLAY` |
|---|---|---|
| `$00`-`$26` | font, cell = index | font, cell = index |
| `$27`-`$2F` | copyright art, cell = index − `$27` | copyright art, cell = index − `$27` |
| `$30` and up | copyright art, cell = index − `$27` | gameplay art, cell = index − `$30` |
| past the real art | empty cell | empty cell |

`kContentTileBase` (`$27`), `kGameplayTileBase` (`$30`), `kCarriedCopyrightTiles` (9), and the three
per-sheet tile counts are all in `tile_atlas.h`. The nine-tile overlap is not incidental: the board's
empty cell is `$2F`, the last of the nine, so a blank cell is the same picture under both regimes.
Shortening the overlap would change every blank cell on a gameplay screen.

### Composing a frame

```cpp
#include "render/background.h"

std::vector<retropp::TileCell> cells;   // held across frames

kirpich::render::composeBackground(game.field, game.display.sheet, tiles, cells);

retropp::FrameDrawState frame;
frame.layers.push_back(kirpich::render::backgroundLayer(cells));
renderer.renderFrame(frame);
```

`composeBackground` resizes `cells` to 360 (20 × 18) and fills it row-major from the board's visible
corner. Writing into a caller's vector rather than returning one keeps a per-frame allocation out of
a grid that never changes size.

`backgroundLayer` wraps those cells as the frame's one background layer: key `"background"`, z 0,
sized to the Game Boy viewport, at the origin, `TileWrap::Blank`. **It borrows the cells** — the
engine's tile content holds a span valid only for the `renderFrame` call that consumes it — so the
vector must outlive that call. Do not hand it a temporary.

There is no camera and no scroll: this game does not scroll, so the map and the screen are the same
size.

## The host — `src/main.cpp`

The program: configure the engine, make sure the assets exist, build the platform and renderer,
register the two virtual-machine routines, upload the art, install every state handler, and run.

The frame is two callbacks on the engine's run loop:

- `simTick` advances the machine's divider by one tick's worth of cycles, then runs one game frame
  through the dispatcher.
- `renderLoop` composes the board and submits it.

The engine's run loop owns pacing at the true Game Boy rate; the port sets no rate of its own. See
[dispatcher.md](dispatcher.md) for what one game frame is.

Two things to know before changing it.

**One virtual machine, shared.** The piece randomizer and the garbage fill both read the divider, and
a Type B round init draws pieces and then fills garbage in the same frame, so the draws advance the
divider the fill reads. Registering both routines on separate machines would give each its own
divider and silently change the field a round starts under. Nothing in the types enforces this.

**The boot path is substituted.** The original's startup routine is not ported; the host sets the
first game state directly. Anything that startup would have established is not established.

## Status

Backgrounds draw. Three differences from the original are known and deliberate, each recorded in
[`../contracts/screen.md`](../contracts/screen.md) §5:

- **The paused screen is blank of change.** The original pauses by switching to a second background
  map holding the same backdrop plus a `PAUSE` label. The port models one map, so pausing stops input
  and cues the music but does not change the picture.
- **The field wipe does not sweep.** The wipe's job on hardware is to carry the board into the
  background map a row per frame. With one grid there is nothing left to reveal, so a fill or clear
  completes the moment it is written.
- **No palette effects.** The line-clear flash, the fades, and the blank at a screen change are
  register writes the port does not make; everything renders through the fixed grey ramp.

Sprites are absent for a different reason — the object layer is not bridged yet.

## Where to change things

| To change | Edit |
|---|---|
| which backdrop or art a screen loads | that screen's handler in `src/systems/` |
| what a tile index draws | `locateTile` in `src/render/tile_atlas.cpp` |
| the colours | the four shade constants in `src/render/tile_atlas.h` |
| the visible window, layer key, depth, or wrap | `src/render/background.{h,cpp}` |
| what the program does at startup | `src/main.cpp` |
| the backdrop data itself | regenerate the tilemaps — see [tilemaps.md](tilemaps.md) |
