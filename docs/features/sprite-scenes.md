# Sprite scene lists

The sprite objects each scripted scene places on screen: the two-player victory and defeat
characters, the ending dance troupe, the Buran and rocket launch sequences, the menu selection
markers, and the active- and preview-piece templates. This unit ports the object tables of those
scenes — 13 tables, 35 objects. It is data, not drawing: nothing here copies an object into the
object buffer or animates it.

## What it is

| Surface | Where | Shape |
|---|---|---|
| `SceneSprite` | `src/data/scene_sprites.h` | `{ bool hidden; uint8_t y, x; SpriteId sprite; bool behindBg, xflip; }` |
| 11 list accessors (`marioVictorySprites` … `rocketLaunchSprites`) | `src/data/scene_sprites.h` | `std::span<const SceneSprite>` |
| 2 template accessors (`activePieceSprite`, `previewPieceSprite`) | `src/data/scene_sprites.h` | `const SceneSprite&` |

Each object gives a screen position, the `SpriteId` drawn there (its composed layout is the sprites
unit), and three attribute bits. Where each scene's objects are copied and drawn, and how the
falling-piece templates are patched at run time, are pinned with source line anchors in
[`../contracts/sprite-scenes.md`](../contracts/sprite-scenes.md).

## Decisions

**One `SceneSprite` type for both record shapes.** The original stores most scenes as count-prefixed
6-byte objects and the two piece templates as `$FF`-terminated 7-byte objects. The extra template
byte is a fixed `$00` and the terminator is a copy signal, not object data, so both shapes map to the
same six-field type; the templates differ only in being single objects rather than lists.

**Named accessors, no scene enum.** Each scene is selected at one fixed call site in the original, not
by a runtime index into a table of scenes, so there is no symbol space to enumerate. The scenes are
reached by name (`marioVictorySprites()`, `dancerSprites()`, …) rather than through an enum-keyed
lookup. The one symbol space present — the sprite each object draws — is the existing `SpriteId`.

**Attribute bits unpacked to named bools.** Across all 35 objects only three OAM attribute bits vary:
visibility, background priority, and horizontal flip. Those become `hidden`, `behindBg`, and `xflip`;
the bits that never vary are checked when the table is generated rather than stored.

**The composition, not the serialization.** The typed objects drop the parts that are pure
serialization — the `$FF` terminator and the fixed base-flags byte of the piece templates — and store
the placeholder sprite of the templates as-is. The byte fixture keeps the raw object bytes so the two
can be checked against each other.

## Scope

The raw OAM object blocks copied straight to the OAM buffer (the two-player face displays and the
push-start markers) do not use the sprite indirection and are not scene sprites; they belong with the
miscellaneous object data. The cursor and music-type coordinate tables are separate as well.

## Status

Delivered. The tables and the fixture are generated from the disassembly; the full corpus is swept in
[`../../tests/test_scene_sprites.cpp`](../../tests/test_scene_sprites.cpp) against the raw bytes.
Copying objects into the buffer and drawing them arrive with the rendering layer.
