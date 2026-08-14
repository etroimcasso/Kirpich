# Sprite-renderer state

The live sprite-object array — the sixteen high-level sprite descriptors every menu cursor, gameplay
piece, and scripted scene writes, and the renderer compiles into the OAM staging buffer each frame —
ported as one hand-written C++ struct. Where the global game state holds the score and the OAM buffer,
and the game-flow state holds the main-loop bookkeeping, this holds the sprites the game wants on screen
before the renderer turns them into hardware objects.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `SpriteRendererState` | `src/state/sprite_renderer_state.h` | sixteen `SpriteSlot`s: each a visibility flag, a screen position, a composed-sprite id, three unpacked attribute flags, and an ending-dancer animation counter pair |

The behavioral specification — the byte-by-byte slot map, the status domain, the sprite-id ↔ rotation
link, the attribute unpacking, the HRAM-mechanism adjudication, the entry points, and the slot-role
inventory — is in [`../contracts/sprite-renderer-state.md`](../contracts/sprite-renderer-state.md).

## Decisions

**One header-only struct, a sibling of the other state blocks.** `SpriteRendererState` is a plain struct
wrapping `std::array<SpriteSlot, 16>` with a `reset()` to the all-zero boot state; there is no `.cpp`. It
sits beside `EngineState` and `GameFlowState` rather than inside them — each state block is its own type,
and combining them into the running game is later wiring. Sixteen slots is the assigned window over the
`$10`-byte stride, `($C300 − $C200) / $10`.

**The state stops at the WRAM/HRAM line.** The `$C200` array persists across frames and is state; the
renderer's own working memory — the OAM cursor, the per-tile working copy, the escape walk, the flip
arithmetic, the tile-address lookup — lives in HRAM only for the length of one render call and is
mechanism, not state. It ports as nothing here (the render bridge re-implements it with locals in a later
phase), the same treatment the audio unit gave the sound driver's private RAM. The contract records every
one of those HRAM bytes with its role so the boundary is checkable, but none becomes a field.

**The slot models nine bytes; seven are dropped.** Each `$10`-byte slot touches only offsets
{0,1,2,3,4,5,6,$E,$F}; the seven never-accessed bytes (+7…+$D) are omitted, the same stride-padding
choice the OAM buffer made. If a later phase finds an access to one of them, the omission is revisited.

**The status byte becomes a `bool`.** Byte 0's whole writer set was traced: every writer produces `$80`
(hidden) or `$00` (visible), so `hidden` is a `bool`. The renderer's third branch — skip a slot whose
status is any other value — is unreachable in the game and is noted in the contract, not modelled.

**The sprite id is typed, and for a piece it is the rotation.** Byte +3 is the existing `SpriteId`. The
four orientations of a tetromino are four consecutive ids, so the active piece's rotation is arithmetic
on this byte; the field carries the id, and the rotation logic is gameplay work built on top.

**The three attribute bytes unpack to named flags.** Bytes +4/+5/+6 each carry part of the object's DMG
attribute, which the renderer OR-composes; the port unpacks them into `behindBg`, `yflip`, `xflip`, and
`palette1` rather than leaving raw bits, the same composition-over-serialization choice the OAM entry and
the scene-sprite records made.

**The dancers get a named animation pair.** Bytes +$E/+$F are the Type-B ending dancers' animation
countdown and its reload period; the port names both (the original has no labels here).

**Slots 0 and 1 get constants; the scenes stay indices.** The active piece is always slot 0 and the
preview always slot 1, so those two get named constants. The scene usages (cursors, victory characters,
dancers, rockets) have no fixed identity and stay call-site indices with contract narrative — no scene
enum, following the scene-list unit.

## Keeping it honest

The struct does not mirror the array's byte offsets, so its fidelity is checked against the existing
fixtures — this unit adds no parser work, a first for a state unit. The work-RAM census (from the audio
unit) already records the twelve `$C2xx` addresses the game reaches; the test proves each resolves to a
modelled slot offset, with a guard that fails if a censused byte ever lands on one of the dropped
offsets. The high-RAM layout-and-census fixture (from the game-flow unit) already carries the renderer's
HRAM window; the test pins that those rows are still present, so an upstream repin that moved or removed
them surfaces as a failure. See
[`../engine/sprite-renderer-state.md`](../engine/sprite-renderer-state.md) for how to use it.

## Not here yet

The state block is data; the code that reads and writes it — the piece system that spawns and rotates
slots 0 and 1, the menus that place cursors, the victory / dance / rocket scenes that lay out multiple
slots, and above all the render bridge that walks the slots and rebuilds the OAM buffer against the
composed sprites — is gameplay and rendering work that builds on this struct. The renderer's own
mechanism (the OAM cursor, the escape walk, the flip arithmetic, the tile lookup) is re-implemented in
that bridge with locals, not carried as state. This unit provides the slot struct, its lifecycle, and the
existing fixtures to check it against.
