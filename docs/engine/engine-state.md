# Engine state

The game's mutable global state — the score, its line-clear bookkeeping, the sprite staging buffer the
renderer flushes each frame, and the piece ring the randomizer fills — held in one `EngineState` struct.
A single instance is the running game: the systems that make up gameplay take a reference to it, read the
fields they care about, and write the ones they own.

It is an idiomatic C++ surface, not a byte image of the original's RAM. The score is a decimal integer,
the line-clears are field-row indices, and the sprite attribute byte is unpacked into named flags. The
exact mapping back to the original's addresses is the behavioral spec in
[`../contracts/engine-state.md`](../contracts/engine-state.md).

Everything is in `namespace kirpich`, header-only, included as `"state/engine_state.h"` (the `src/` tree
is on the library's include path). There is no `.cpp`.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/engine_state.h` | `EngineState`, `OamEntry`, `LineClearStats`, and `reset()` | Hand-written. Edit here to add or reshape a field. |
| `tests/fixtures/wram_expected.h` | The original's whole RAM-layout map (`{name, address, size}` per label), used by the width tests | **Generated — do not hand-edit.** |

## The types

`EngineState` is the state block:

```cpp
struct EngineState {
    std::array<OamEntry, 40> oam{};        // sprite staging, flushed to the display each frame
    uint32_t score = 0;                    // decimal, 999,999 display ceiling
    BoundedVec<uint8_t, 4> lineClears{};   // rows cleared this line-clear, as field-row indices 0..17
    LineClearStats stats{};                // running singles/doubles/triples/tetrises tallies
    uint16_t softDropPoints = 0;           // points for the current soft drop
    uint8_t scoreboardState = 0;           // scoreboard state-machine index
    bool hidePreviewPiece = false;         // hide the next-piece preview
    uint8_t scoreboardTallyPhase = 0;      // which phase of the score count-up animation is running
    bool blockSoftDropAfterLock = false;   // suppress soft-drop scoring right after a piece locks
    bool scoreRedrawRequested = false;     // the score display needs redrawing
    std::array<Piece, 256> pieceList{};    // the randomizer's piece ring

    void reset();
    friend bool operator==(const EngineState&, const EngineState&) = default;
};
```

`OamEntry` is one sprite in the staging buffer, with the DMG object-attribute byte unpacked into four
flags:

```cpp
struct OamEntry {
    uint8_t y = 0, x = 0, tile = 0;
    bool behindBg = false;   // draw behind background colours 1-3
    bool yflip = false;      // vertical flip
    bool xflip = false;      // horizontal flip
    bool palette1 = false;   // use object palette 1 (OBP1) instead of 0
};
```

`LineClearStats` groups the four tallies:

```cpp
struct LineClearStats { uint8_t singles = 0, doubles = 0, triples = 0, tetrises = 0; };
```

All three have a defaulted `==`. Every `EngineState` member is zero-initialised, so a default-constructed
instance is the boot state.

## Using it

```cpp
#include "state/engine_state.h"

kirpich::EngineState state;   // all-zero: the boot state

// Read and write fields directly.
state.score += 40;
state.stats.singles++;
state.hidePreviewPiece = true;

// Stage a sprite for this frame.
state.oam[0] = kirpich::OamEntry{.y = 64, .x = 80, .tile = 0xAE, .xflip = true};

// Record the rows a line-clear removed (field-row indices, up to four).
state.lineClears = {17, 16};

// Return everything to the boot state (e.g. when starting a new game).
state.reset();
```

`reset()` restores every field to its default (all-zero) value; it is equivalent to assigning a
fresh `EngineState{}`.

## Gotchas

- **`score` is a decimal integer, not packed-decimal.** The original stores the score as three
  packed-decimal bytes; `EngineState` keeps a plain `uint32_t` and converts at print time. There is no
  stored packed-decimal copy — the soft-drop tally's scratch copy in the original is not carried either.
- **`lineClears` holds row indices, not addresses.** The original stores field-row *addresses*
  terminated by a zero word; `EngineState` stores the equivalent indices 0..17 and uses `size()` in place
  of the terminator. The index is `(address − $C802) / $20` in the original's field geometry (see
  [`playing-field.md`](playing-field.md)).
- **`OamEntry` is not `OamObject`.** `OamEntry` (this file) is a live staging entry and carries the full
  four-bit attribute set. `OamObject` (`src/data/misc.h`) is a static data-table row and carries only
  `xflip`. They are different types for different roles; do not swap one for the other.
- **Three fields have no name in the original.** `scoreboardTallyPhase`, `blockSoftDropAfterLock`, and
  `scoreRedrawRequested` are RAM the original reads and writes but never labels; the names here are the
  port's, and the contract anchors each to its use sites.

## Regenerating the layout fixture

`wram_expected.h` is produced from the disassembly's RAM map. Regenerate it after repinning the upstream
source:

```sh
python3 tools/asm_parser/parse_wram.py \
  --source-root ../tetris \
  --all \
  --fixture-out tests/fixtures/wram_expected.h
```

The parser walks the RAM map deriving every label's address and size, and stops with a source citation if
an address-arithmetic gap does not line up with the running address, a section's origin is not reached
exactly, a directive is unrecognised, or a region's size is non-positive or out of range. Python 3
(standard library only); it is a development tool and is never needed to build or test Kirpich.

## Changing it

To add or reshape a state field, edit `src/state/engine_state.h` and give the new member a zero default so
`reset()` and the default constructor stay correct. If the field maps to a labelled address in the
original, add the mapping (and any collapsed/omitted bytes) to
[`../contracts/engine-state.md`](../contracts/engine-state.md); the width tests read the label's size from
the generated fixture, so a field whose width must match the original is pinned there.

## Testing

`tests/test_engine_state.cpp` sweeps the whole RAM-layout fixture — every label and gap, proving each
section tiles with no overlap or hole — and pins the struct widths against it (the 40-entry OAM buffer,
the 256-entry piece ring, the four-entry line-clears cap, the stat counts, and the soft-drop width). It
also checks `reset()` returns a fully mutated instance to the boot state, the `OamEntry` value semantics,
and the line-clears bounded domain. The parser has its own tests
(`tools/asm_parser/test_parse_wram.py`, run with `python3 -m unittest tools.asm_parser.test_parse_wram`).
