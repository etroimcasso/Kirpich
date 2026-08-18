# Screen loading and the background — behavioral contract

Reverse-derived from the Game Boy Tetris disassembly (upstream `b95c668`). Covers what a screen does
to the background before it does anything else — stamping a stored backdrop, and choosing the tile
art that backdrop is drawn from — and the relation the port's render bridge reads to turn the result
into a picture.

Source anchors: `LoadTilemap` (`tetris.asm:6410-6431`), `LoadFontTiles` (`:6378-6392`),
`LoadGameplayTiles` (`:6368-6376`), `LoadCopyrightAndTitleScreenTiles` (`:6394-6398`), and the call
sites listed in §4.

## 1. The background is the board

The original keeps a 32 × 32 tile grid at `$C800` and mirrors it into the background map at `$9800`
as it draws. `PlayingFieldState` (`docs/contracts/playing-field-state.md`) is the port's model of
that grid, and every writer the port has ported already writes it: the title screen paints walls and
a floor into it, piece locking writes blocks, line clears scan and compact it, the garbage fill
buries it, the game-over and scoreboard printers stamp text into it.

`LoadTilemap` is one more writer. On hardware it writes the background map rather than the board, and
the wipe carries the board forward into that map a row at a time; in the port there is one grid, so a
backdrop load writes it directly. The visible screen is the board's top-left 18 rows × 20 columns.

## 2. Stamping a backdrop — `LoadTilemap.to9800` (`:6410-6431`)

```
.to9800   ld hl, $9800        ; the background map
.toHL     ld b, SCRN_Y_B      ; 18 rows
          ...                 ; 20 cells per row, then + SCRN_VX_B (32) to the next
```

`SCRN_X_B` = 20, `SCRN_Y_B` = 18, `SCRN_VX_B` = 32 (`hardware.inc:896-901`).

Port surface: `loadScreenTilemap(field, tilemap)` (`src/systems/screen.h`).

- Writes `board[0..17][0..19]` from the tilemap, cell for cell.
- Leaves every other board cell alone. Columns 20-31 and rows 18-31 hold the board's own off-screen
  content — the floor row, the wall columns past the screen, the garbage a link-cable round parks
  below the floor — and no backdrop reaches them.
- Overwrites whatever was in the region, the playing field included. The stored screens are authored
  for exactly that: both gameplay backdrops carry the field's walls in their own columns 1 and 12 and
  leave columns 2-11 as `CharTile::SPACE`, so stamping one lays out an empty field.

Nine of the twenty-two stored maps are full 20 × 18 backdrops and load through this routine: the two
gameplay screens, the copyright screen, the title screen, the config screen, the two difficulty
screens, and the two link-cable screens. The other thirteen are banners, field overlays, window
blocks, tower columns, and the congratulations strip, with other shapes and other loaders.

## 3. The tile art — which picture an index names

Both solo loaders begin with `LoadFontTiles` (`:6378-6392`), which expands 39 1bpp font tiles into
the first 39 slots of the tile block. Indices `$00`-`$26` therefore name the same glyph under either
regime. They differ after that:

| Loader | What follows the font |
|---|---|
| `LoadCopyrightAndTitleScreenTiles` (`:6394-6398`) | the copyright-and-title art, from index `$27` |
| `LoadGameplayTiles` (`:6368-6376`) | ten tiles of that same art from `$27`, then the config-and-gameplay art from `$30` |

The gameplay loader's second copy starts one tile inside the first and wins, so **nine** tiles of the
copyright art survive at `$27`-`$2F` under the gameplay regime and the gameplay art runs from `$30`
up. Both loaders copy more than their art holds — 218 tiles against 119, and 208 against 197 — so an
index past the real art names bytes nobody authored.

Two consequences the port depends on:

- **The empty cell is regime-independent.** `CharTile::SPACE` is `$2F`, the last of the nine carried
  tiles. A blank board cell is therefore the same picture on a title screen and on a gameplay screen,
  which is what lets one board serve every screen.
- **An index with no art resolves to the empty cell.** The port does not throw. The original draws
  whatever the tile block happens to hold, and a screen that is not finished should look wrong rather
  than end the run.

Port surface: `TileSheet` (`src/state/display_state.h`) names the regime; `locateTile` /
`resolveTile` (`src/render/tile_atlas.h`) carry an index to its sheet and cell.

The regime is real machine state — it is the contents of video memory — and the original records it
nowhere, because on hardware the contents *are* the record. `DisplayState` carries it as a value. It
is deliberately not a field on `GameFlowState` or `EngineState`: those two are tiled byte for byte by
the layout fixtures, which resolve every byte of their windows to exactly one owner, and a field with
no address would break that guard.

## 4. Where each screen loads what

| State | Art | Backdrop |
|---|---|---|
| `$24` init copyright (`:481-483`) | copyright-and-title | copyright screen |
| `$06` init title (`:537`, `:556-557`) | copyright-and-title | title screen, over the board paint at `:538-555` |
| `$08` config body (`:3123-3125`) | gameplay | config screen |
| `$10` init Type A difficulty (`:3319-3320`) | — | Type A difficulty |
| `$12` init Type B difficulty (`:3410-3411`) | — | Type B difficulty |
| `$0A` round init (`:4141`/`:4148`, loaded `:4154`) | — | Type A or Type B gameplay, by game type |

The three rows with no art load are not omissions: the config screen loads the gameplay art and the
screens entered from it do not reload it.

`StartDemo` (`:617-620`) also loads the gameplay art and the config backdrop, one frame before the
round init replaces the backdrop. That call site belongs to the attract-demo routine, which is not
ported; it lands with that routine.

## 5. What the port does not draw

Three visible differences, none of them approximated:

**The paused screen.** The original keeps a *second* background map at `$9C00` and pauses by
switching the display to it (`set 3, [hl]` on `rLCDC`, `:4461`; cleared at `:4487`). The round init
writes the whole gameplay backdrop there as well as to the live map (`:4155-4157`) and stamps the
pause message into it (`:4158-4161`), so the paused screen shows the same panel with no field and a
`PAUSE` label. `PlayingFieldState` models one map, so the port has nowhere to put the second, and
pausing changes the simulation (input stops, the music cue fires) without changing the picture. The
same second map serves the link-cable round init (`:1245-1250`) and the launch scenes
(`:2696-2801`), neither of which is ported.

**The wipe animation.** `wipeCounter` 2-19 walks a row of the board into the background map per
frame, which is what makes a field fill or clear sweep rather than appear. The port carries the
counter and its schedule (`docs/contracts/line-clear.md`) but has one grid, so a fill is complete the
moment it is written and the sweep is not seen.

**Palette effects.** The original writes `BGP` for the line-clear flash, the fades, and the blank at
a screen change. The port renders through a fixed identity ramp: index 0 the darkest shade, the top
index white, matching the inversion the extractor applies (`docs/contracts/tile-graphics.md`).

Sprites are absent for a different reason — the object layer is simply not bridged yet — and are not
a property of this contract.

## 6. What the tests pin

`tests/test_screen.cpp`

1. Every one of the nine full backdrops stamps into `board[0..17][0..19]` cell for cell, with every
   cell outside that window untouched.
2. The empty cell is `$2F` and both gameplay backdrops leave the field's ten columns empty.
3. Each restored call site leaves the regime its paired loader leaves, the three screens with no
   loader call leave the regime alone, and boot is the regime the first screen loads.
4. The game-state aggregate still boots, resets, and compares with the display member.

`tests/test_background_bridge.cpp`

1. `locateTile` over all 256 indices in both regimes, including the fallback and the boundaries.
2. Composition reads the visible window and only that, at 20 × 18, with no flip or rotation.
3. The layer's key, depth, size, scroll, wrap, and that its content borrows the caller's cells.
4. A stamped backdrop and a later board write both reach the picture, and nothing else moves.

The upload itself and the picture on the glass are verified by hand on a development machine: a
continuous-integration runner has neither a display nor the extracted art, which is never placed on
one (`docs/engine/assets.md`).
