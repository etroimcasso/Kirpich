# New mode

Six extra piece shapes the Game Boy game does not have, and the forks that carry them through a
system built for seven four-cell pieces.

There is no page in [`../contracts/`](../contracts/) for this one. Every other behaviour in Kirpich is
specified against the original game; these pieces are original work, so the code here is the whole
specification and nothing about it is checked against a cartridge.

## What it is

A round is played with one of two piece sets:

| Set | Pool |
|---|---|
| Classic | the seven Game Boy tetrominoes |
| New | all thirteen — those seven plus the six shapes below |

The six are drawn as 5×5 grids, each with its own 8×8 block:

| Kind | Cells | Shape |
|---|---|---|
| `C` | 5 | `##` / `#.` / `##` |
| `COMMA` | 3 | `.#` / `##` |
| `CROSS` | 5 | `.#.` / `###` / `.#.` |
| `Z` | 5 | `..#` / `###` / `#..` |
| `Z_MIRRORED` | 5 | `#..` / `###` / `..#` |
| `W` | 5 | `..#` / `.##` / `##.` |

`COMMA` is three cells, so the four-cells-per-piece rule the Game Boy pieces follow does not hold
across the thirteen. `Z_MIRRORED` is a distinct shape rather than a rotation of `Z` — that pentomino
is chiral, the same reason the cartridge carries both S and Z.

## Where it lives

| File | Holds |
|---|---|
| `src/data/new_pieces.h` | The shapes as 5×5 character grids, their four orientations derived at compile time, and the helpers that read a piece's identity byte: `isNewPiece`, `newPieceIndex`, `newPieceShape`, `newPieceTile`. |
| `src/state/new_mode_state.h` | `PieceType` and `NewModeState` — which set is selected, and which one the round in progress latched. A `GameContext` member. |
| `src/vm/random_new.asm` | The draw core for the thirteen-kind pool: the Classic fold with a wider wrap. |
| `src/vm/piece_random.h` / `.cpp` | `registerNewPieceRandom`, beside `registerPieceRandom`. |
| `src/systems/piece.cpp` | The cell query's fork — where a shape becomes board cells. |
| `src/systems/sprite_renderer.cpp` | The renderer's fork — where a shape becomes object-buffer entries. |
| `src/systems/gameplay.cpp` | The round init's latch, and the `NewModeEnabledHook` seam. |
| `src/render/tile_atlas.h` / `.cpp` | `TileSource::NEW_PIECES` and the tile indices the six blocks occupy. |
| `src/assets/gfx/newPieces.png` | The block art. `newPieces-indexed.png` beside it is the file the game loads. |
| `src/assets/gfx/shapes/` | The six shape drawings, for reading against the grids in `new_pieces.h`. Never loaded. |
| `tests/test_new_pieces.cpp` | The behavioral tests. |

## Identity

A piece is one packed byte — kind × 4 + rotation (`include/kirpich/piece.h`). The seven Game Boy
kinds are 0..6, so the New kinds are 7..12 and their bytes run 28..51:

```cpp
inline constexpr std::uint8_t kNewPieceRawBase = 28;   // 7 kinds x 4 orientations
inline constexpr std::uint8_t kNewModeRawEnd   = 52;   // 13 kinds x 4 orientations

bool          isNewPiece(std::uint8_t raw);            // raw >= 28
std::uint8_t  newPieceIndex(std::uint8_t raw);         // 0..5
const NewPieceShape& newPieceShape(std::uint8_t raw);  // cells, in this orientation
std::uint8_t  newPieceTile(std::uint8_t raw);          // the shape's own block tile
```

Every fork in the piece system asks `isNewPiece` about the byte rather than asking a mode flag what
the round is. The Classic draw folds at 28 and cannot produce a byte above it, so a Classic round
takes the Game Boy path without any check that a caller could forget to write — and a Game Boy piece
drawn during a New round takes it too, which is correct, because it is one.

`newPieceShape` and `newPieceTile` assert on a byte outside 28..51; ask `isNewPiece` first.

## Shapes and orientations

A shape is authored once, as the grid it is drawn as:

```cpp
NewPieceGrid{".....",
             ".##..",
             ".#...",
             ".##..",
             "....."},
```

The bounding box sits centred in the 5×5, ties toward the top-left. The other three orientations are
derived at compile time by turning the grid a quarter turn about its centre, so there are no rotation
tables to fall out of step with each other. `NewPieceShape` carries the resulting cells as signed
offsets from that centre, in the base grid's reading order:

```cpp
struct NewPieceOffset { std::int8_t dy, dx; };
struct NewPieceShape {
    std::array<NewPieceOffset, kMaxNewPieceCells> cells;  // kMaxNewPieceCells == 5
    std::uint8_t                                  count;
};

// every shape, every orientation
inline constexpr std::array<std::array<NewPieceShape, 4>, kNewPieceCount> kNewPieceShapes;
```

**Orientations count counter-clockwise.** That is the direction the Game Boy sprites are laid out in
(`L_1` is `L_0` turned counter-clockwise) and the direction the rotate handler steps the identity byte
for the counter-clockwise button, so both rotate buttons mean the same thing for all thirteen kinds.
A test pins the derivation against `L_0` and `L_1` directly.

A shape drawn with more than five cells fails to compile rather than losing one.

## How a shape reaches the board

`activePieceCells` (`src/systems/piece.h`) is the single query collision, locking and the ghost all
read. It returns up to five cells now rather than exactly four:

```cpp
BoundedVec<PieceCell, kMaxNewPieceCells> activePieceCells(const GameContext& game);
```

A Game Boy piece resolves through its composed sprite and comes to four cells. A New shape has no
composed sprite — its cells are its definition — so the query places each offset a whole tile from the
descriptor's position and converts to a board cell by the same arithmetic:

```
y = slot.y + dy * 8      row = ((y - 0x10) & 0xFF) >> 3
x = slot.x + dx * 8      col = ((x - 0x08) & 0xFF) >> 3
```

Eight-bit throughout, so a cell above the playing field comes out as row 29, 30 or 31 rather than as a
negative number. Three quarters of an upright piece is above the field at spawn, so this is the
ordinary case; the ghost's landing walk steps around the same thirty-two rows.

A hidden descriptor takes the renderer's off-screen y substitution, which puts every cell on row 29
and leaves the columns real — so a hidden piece still collides where it is.

## How a shape reaches the screen

`renderActivePieceSprite` and `renderPreviewPieceSprite` compose a New shape's entries from its cell
list instead of walking a sprite. They are the only place in the renderer that knows New mode exists.

Each piece descriptor owns a window of object-buffer entries, and the width depends on the round:

| Round | Active piece | Preview |
|---|---|---|
| Classic | entries 4–7 | entries 8–11 |
| New | entries 4–8 | entries 9–13 |

Entries the piece does not fill are blanked, and blanked means removed from the frame — the entry and
its source record are both cleared, so the bridge leaves it out and the ghost does not walk it.
Without that, a three-cell shape following a five-cell one would leave a block on screen.

The width is a property of the round, not of the piece in the slot: a four-tile Game Boy piece drawn
during a New round still owns five entries and leaves the last blank, because the piece after it may
need all five.

## The block art

The six blocks are original art, so unlike every other picture in the game they are committed, they
ship, and they never go through the ROM extractor.

They occupy tile indices `$F5..$FA`, at the top of the gameplay regime's range — indices no art is
loaded into, so a New piece's block is an ordinary board byte that resolves to a different sheet.
Only the gameplay regime maps them. `locateTile` resolves them to `TileSource::NEW_PIECES`; a test
sweeps all 256 indices in all three regimes.

Two files, and the distinction matters:

- `src/assets/gfx/newPieces.png` — the drawing: 48×8, four grey levels, transparent holes.
- `src/assets/gfx/newPieces-indexed.png` — what the game loads: the same image as 2-bit grayscale.

The tile loader keeps a PNG's sample value *as* the palette index and never scales it, so an 8-bit
image would load as indices 0/85/170/255 and fall outside the four-entry palettes every sheet is drawn
through. `tools/convert_new_piece_tiles.py` writes the second file from the first
(`index = grey / 85`, transparent → 3). **Re-run it after editing the art**; a test decodes both and
fails if they disagree.

The atlas is declared `AssetPolicy::Embed`, so its bytes are baked into the binary — a player has no
cartridge to extract them from. That requires `retropp_autoembed_assets(kirpich-lib)` in
`src/CMakeLists.txt`; without it nothing is baked and the load falls back to a path only a source tree
has.

## Choosing the set

```cpp
enum class PieceType : std::uint8_t { CLASSIC = 0, NEW = 1 };

struct NewModeState {
    PieceType choice;          // what the player selected
    PieceType roundPieceType;  // what the round in progress is being played with
};
```

The round init latches `roundPieceType` and is the only thing that writes it. Four conditions must all
hold, or the round is Classic:

- New mode is switched on — asked through `GameplayWiring::newModeEnabled`, a `std::function<bool()>`.
  An absent hook reads as off.
- `choice` is `NEW`.
- No attract demo is running. A demo replays a recorded piece list that only names the seven.
- This is not a two-player round. Both machines walk a shared list and there is no way to tell the
  other side which pool it came from.

A round keeps the set it started with: switching the setting from a paused round does not change the
pieces already falling. Only the next round's init reads the setting again.

The draw forks on the latch, not on the setting:

```cpp
const auto drawForRound = [&] {
    return game.newMode.roundPieceType == kirpich::PieceType::NEW ? drawNewModePiece() : drawPiece();
};
```

Use **one** such closure everywhere a piece is drawn. The round init draws through the gameplay
wiring, and the spawn that follows a line clear is driven from the frame's last beat — a round whose
clears drew from the other pool would stop being a New round the first time a line came out.

Both folds register on the **same** `retropp::Vm` as the garbage fill. All three read the one
free-running divider, and a round that draws pieces and then fills garbage in the same frame depends
on the earlier reads having advanced it.

## Changing behavior

- **A shape** is its grid literal in `src/data/new_pieces.h`. Edit the grid; the orientations follow.
  Keep it inside the 5×5 and at five cells or fewer. Update the matching drawing in
  `src/assets/gfx/shapes/` so the two still agree.
- **Adding a shape** means raising `kNewPieceCount`, adding a grid, adding a block to the tile strip
  (and re-running the converter), and widening the fold: the `52` in `random_new.asm` is 13 × 4 and is
  pinned in the tests, so change both together. `kNewPieceTileCount` follows `kNewPieceCount`, and the
  indices must stay inside `$F5..$FF`.
- **Which block a shape draws** is `newPieceTile` — the index is `kNewPieceTileBase` plus the kind's
  position in the roster, so the strip's order is the roster's order.
- **The window widths** are `kNewModePieceOamSlots` and `kNewModePreviewPieceOamStart` in
  `src/systems/sprite_renderer.h`. A shape wider than the window is truncated, not wrapped.
- **When a round is New** is the latch in `initGame` (`src/systems/gameplay.cpp`).

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R NewPieces
```

The tests cover the shapes against their drawings, the orientation derivation (against the Game Boy
sprites' own turn direction), the identity split over the whole byte range, the cell query over every
kind and orientation, collision and locking, the window widths and blanking in both kinds of round,
the latch, the ghost over a five-cell shape, the draw fold and its shared divider, and that the loaded
art matches the drawing.

What they cannot cover is what the blocks look like: resolving a sprite's own coverage needs a
renderer, and no test job has a graphics device. That is checked by playing the game.
