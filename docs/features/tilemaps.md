# Background tilemaps

The static screens the game draws: the copyright, title, config, and difficulty screens, both
gameplay backdrops, the multiplayer screens, the scoreboard and dancers overlays, the pause,
game-over, and try-again messages, the Buran launch backdrop with its towers, and the
congratulations strip. This unit ports the tile-index grids of those screens — 22 of them, ~4.1 KB
of tiles. It is data, not drawing: nothing here places a screen on the display or animates it.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `kTilemapScreenCols` / `kTilemapScreenRows` | `src/data/tilemaps.h` | `uint8_t` = 20 / 18 |
| `kTilemapWindowCols` / `kTowerTilemapRows` | `src/data/tilemaps.h` | `uint8_t` = 8 / 7 |
| 9 full-screen grids (`kTypeAGameplayTilemap` … `kMultiplayerGameplayTilemap`) | `src/data/tilemaps.h` | `array<array<uint8_t, 20>, 18>` |
| 3 banner grids (`kMultiplayerVictoryTopTilemap`, `…BottomTilemap`, `kBuranBackdropTilemap`) | `src/data/tilemaps.h` | `array<array<uint8_t, 20>, N>` |
| 2 field overlays (`kScoreboardTilemap`, `kDancersTilemap`) | `src/data/tilemaps.h` | `array<array<uint8_t, 10>, 18>` |
| 3 window blocks (`kPauseMessageTilemap`, `kGameOverTilemap`, `kTryAgainTilemap`) | `src/data/tilemaps.h` | `array<array<uint8_t, 8>, N>` |
| 4 tower columns (`kLeftTowerLeftSideTilemap` …) | `src/data/tilemaps.h` | `array<uint8_t, 7>` |
| `kCongratulationsTilemap` | `src/data/tilemaps.h` | `array<uint8_t, 16>` |

Every screen is a row-major grid of raw tile indices — `grid[row][col]` is the tile at that cell.
The tower columns and the congratulations strip are one-dimensional. Where each screen is drawn,
which cells the game overwrites afterward, and the pacing of the printed screens are pinned, with
source line anchors, in [`../contracts/tilemaps.md`](../contracts/tilemaps.md).

## Decisions

**Cells are raw `uint8_t`, no enum.** The background tile-index space has no named symbols in the
original and no identity role in the port's logic — a cell is just an index into the loaded tile
sheet. So there is nothing to name; every cell is a plain byte.

**The composition, not the serialization.** The original stores some rows as `db "text"` the
assembler encodes through the character map, some as raw bytes, some as a mix; the field overlays end
with a `$FF` copy-terminator; the tower strips are written down a column. The ported grids are the
resolved rectangles: text is decoded to tiles, the `$FF` terminator is dropped from the grid (it is a
loader signal, not a cell), and the towers are stored top to bottom. The byte fixture keeps the raw
serialization so the two can be checked against each other.

**Text decodes exactly as the assembler did.** Text rows go through the same greedy longest-match the
character map uses, so the one multi-character sequence — the `.”` ligature — is a single tile. The
credits line `"by alexey pazhitnov.”"` is 20 tiles, not 21; a naive per-character decode would be
wrong. The generator reuses the character-map parse and would fail hard on any unmapped character.

**Header-only, no accessor.** There is no runtime lookup — the game references each screen directly —
so there is no keyed function to port and no `.cpp`. The unit is four constants and 22 arrays in a
header. The field overlays reuse the playing-field extent constants rather than restating 10 and 18.

**Destinations are not ported here.** Screen addresses, the pause dual-map trick, the level-digit and
score pokes, the victory-name overwrites, and the tower pokes are consumer behavior. They are
recorded in the contract as context so the data's shape is justified, but they belong to the screen
and presentation code.

**Every byte verbatim.** Upstream oddities — the level-0 scores baked into the scoreboard, the
dancers' irregular rows, the copyright layout — are ported exactly. Nothing that looks off is
"fixed"; the original is the contract.

## Keeping it honest

The four constants, all 22 grids, and the byte fixture are generated from the disassembly by
`tools/asm_parser/parse_tilemaps.py`. It matches every screen by its label shape, not by line number,
and stops with a `file:line` citation rather than emit a wrong file if anything has moved: each screen
must have exactly its class dimensions, the field overlays must end with a lone `$FF`, mixed
string/byte rows must still land on the class width, the character map must resolve every text run,
the loaders must still declare the widths and sentinel the grids are built on, and the per-screen byte
counts must sum to 4110. The fixture holds the raw serialization with no port type in it, so the test
sweep compares the composed grids against source bytes rather than against themselves. See
[`../engine/tilemaps.md`](../engine/tilemaps.md) for how to regenerate.

## Not here yet

Drawing the screens — loading a grid to the display, the pause map flip, printing live scores and
player names over a loaded screen, walking the congratulations cursor — is rendering and gameplay
code and ports with those loops. The contract records all of it so those loops have a specification
to build against; this unit gives them the tile data to draw.
