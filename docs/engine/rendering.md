# Rendering

How Kirpich gets a picture on screen: the two calls a screen makes to set its background up, the
routine that draws the game's objects, the bridge that turns both into layers, and the host that ties
the whole program together.

## The model in one paragraph

Two things are drawn, and both are reads of simulation state. The **board** is the background:
`PlayingFieldState` (see [playing-field-state.md](playing-field-state.md)) holds a 32 × 32 grid of tile
indices that every system writes — screens stamp backdrops, piece locking writes blocks, line clears
compact it, the printers stamp text — and the visible screen is its top-left 18 rows × 20 columns. The
**object buffer** is everything else: `EngineState::oam` holds forty hardware object entries, filled by
the sprite renderer from the game's sixteen sprite descriptors. Drawing is resolving each of those to a
picture and submitting two layers. There is no render-side state to keep in step.

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

## Drawing the objects — `src/systems/sprite_renderer.h`

Namespace `kirpich::systems`. This is a simulation routine, not part of the bridge: it reads the
game's sprite descriptors and writes the game's object buffer, and knows nothing about the engine.

Four entry points, each a fixed choice of which descriptors to draw and where in the buffer to start.
A handler calls them exactly where the original does — see
[`../contracts/sprite-renderer.md`](../contracts/sprite-renderer.md) §3 for the sites and for the paths
that deliberately do not redraw.

```cpp
#include "systems/sprite_renderer.h"

kirpich::systems::renderCursors(game);             // descriptors 0-1, from entry 0
kirpich::systems::renderActivePieceSprite(game);   // descriptor 0, at entry 4
kirpich::systems::renderPreviewPieceSprite(game);  // descriptor 1, at entry 8
kirpich::systems::renderSprites(game, 10);         // descriptors 0-9, from entry 0
```

All four take `GameContext&` and return `void`. `renderSpriteRange(game, firstSlot, count, oamStart)`
is the general form the four are written in terms of.

Each descriptor contributes one entry per part of its composed sprite ([sprites.md](sprites.md)), and
the buffer cursor **runs on across descriptors** — it is not reset per descriptor, so a wider sprite
pushes everything after it along. Entries the walk does not reach keep what they held; a caller that
wants a blank buffer calls `clearOamObjects(game)` first ([menu-screens.md](menu-screens.md)).

A hidden descriptor is drawn, not skipped: every part is written with its real column, tile and
attributes, and only its row is replaced by `kHiddenSpriteY`. The piece system depends on that.

`spritePartPosition(slot, sprite, part)` returns the composed `SpritePosition { y, x }` for one part.
It is the placement arithmetic on its own, and the piece system consumes it to work out which board
cells the active piece covers — the two share one implementation deliberately, since in the original
collision reads back what the renderer drew.

## The bridge — `src/render/`

Namespace `kirpich::render`. The only code that knows both Kirpich's types and the engine's.

### Uploading the art

```cpp
#include "render/tile_atlas.h"

const kirpich::render::TileAtlas tiles = kirpich::render::uploadTileAtlas(renderer);
```

Uploads all four extracted sheets and all five palettes, once, and returns their handles. Call it
after the assets are present and before the first frame. It throws whatever the engine's loaders throw
when a file is missing or will not decode.

`TileAtlas` holds four `retropp::AtlasId`s and five `retropp::PaletteId`s:

| Palette | Entries | Used by |
|---|---|---|
| `fontPalette` | 2 | background cells drawn from the font |
| `contentPalette` | 4 | background cells drawn from either content sheet |
| `fontSpritePalette` | 2 | objects drawn from the font |
| `spritePalette0` | 4 | objects, plain ramp |
| `spritePalette1` | 4 | objects, the variant the ending's dancers select |

Backgrounds and objects cannot share a palette: an object's lowest hardware colour is see-through
rather than a shade. Which entry that is follows from the decode inverting — the see-through colour is
the **last** entry of each object ramp, never the first. The two object ramps differ in one place: the
variant draws the second-darkest colour lightest. The font needs no variant, because its tiles carry
only the darkest colour and the see-through one, on which both object palettes agree.

`multiplayerBuran` is uploaded but unused — no screen that currently draws selects it.

### Resolving a tile index

```cpp
const kirpich::render::ResolvedTile art =
    kirpich::render::resolveTile(index, game.display.sheet, tiles);

const kirpich::render::ResolvedTile obj =
    kirpich::render::resolveSpriteTile(index, game.display.sheet, entry.palette1, tiles);
// .atlas, .cell, .palette
```

Both are `noexcept` and neither can fail: an index with no art under the current regime resolves to
the empty cell rather than throwing, because the original draws whatever its tile block happens to
hold. `resolveSpriteTile` picks the same sheet and cell as `resolveTile` — objects index the same tile
block — and differs only in the palette, which follows `palette1`. `locateTile(index, sheet)` is the
pure part both are built on and returns `TileLocation { TileSource source; std::uint16_t cell; }`.

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
#include "render/sprites.h"

std::vector<retropp::TileCell> cells;     // both held across frames
std::vector<retropp::Sprite>   sprites;

kirpich::render::composeBackground(game.field, game.display.sheet, tiles, cells);
kirpich::render::composeSprites(game.engine, game.oamSources, game.display.sheet, simTicks,
                                tiles, sprites);

retropp::FrameDrawState frame;
frame.layers.push_back(kirpich::render::backgroundLayer(cells));
frame.layers.push_back(kirpich::render::spriteLayer(sprites));
renderer.renderFrame(frame);
```

`composeBackground` resizes `cells` to 360 (20 × 18) and fills it row-major from the board's visible
corner. `composeSprites` clears `sprites` and appends one per buffer entry that puts a pixel on the
screen — entries that draw nothing are left out entirely, so the count varies per frame. Writing into
caller-held vectors rather than returning them keeps per-frame allocation out of the loop.

`backgroundLayer` wraps the cells as key `"background"` at z 0, `TileWrap::Blank`; `spriteLayer` wraps
the sprites as key `"sprites"` at z 1. Both are sized to the Game Boy viewport and parked at the
origin, and **both borrow their vector** — the engine's layer content holds a span valid only for the
`renderFrame` call that consumes it — so the vectors must outlive that call. Do not hand either a
temporary.

`simTicks` must increment once per simulation tick. It goes into every object's name, and a name the
renderer has not just seen is what stops it easing an object from its previous position into its new
one. Every object in this game steps a whole tile at a time, so a move must arrive rather than glide;
passing a constant here makes objects glide between their steps, and passing a value that repeats
within a few ticks makes them do it intermittently. An object's name also carries which descriptor,
sprite and part it is, so that two objects in one frame are never confused for each other — the
renderer records that as it writes.

There is no camera and no scroll: this game does not scroll, so the map and the screen are the same
size.

## The host — `src/main.cpp`

The program: configure the engine, make sure the assets exist, build the platform and renderer,
register the two virtual-machine routines, upload the art, install every state handler, and run.

The frame is two callbacks on the engine's run loop:

- `simTick` advances the machine's divider by one tick's worth of cycles, runs one game frame through
  the dispatcher, then runs the frame's last beat — the line-clear flash and the field wipe, which the
  original runs in its vertical-blank handler. Both gate themselves, so they are called every frame and
  act only while a clear is in progress. Without them a round stops after its first lock.
- `renderLoop` composes both layers and submits them.

The engine's run loop owns pacing at the true Game Boy rate; the port sets no rate of its own. See
[dispatcher.md](dispatcher.md) for what one game frame is.

Two things to know before changing it.

**One virtual machine, shared.** The piece randomizer and the garbage fill both read the divider, and
a Type B round init draws pieces and then fills garbage in the same frame, so the draws advance the
divider the fill reads. Registering both routines on separate machines would give each its own
divider and silently change the field a round starts under. Nothing in the types enforces this.

**The boot path is substituted.** The original's startup routine is not ported; the host seeds what it
would have left behind — the first game state, and the game type and music type, both of which later
screens read as drawing inputs. Anything else that startup would have established is not established.

## Status

Backgrounds and objects both draw. A solo round is playable end to end: the piece you are moving, the
next one waiting, the menu cursors, and the ending's ten performers are all on screen.

Four differences from the original are known and deliberate:

- **The paused screen is blank of change.** The original pauses by switching to a second background
  map holding the same backdrop plus a `PAUSE` label. The port models one map, so pausing stops input
  and cues the music but does not change the picture.
- **The field wipe does not sweep.** The wipe's job on hardware is to carry the board into the
  background map a row per frame. With one grid there is nothing left to reveal, so a fill or clear
  completes the moment it is written — a game-over curtain appears rather than building up.
- **The line-clear flash does not flash.** The original alternates the clearing rows in the background
  map while its own copy of the board keeps the blocks. With one grid there is no copy to alternate
  against.
- **No palette effects.** The fades and the blank at a screen change are register writes the port does
  not make; everything renders through the fixed grey ramp.

The first three are one gap seen from three sides: the hardware keeps the game's copy of the screen
and the displayed copy separate, and each effect lives in the difference. Recorded in
[`../contracts/screen.md`](../contracts/screen.md) §5.

Three object behaviours are also not reproduced — background priority, the per-scanline object limit,
and left-to-right priority — and are recorded in
[`../contracts/sprite-renderer.md`](../contracts/sprite-renderer.md) §7.

## Where to change things

| To change | Edit |
|---|---|
| which backdrop or art a screen loads | that screen's handler in `src/systems/` |
| where an object is drawn from, or when | the call sites in `src/systems/`, per the contract's §3 |
| the placement arithmetic | `spritePartPosition` in `src/systems/sprite_renderer.cpp` |
| what a tile index draws | `locateTile` in `src/render/tile_atlas.cpp` |
| the colours, or an object palette | the shade constants and `uploadTileAtlas` in `src/render/tile_atlas.{h,cpp}` |
| the visible window, layer key, depth, or wrap | `src/render/background.{h,cpp}` |
| how objects are named, placed, or ordered | `src/render/sprites.{h,cpp}` |
| what the program does at startup | `src/main.cpp` |
| the backdrop data itself | regenerate the tilemaps — see [tilemaps.md](tilemaps.md) |
