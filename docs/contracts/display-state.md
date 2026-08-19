# Contract — Display state

Reverse-derived behavioral contract for `DisplayState` (`src/state/display_state.h`): the video-memory
state the port carries as values. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors are the authority the tests check against.

Every other structure in `src/state/` mirrors a block of the original's RAM, and its contract is the
byte layout it mirrors. This one has no address at all. It models two things about video memory that
the game's own memory never records, because on hardware the answer is the hardware's contents.

---

## 1. The displayed map

`map` is the 32 × 32 grid of tile indices at `$9800` — the background the hardware displays. The
visible screen is its top-left 18 rows × 20 columns (`SCRN_Y_B`, `SCRN_X_B`; the row stride is
`SCRN_VX_B` = 32, `hardware.inc:896-901`).

It is a different thing from the board (`PlayingFieldState`, `$C800`), which is the game's own copy of
the playing field and the thing collision, locking and the line-clear scan read. The two sit `$3000`
apart, and every routine that reaches one from the other uses exactly that offset.

Which writes reach which grid is tabulated in [`screen.md`](screen.md) §5. The short form: backdrops
and the effects reach the map, the game's own field writes reach the board, and the writes that must
appear at once reach both.

**Three of the game's effects exist only in the difference between them**, which is the reason the
port carries both rather than collapsing them:

| Effect | What the difference is | Anchor |
|---|---|---|
| the field wipe | the board holds the new contents; the map catches up one row per frame | `:5896-5908` |
| the line-clear flash | the map alternates between a solid block and the row's real contents, read back out of the board | `:5419-5447` |
| the paused screen | a *second* displayed map the port does not model — see §4 | `:4155-4161`, `:4461` |

**Boot value:** all zero. That is what video memory holds before anything writes it; the first screen
fills what it needs.

## 2. The tile regime

`sheet` records which of the game's tile sets occupies the tile block the background reads through.

A tile index does not name a picture on its own. The original keeps one tile block and rewrites it
when it changes screens, so index `$30` is one glyph under the copyright-and-title art and a different
one under the gameplay art. There is no byte anywhere recording which is loaded, because on hardware
the answer *is* the contents of `$8000-$8FFF`. A port that draws needs it as a value.

| Enumerator | Loader | Screens |
|---|---|---|
| `COPYRIGHT_TITLE` | `LoadCopyrightAndTitleScreenTiles` (`:6394-6398`) | the copyright and title screens |
| `GAMEPLAY` | `LoadGameplayTiles` (`:6368-6376`) | the config, difficulty and gameplay screens |

Both loaders begin with `LoadFontTiles` (`:6378-6392`), so the font's indices mean the same picture
under either regime; they differ in what follows it. The exact index-to-picture relation each produces
is derived in `src/render/tile_atlas.h`, and the per-screen table of which screen loads what is
[`screen.md`](screen.md) §4.

A third set — the multiplayer and Buran art — exists in the ROM and is extracted, but no screen the
port draws selects it, so it gets no enumerator.

**Boot value:** `COPYRIGHT_TITLE`, the art the game's first screen loads (`GameState_24`, `:481`).

## 3. Why it is its own struct

It does not go on `GameFlowState` or `EngineState`. Those two are tiled byte for byte by the layout
fixtures in `tests/fixtures/`, which resolve every labelled and unlabelled byte of their windows to
exactly one owner. A field with no address would break that guard. Neither of these two has an
address, so they get their own struct.

## 4. What this does not model

**The second displayed map.** The original keeps another 32 × 32 grid at `$9C00` and pauses by
switching the display to it (`set 3, [hl]` on `rLCDC`, `:4461`; cleared at `:4487`). The round init
writes the gameplay backdrop there as well as to the live map (`:4155-4157`) and stamps the pause
message into it (`:4158-4161`). The port carries one displayed map, so pausing changes the simulation
— input stops, the music cue fires — without changing the picture. The same second map serves the
link-cable round init (`:1245-1250`) and the launch scenes (`:2696-2801`), neither of which is ported,
so whichever unit takes it first settles the shape for all three.

**Anything about the tile block beyond which set is loaded.** The port uploads every extracted sheet
once and resolves an index against the regime; it never models the block's contents.

## 5. Tested by

`tests/test_display_map.cpp` covers the grids' separation: the wipe carrying one field row per frame
in the counter's own order and nothing outside the wipe's range, the flash covering and restoring from
the board across all seven passes without ever writing the board, garbage reaching both grids in a
solo round and the board alone in a link-cable one, locking appearing at once, and the results screen
printing to the map alone.

`tests/test_screen.cpp` covers the regime and the backdrop's destination: every full backdrop stamping
into the map's visible corner with the board untouched, each restored call site leaving the regime its
paired loader leaves, and the boot regime.

`tests/test_background_bridge.cpp` covers the read: index resolution over the whole domain in both
regimes, and composition reading the map's visible window and only that.
