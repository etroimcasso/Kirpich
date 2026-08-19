# Contract — Display state

Reverse-derived behavioral contract for `DisplayState` (`src/state/display_state.h`): the video-memory
state the port carries as values. Every value here is transcribed from the `kaspermeerts/tetris`
disassembly (upstream `b95c668`); the line anchors are the authority the tests check against.

Every other structure in `src/state/` mirrors a block of the original's RAM, and its contract is the
byte layout it mirrors. This one has no address at all. It models three things about video memory that
the game's own memory never records, because on hardware the answer is the hardware's contents.

---

## 1. The background maps

`map` is the 32 × 32 grid of tile indices at `$9800`, and `secondMap` the one at `$9C00`. The hardware
displays one at a time; `displayed` names which, and `displayedMap()` returns it. The visible screen is
that map's top-left 18 rows × 20 columns (`SCRN_Y_B`, `SCRN_X_B`; the row stride is `SCRN_VX_B` = 32,
`hardware.inc:896-901`).

The second map is the paused screen — §4. Everything below that says "the map" without qualification
means the first.

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
| the paused screen | the display switches to the second map, which has no field on it — see §4 | `:4155-4161`, `:4461` |

**Boot values:** both maps all zero — that is what video memory holds before anything writes it, and
the first screen fills what it needs — and `displayed` is `FIRST`, the map the game shows outside a
pause.

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
exactly one owner. A field with no address would break that guard. None of these members has an
address, so they get their own struct.

## 4. The second map and the paused screen

The round init writes the gameplay backdrop into `secondMap` alongside the first (`:4155-4157`) and
stamps the pause message over it (`:4158-4161`). Pausing sets `displayed` to `SECOND` (`set 3, [hl]`
on `rLCDC`, `:4461`); unpausing clears it (`:4487`). The paused screen is therefore the stats panel
with no playing field and a `PAUSE` label.

The panel readouts write both maps as they go, so the paused screen carries the score, level and
height that were live. The line count is the exception: it reaches the second map only through the
four-cell copy the pause performs (`:4464-4476`), which is why the count shown while paused is the one
current at the moment of pausing rather than at the last clear. See
[`readouts.md`](readouts.md).

The same map serves the link-cable round init (`:1245-1250`) and the launch scenes (`:2696-2801`),
neither of which is ported.

## 5. What this does not model

**Anything about the tile block beyond which set is loaded.** The port uploads every extracted sheet
once and resolves an index against the regime; it never models the block's contents.

## 6. Tested by

`tests/test_readouts.cpp` covers the second map: the round init filling it with the backdrop and the
pause message, the readouts that write it alongside the first, the line-count copy, and the display
switching to it and back.

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
