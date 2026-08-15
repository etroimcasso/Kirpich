# Playing-field state

The board the game plays on — the 32×32 tile grid the game reads and writes as its field — held in one
`PlayingFieldState` struct, alongside the 10-cell row a multiplayer attack is staged in. The board is the
authoritative copy of the field: collision reads it, piece locking writes it, line clears scan it, and
garbage fills it. The renderer presents it into the engine's background map; the board itself is the
source of truth.

It is an idiomatic C++ surface, not a byte image of the original's `$C800` shadow. Cells are raw tile
indices; the visible field is reached through a coordinate accessor; the video-RAM mirror the original
keeps is not part of this struct. The mapping back to the original's addresses, and the reasons the whole
32×32 grid is carried rather than just the visible field, are the behavioral spec in
[`../contracts/playing-field-state.md`](../contracts/playing-field-state.md).

Everything is in `namespace kirpich`, header-only, included as `"state/playing_field_state.h"` (the
`src/` tree is on the library's include path). There is no `.cpp`. It is a sibling of `EngineState`
([`engine-state.md`](engine-state.md)) and the other state blocks, not a member of any.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/playing_field_state.h` | `PlayingFieldState`, the board/field/attack constants, `fieldCell()`, and `reset()` | Hand-written. Edit here to reshape a field or add a landmark constant. |
| `src/data/playing_field.h` | `kPlayingFieldRows` (18), `kPlayingFieldCols` (10), and the wipe schedule | Field extent, included here for `fieldCell` and `attackRow`. See [`playing-field.md`](playing-field.md). |
| `tests/fixtures/wram_expected.h` | The original's work-RAM layout and the raw-operand access census (`{address, refCount}`) | **Generated — do not hand-edit.** Shared with the other state units. |
| `tests/fixtures/playing_field_expected.h` | The 18 wipe address triples (counter, VRAM destination, WRAM source) | **Generated — do not hand-edit.** Shared with the playing-field data unit. |

This unit adds no generated files of its own — it reuses the fixtures the earlier units produced.

## The types

```cpp
inline constexpr std::size_t  kBoardRows = 32;             // full background-map shadow
inline constexpr std::size_t  kBoardCols = 32;             // $20-byte row stride
inline constexpr std::size_t  kPlayingFieldOriginCol = 2;  // visible field's left edge
inline constexpr std::uint8_t kAttackRowBrickTile = 0x28;  // brick the attack row is built from

struct PlayingFieldState {
    std::array<std::array<std::uint8_t, kBoardCols>, kBoardRows> board{};  // 32 x 32 tile grid
    std::array<std::uint8_t, kPlayingFieldCols>                  attackRow{};  // 10-cell staging row

    std::uint8_t&       fieldCell(std::size_t row, std::size_t col);
    const std::uint8_t& fieldCell(std::size_t row, std::size_t col) const;

    void reset();
    friend bool operator==(const PlayingFieldState&, const PlayingFieldState&) = default;
};
```

`board` is the whole 32×32 grid, row-major: `board[row][col]` is the cell the original keeps at
`$C800 + row·32 + col`. The visible 18×10 field is its top-left interior, its left edge at column
`kPlayingFieldOriginCol`. Every member is zero-initialised, so a default-constructed instance is the boot
state, and the struct has a defaulted `==`.

`fieldCell(row, col)` returns the board cell at a visible-field coordinate — field row from the top, field
column from the field's left edge — equivalent to `board[row][kPlayingFieldOriginCol + col]`. It is a
reference, so it reads and writes the same cell.

## Using it

```cpp
#include "state/playing_field_state.h"

kirpich::PlayingFieldState field;   // all-zero: the boot state

// Read and write the visible field by its own coordinates (row 0 = top, col 0 = left edge).
field.fieldCell(17, 0) = 0x80;                     // drop a block in the bottom-left field cell
std::uint8_t cell = field.fieldCell(0, 4);         // top row, fifth column

// The whole grid is reachable for the landmarks outside the visible field (walls, floor, below-floor
// garbage) via board[row][col].
field.board[18][2] = 0x8E;                          // a floor cell

// Stage a multiplayer attack row of bricks.
field.attackRow.fill(kirpich::kAttackRowBrickTile);
field.attackRow[3] = 0x2F;                          // punch a hole (space)

// Return the board and staging row to the boot state.
field.reset();
```

`reset()` restores every cell to its default (all-zero) value; it is equivalent to assigning a fresh
`PlayingFieldState{}`.

## Gotchas

- **The board is 32×32, not 18×10.** The visible field is only the top-left interior. The walls run the
  full height, the floor is a row below the field, a multiplayer round parks garbage in the rows beneath
  the floor, and startup clears the whole page — so the struct carries every row. Use `fieldCell` for
  field coordinates and `board[row][col]` for the landmarks outside it.
- **A cell is a raw tile index.** Cells are plain `uint8_t`, the same tile space as the tilemaps and the
  garbage table. The empty cell is `kGarbageEmptyTile` (`$2F` = `CharTile::SPACE`), from the garbage unit
  — this struct does not define its own empty value.
- **The board is the only copy.** The original mirrors the field into the background map at a fixed
  offset as it writes; the port does not. Write the board; the render bridge presents it. Nothing in this
  struct touches video memory.
- **This is the board, not the game logic.** Nothing here detects a collision, locks a piece, scans for
  completed rows, wipes the field, or stages an attack — those read and write `PlayingFieldState` from
  the gameplay and multiplayer systems built on top of it.

## How it is checked

`tests/test_playing_field_state.cpp` proves the struct's geometry lines up with the original's board. The
work-RAM access census records every byte of both windows the game reaches; the test checks each resolves
to one owner — the lone `$C400` staging row, or a cell on the 32×32 grid — and that nothing above the
board is censused except the stack marker `$CFFF`. It sweeps the playing-field wipe fixture to confirm
the struct's own row math reproduces every wipe address and that the shadow and video-RAM addresses stay
a fixed offset apart, pins `fieldCell` against `board` across the whole field extent (including the
line-clear scan start and the garbage anchors), and checks the struct shape, the wire constants, and
`reset()`. The fixtures are generated by the disassembly parsers; regenerate them with `parse_wram.py` /
`parse_playing_field.py` (see [`engine-state.md`](engine-state.md) and [`playing-field.md`](playing-field.md))
after repinning the upstream source. This unit adds no parser of its own.

## Changing it

To model a board landmark as a named constant (say the floor row), add it to
`src/state/playing_field_state.h` and pin it in the test against its board address. To reshape the field
extent, change it in `src/data/playing_field.h` (it is shared with the playing-field data unit), not
here. If a later phase needs a cell type richer than a raw tile index, replace the `uint8_t` cell and
update the fixtures' expectations and the census sweep so it still resolves. Keep every new member
zero-defaulted so `reset()` and the default constructor stay the boot state.
