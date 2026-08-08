# Sprite scene lists

The sprite objects each scripted scene places on screen — the victory and defeat characters, the
ending dance, the Buran and rocket launches, the menu markers, and the two falling-piece templates.
This page covers the data: what each object holds, where it lives, and how to regenerate it. This
unit is data only; copying objects into the object buffer and drawing them belong to the rendering
code, which is not written yet.

## Where it lives

| File | What it holds | Editing it |
|---|---|---|
| `src/data/scene_sprites.h` | The `SceneSprite` type and one accessor per scene | Hand-written header; the object tables are included from the generated file. |
| `src/data/generated/scene_sprites_data.inc` | The 13 object tables | **Generated — do not hand-edit.** |
| `tests/fixtures/scene_sprites_expected.h` | The raw object bytes, for the test sweep | **Generated — do not hand-edit.** |

Everything is in `namespace kirpich`, included as `"data/scene_sprites.h"` (the `src/` tree is on the
library's include path). The header includes `<kirpich/sprite_id.h>` for the `SpriteId` each object
draws.

## Using it

```cpp
#include "data/scene_sprites.h"

// Most scenes are a list of objects, returned as a span.
for (const kirpich::SceneSprite& s : kirpich::marioVictorySprites()) {
    // s.y, s.x        -> where to draw it
    // s.sprite        -> which composed sprite (see data/sprites.h)
    // s.hidden        -> start invisible until game logic reveals it
    // s.behindBg      -> draw behind the background layer
    // s.xflip         -> mirror horizontally
}

kirpich::dancerSprites().size();            // -> 10
kirpich::rocketLaunchSprites()[0].sprite;   // -> SpriteId::ROCKET_L

// The two falling-piece templates are single objects, returned by reference.
kirpich::activePieceSprite().y;             // -> 0x18
```

A `SceneSprite` is six fields: `hidden`, `y`, `x`, `sprite`, `behindBg`, `xflip`. `y` and `x` are the
OAM base coordinates; `sprite` is a `SpriteId` whose composed layout is in
[`sprites.md`](sprites.md). All the tables are `constexpr`, so they can be read at compile time; there
are no functions with state.

The eleven list accessors return `std::span<const SceneSprite>` — the span length is the object count:

`configScreenSprites`, `typeADifficultySprites`, `typeBDifficultySprites`, `twoPlayerHeightSprites`,
`marioVictorySprites`, `luigiVictorySprites`, `marioDefeatSprites`, `luigiDefeatSprites`,
`dancerSprites`, `buranLaunchSprites`, `rocketLaunchSprites`.

The two template accessors return `const SceneSprite&`: `activePieceSprite`, `previewPieceSprite`.

## Notes

The falling-piece templates carry `SpriteId::L_0` in their `sprite` field as a placeholder; the piece
code sets the real rotation sprite before drawing, so read those two objects for their coordinates and
attributes, not their sprite.

`hidden` marks an object the scene starts invisible and reveals later — the ten dancers and the launch
smoke plumes begin hidden. `behindBg` is set on the victory and defeat characters and both piece
templates. `xflip` is set on the mirrored second object of each victory pair and the second smoke
plume of each launch.

The scene tables carry only these three attribute bits because they are the only OAM attribute bits
that vary across the game's scenes; the rest are fixed and are applied by the renderer.

## Regenerating the tables

The object tables and the test fixture are produced from the disassembly by the parser. Regenerate
both after repinning the upstream source:

```sh
python3 tools/asm_parser/parse_scene_sprites.py --source-root ../tetris --all \
    --inc-out src/data/generated/scene_sprites_data.inc \
    --fixture-out tests/fixtures/scene_sprites_expected.h
```

To change a placement, edit the value in the disassembly and regenerate — the generated files are
overwritten, so never edit them directly. The object byte layout and the per-scene consumer sites are
specified in [`../contracts/sprite-scenes.md`](../contracts/sprite-scenes.md).
