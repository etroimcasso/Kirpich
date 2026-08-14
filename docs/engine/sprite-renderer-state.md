# Sprite-renderer state

The live sprite-object array — the sixteen high-level sprite descriptors the game keeps on screen — held
in one `SpriteRendererState` struct. Each slot is one sprite the game wants drawn: whether it is visible,
where it is, which composed sprite it shows, how it is flipped and paletted, and (for the ending dancers)
where it is in its two-frame animation. The menus, the piece code, and the victory / dance / rocket
scenes write these slots; the renderer reads them and builds the hardware object buffer from them.

It is an idiomatic C++ surface, not a byte image of the original's `$C200` array. The status byte is a
`bool`, the sprite index is the typed `SpriteId`, and the attribute bytes are unpacked into named flags.
The exact mapping back to the original's addresses — and the fact that the renderer's own working memory
is deliberately *not* part of this struct — is the behavioral spec in
[`../contracts/sprite-renderer-state.md`](../contracts/sprite-renderer-state.md).

Everything is in `namespace kirpich`, header-only, included as `"state/sprite_renderer_state.h"` (the
`src/` tree is on the library's include path). There is no `.cpp`. It is a sibling of `EngineState`
([`engine-state.md`](engine-state.md)) and `GameFlowState`
([`game-state-machine-state.md`](game-state-machine-state.md)), not a member of either.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/state/sprite_renderer_state.h` | `SpriteSlot`, `SpriteRendererState`, `reset()`, and the two slot-index constants | Hand-written. Edit here to add or reshape a field. |
| `tests/fixtures/wram_expected.h` | The original's work-RAM layout and the raw-operand access census (`{address, refCount}`) | **Generated — do not hand-edit.** Shared with the other state units. |
| `tests/fixtures/hram_expected.h` | The original's high-RAM layout and access census | **Generated — do not hand-edit.** Shared with the other state units. |

This unit adds no generated files of its own — it reuses the two fixtures the earlier state units
produced.

## The types

`SpriteSlot` is one sprite object, reduced to the nine bytes the game touches:

```cpp
struct SpriteSlot {
    bool     hidden = false;    // $80 = hidden, $00 = visible
    uint8_t  y = 0;             // screen Y (OAM convention, +16 offset)
    uint8_t  x = 0;             // screen X (OAM convention, +8 offset)
    SpriteId spriteId{};        // composed-sprite id; for a piece, its rotation
    bool     behindBg = false;  // draw behind background
    bool     yflip = false;     // vertical flip
    bool     xflip = false;     // horizontal flip
    bool     palette1 = false;  // use object palette 1 (OBP1)
    uint8_t  animCounter = 0;   // dancer animation countdown
    uint8_t  animReload = 0;    // dancer animation reload period

    friend constexpr bool operator==(const SpriteSlot&, const SpriteSlot&) = default;
};

struct SpriteRendererState {
    std::array<SpriteSlot, 16> slots{};

    void reset();
    friend bool operator==(const SpriteRendererState&, const SpriteRendererState&) = default;
};
```

Every member is zero-initialised, so a default-constructed instance is the boot state, and both types
have a defaulted `==`. The active piece is always slot 0 and the preview always slot 1:

```cpp
inline constexpr std::size_t kActivePieceSlot = 0;
inline constexpr std::size_t kPreviewPieceSlot = 1;
```

## Using it

```cpp
#include "state/sprite_renderer_state.h"

kirpich::SpriteRendererState sprites;   // all-zero: the boot state

// Place the active piece: visible, at a position, showing a rotation sprite.
auto& piece = sprites.slots[kirpich::kActivePieceSlot];
piece.hidden   = false;
piece.y        = 40;
piece.x        = 80;
piece.spriteId = kirpich::SpriteId::T_0;

// Hide the preview.
sprites.slots[kirpich::kPreviewPieceSlot].hidden = true;

// Return every slot to the boot state (e.g. when starting a new scene).
sprites.reset();
```

`reset()` restores every slot to its default (all-zero) value; it is equivalent to assigning a fresh
`SpriteRendererState{}`.

## Gotchas

- **`hidden` is a `bool`, and `$80` — not `1` — is the hidden value in the original.** The struct hides
  that: set `hidden = true`. The visibility semantics (the renderer forces a hidden sprite's Y off the
  bottom of the screen) are the renderer's job, not the slot's.
- **`spriteId` doubles as the piece rotation.** For slots 0 and 1 the four orientations of a tetromino
  are four consecutive `SpriteId`s, so rotating a piece walks this field. That logic lives in the piece
  system; the slot just carries the id.
- **The three flip/palette/priority flags come from three different original bytes.** `behindBg`,
  `yflip`/`xflip`, and `palette1` are unpacked from separate attribute bytes that the renderer combines.
  Set the flags you mean; the renderer composes them.
- **`animCounter`/`animReload` are only used by the Type-B ending dancers.** Other slots leave them zero.
- **This is the slot data, not the renderer.** Nothing in this struct walks the sprite, computes tile
  positions, applies flips, or writes the hardware object buffer — that is the render bridge's job, and it
  takes a `SpriteRendererState` (and the composed sprites) to do it. The original's renderer working
  memory is intentionally not part of this struct.

## How it is checked

`tests/test_sprite_renderer_state.cpp` proves the slot shape lines up with the original's array. The
work-RAM access census records every byte of the array the game reaches; the test checks each resolves to
one of the modelled slot offsets, and fails if a reached byte lands on one of the seven offsets the slot
deliberately drops. It also pins that the renderer's high-RAM working bytes are still present in the
high-RAM census (so a change upstream is caught), that the slot count matches the array window, and that
`reset()` returns a mutated instance to the boot state. The fixtures are generated by the disassembly
parsers; regenerate them with `parse_wram.py` / `parse_hram.py` (see
[`engine-state.md`](engine-state.md) and [`game-state-machine-state.md`](game-state-machine-state.md))
after repinning the upstream source. This unit adds no parser of its own.

## Changing it

To add or reshape a slot field, edit `src/state/sprite_renderer_state.h` and give the new member a zero
default so `reset()` and the default constructor stay correct. If the change models a byte of the array
the original reaches at a new offset, update the slot map in
[`../contracts/sprite-renderer-state.md`](../contracts/sprite-renderer-state.md) and the offset check in
the test so the census still resolves. If it models one of the renderer's working bytes as state (rather
than leaving it to the render bridge), move its row out of the contract's mechanism table and give it a
field.
