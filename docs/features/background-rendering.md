# Background rendering

**Date:** 2026-08-18
**Status:** Partially implemented — backgrounds draw; sprites do not

## Concept

The first thing in this port that opens a window. Kirpich had a complete simulation — a state
dispatcher, every solo screen, a full round from the title through play to the ending, and audible
sound — and no way to look at it. This unit restores the backdrop loads every screen handler had
dropped, bridges the board to the engine's tile path, and turns the entry point into a real host that
ticks the game and submits a frame.

What it draws is the background: the copyright and title screens, the menus, the playing field with
its walls and panel, the blocks as they lock, the game-over text, the Type B scoreboard. What it does
not draw is anything on the object layer — the falling piece, the next-piece preview, every menu
cursor, the ending's dancers. A round runs and is scored; it is not yet playable in the sense of
being able to see the piece you are steering.

## Design decisions

### The background is a function of the board, not a second copy

The original keeps its background in two places: a 32 × 32 grid in work RAM at `$C800`, and the
hardware's background map at `$9800`, which it copies into a row at a time. The port already carries
the first as `PlayingFieldState` — every shipped writer, from piece locking to the game-over printer,
writes it — so the question was whether the render bridge should read the board or model the map.

Reading the board wins, and the deciding evidence is what the dropped `LoadTilemap` calls were doing:
they wrote a stored screen into the map, and the stored screens are authored to be the board. The two
gameplay backdrops carry the field's walls in their own columns and leave the field's ten columns
empty, which is exactly the board the title screen paints by hand. So a backdrop load is a board
write, and once it is one, the picture is a pure function of state that already ships. Nothing on the
render side has to be kept in step with anything.

That reading was later corrected: the two destinations are separate on hardware and the port carries
both. A backdrop reaches the displayed map alone, the board is filled by the screens that own it, and
the wipe carries the board into the map a row at a time — which is what the row-by-row animation is.
`docs/contracts/screen.md` §5 tabulates which writes reach which grid.

### The tile regime is new state, and it does not go on an existing struct

A tile index is not a picture. The original rewrites its tile block when it changes screens, so index
`$30` is one glyph while the copyright art is loaded and a different one while the gameplay art is;
nothing in RAM records which, because on hardware the contents of video memory *are* the record.

Once something draws, that answer has to exist as a value. It is a new one-field struct,
`DisplayState`, on the game-state aggregate. Putting it on `GameFlowState` or `EngineState` was
rejected: both are tiled byte for byte by layout fixtures that resolve every byte of their window to
exactly one owner, and a field with no address would break that guard. A separate struct says
plainly that this models video memory rather than a block of RAM.

### The index-to-picture relation is read out of the loaders, not assumed

The two loaders were traced rather than taken from their names, and the trace found the detail that
makes the whole collapse work. Both start with the font, so the first 39 indices agree. The gameplay
loader then copies **ten** tiles of the copyright art before laying the gameplay art over the last of
them — leaving nine survivors at `$27`-`$2F`. The board's empty cell is `$2F`, the last survivor. A
blank cell is therefore the same picture on every screen, which is what lets one board serve them
all; had the carry-over been one tile shorter, every blank cell on a gameplay screen would draw the
wrong tile. The relation and the nine-tile overlap are pinned across all 256 indices in both regimes.

### An index with no art draws blank rather than throwing

Both loaders copy more than their art holds — the disassembly's own comments call the surplus
garbage — so indices past the real tiles named nothing. Those resolve to the empty cell. The original
draws whatever the tile block happens to contain, and a screen this port has not finished should look
wrong rather than end the run.

### Two palettes, not one

A single four-entry ramp for everything is the obvious choice and is wrong. The extractor writes each
decoded sample as its own palette index, and the two bit depths do not share an index space: a 2bpp
tile yields 0-3 and the 1bpp font yields 0-1. One four-entry ramp would draw every lit font pixel in
the second-darkest shade. Two uploads ship — a two-entry ramp for the font, a four-entry one for the content sheets —
each the identity for its own sheet. Both are the fixed DMG grey ramp; the original's palette
register writes (the fades, the blank at a screen change) are a later unit and are named as a visible
difference. The line-clear flash is not among them — it repaints tiles rather than the palette, and it
is ported.

### One virtual machine, shared, and the host is where that is paid

The piece randomizer and the garbage fill both read the machine's divider, and a Type B round init
draws its pieces and then fills its garbage in the same frame — so the draws advance the divider the
fill goes on to read. Both routines register on one machine here. Nothing in the types enforces it,
so it is stated at the host, at both headers, and pinned by a test on the garbage side.

### The boot path is substituted, and said to be

The original's startup routine is not ported. The host seeds the machine directly to the copyright
screen, which is where the game starts. That is a substitution, not a port, and it is written down as
one in the host's own comment and in the contract.

## Implementation details

| File | What it holds |
|---|---|
| `src/state/display_state.h` | `TileSheet`, `DisplayState` |
| `src/systems/screen.{h,cpp}` | `loadScreenTilemap`, `loadTileSheet` |
| `src/render/tile_atlas.{h,cpp}` | the sheet uploads, the palettes, `locateTile` / `resolveTile` |
| `src/render/background.{h,cpp}` | `composeBackground`, `backgroundLayer` |
| `src/main.cpp` | the boot host |
| `tests/test_screen.cpp`, `tests/test_background_bridge.cpp` | 8 cases |

The restored call sites are in `title_screens.cpp`, `menu_screens.cpp`, and `gameplay.cpp`; each is
line-anchored to the loader call it replaces in `docs/contracts/screen.md` §4.

## Open questions / future work

**The paused screen is not drawn.** The original pauses by switching the display to a *second*
displayed map that it fills with the same backdrop plus a `PAUSE` label. The port models the board and
one displayed map, so pausing changes the simulation without changing the picture. Drawing it needs a
third grid — a decision about state shape, not a render detail. The same second map serves the
link-cable round init and the launch scenes, neither of which is ported, so whichever unit takes it
first should settle the shape for all three.

**The score, level and top-score readouts are not drawn.** The original prints them from the same
frame beat the wipe and the tally run in; those print routines are not ported, and one of them writes
the paused map as well, so it waits on the decision above.

**Palette effects, scaling, and the display filters** are each their own later unit.
