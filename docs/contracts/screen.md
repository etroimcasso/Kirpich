# Screen loading and the background — behavioral contract

Reverse-derived from the Game Boy Tetris disassembly (upstream `b95c668`). Covers what a screen does
to the background before it does anything else — stamping a stored backdrop, and choosing the tile
art that backdrop is drawn from — and the relation the port's render bridge reads to turn the result
into a picture.

Source anchors: `LoadTilemap` (`tetris.asm:6410-6431`), `LoadFontTiles` (`:6378-6392`),
`LoadGameplayTiles` (`:6368-6376`), `LoadCopyrightAndTitleScreenTiles` (`:6394-6398`), and the call
sites listed in §4.

## 1. Two kinds of grid, written separately

The original keeps a 32 × 32 tile grid at `$C800` — the game's own copy of the playing field — and
two background maps the hardware can display, at `$9800` and `$9C00`. `PlayingFieldState`
(`docs/contracts/playing-field-state.md`) models the first; `DisplayState::map` and
`DisplayState::secondMap` (`docs/contracts/display-state.md`) model the other two, with
`DisplayState::displayed` naming the one on screen.

The second map is the paused screen and is covered in §8; everything below that says "the map"
without qualification means the first.

The board is what the game reasons about: the title screen paints walls and a floor into it, piece
locking writes blocks, line clears scan and compact it, the garbage fill buries it. The map is what
reaches the screen. Some writes go to one, some to the other, and some to both — §5 is the full
table, and the difference between them is where the field wipe and the line-clear flash live.

The visible screen is the map's top-left 18 rows × 20 columns.

## 2. Stamping a backdrop — `LoadTilemap.to9800` (`:6410-6431`)

```
.to9800   ld hl, $9800        ; the background map
.toHL     ld b, SCRN_Y_B      ; 18 rows
          ...                 ; 20 cells per row, then + SCRN_VX_B (32) to the next
```

`SCRN_X_B` = 20, `SCRN_Y_B` = 18, `SCRN_VX_B` = 32 (`hardware.inc:896-901`).

Port surface: `loadScreenTilemap(display, tilemap)` (`src/systems/screen.h`). It writes the map;
the board is untouched, so a backdrop can neither lay out a playing field nor erase one.

- Writes `map[0..17][0..19]` from the tilemap, cell for cell.
- Leaves every other map cell alone. Columns 20-31 and rows 18-31 are off-screen and no backdrop
  reaches them.
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

## 5. The two grids

The hardware keeps the game's own copy of the playing field (`PlayingFieldState`, what collision and
locking read) and the background map it displays (`DisplayState::map`), and they are written
separately throughout:

| Write | Destination | Anchor |
|---|---|---|
| a full-screen backdrop | the map alone | `:6410-6431` |
| the title screen's space fill, walls and floor | the board alone | `:538-554` |
| a field-shaped screen | the board, then arms the wipe | `:4621-4623`, `:6434-6457` |
| the field wipe | carries the board into the map, a row per frame | `:5896-5908` |
| piece locking | both | `:6072-6098` |
| the starting garbage | both, one write `$3000` above the other; the second is skipped in a link-cable game | `:4371-4381` |
| the line-clear flash | the map alone, restoring rows from the board | `:5419-5447` |
| the end-of-round tally | the map alone | `:4888`-`:4903`, `:4866`, `:6167` |
| the panel readouts | mostly both maps — see `docs/contracts/readouts.md` §9 | `:242-249`, `:4162-4194` |
| the staged top-score rows | the board, then carried into the map on the redraw flag — the two-cell gap between each name and its score is stepped over, not copied | `:3934-3950`, `:3835-3890`, `:3893-3932` |
| the name-entry cursor glyph | the map alone, leaving the board's staged cell stale | `:3954-3983`, `:4114-4122` |

The board and the map sit `$3000` apart, which is the offset every one of those double writes and the
wipe uses to reach one from the other. The two maps sit `$400` apart, and a cell has the same row and
column in each.

## 6. The palette registers, which are written once and never again

The game sets its three palette registers in the boot routine and nowhere else. `tetris.asm:296-300`
is every write in the ROM:

```
ld a, %11100100   ; 3210 - the identity
ldh [rBGP], a
ldh [rOBP0], a
ld a, %11000100   ; 3010 - the object variant
ldh [rOBP1], a
```

So there is no fade and no palette animation anywhere in this game. The port's fixed ramps are its
palettes exactly: the identity for the background and the plain object ramp, and `%11000100` for the
second object ramp, which is the one the ending's dancers select. Index 0 is the darkest shade and the
top index is white, matching the inversion the extractor applies
(`docs/contracts/tile-graphics.md`).

What the port does not reproduce is the screen blanking while a tilemap loads. That is `rLCDC` —
eighteen writes across the ROM — and it is display control rather than palette, so it is a separate
visible difference and is recorded in §5.

The line-clear flash is neither: it repaints tiles, and it is ported.

## 7. The second map

The hardware can display either background map and selects between them with a bit of its control
register. The game uses the second as its paused screen: the round init writes the gameplay backdrop
into it alongside the first (`:4155-4157`) and stamps the pause message over it (`:4158-4161`),
pausing switches the display to it (`:4461`), and unpausing switches back (`:4487`). The result is the
same stats panel with no playing field and a `PAUSE` label.

The panel readouts keep it current: each writes both maps as it goes, so the paused screen shows the
score, level and height that were live. The line count is the exception — it reaches the second map
only through the copy the pause itself performs (`docs/contracts/readouts.md` §6).

The same map serves the launch scenes, which clear it and build their pad on it, switch to it on the
way in and back on the way out ([`launch-scenes.md`](launch-scenes.md) §2–§3), and the link-cable round
init (`:1245-1250`), which is not ported.

Those scenes also stamp shapes this surface's loader cannot express — a four-row block starting part
way down the map, and seven-cell vertical strips — so they carry their own stamps rather than widening
`loadScreenTilemap`, which is fixed at a full 18×20 screen from the top-left corner.

Port surface: `DisplayState::secondMap`, `DisplayState::displayed`, and
`DisplayState::displayedMap()`, which is what the render bridge composes.

## 8. What the tests pin

`tests/test_screen.cpp`

1. Every one of the nine full backdrops stamps into `map[0..17][0..19]` cell for cell, with every
   cell outside that window untouched and the board not written at all.
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
