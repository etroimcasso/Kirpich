# Palette rotation

**Date:** 2026-10-23
**Status:** In progress — the game rotates; the screen that chooses a rotation is not built yet

## Concept

A player asked for each level to wear a different color palette as a round goes on. With a palette
rotation on, a round is drawn through a palette chosen by the level it shows, so the game changes color
as the player levels up. Three rotations ship — **hotter**, **colder** and **rainbow** — and the player
can make rotations of their own, kept in ninety-nine slots the way custom palettes are. Off by default,
so a player who never turns it on sees the game they always saw.

NES Tetris is the reference for the feel: it carries ten palettes and picks one by the level's last
digit, so the cycle repeats every ten levels. This game does the same.

## What the player gets

Once the rotation screen exists:

- **Up on the Palette screen opens the rotation screen**, the mirror of Down opening the palette
  editor. It belongs with the palette screens; the settings menus do not change.
- The screen's menus sit at the bottom, because it is entered by scrolling up: a selector, then the
  editor item, then above them a 2×5 grid of the ten palettes, marked `0` to `9`, each with its
  preview.
- **The selector** runs `off`, then the presets, then the player's saved custom rotations.
- **Holding Select** shows the palette editor's preview with a `Level` item added; while Select is
  held, Up or Right steps forward through `0`-`9` and Down or Left steps back, the level number
  carrying an arrow each way it can still move.
- How a custom rotation is edited is still to be designed.

## What is built

- **A round can be drawn through its level's palette.** The atlas carries a second live palette set,
  changed in place, which a round's own states draw through while a rotation is on. Every menu and
  settings screen keeps drawing through the palette chosen on the Palette screen.
- **The level that picks is the level shown, modulo ten.** Level 10 wears level 0's palette again. A
  Type B round keeps its starting level, so it wears that level's palette all round. Heart mode shifts
  the gravity and not the palette.
- **The round's own states** are play (paused included), the game-over curtain and screen, the Type B
  scoreboard and dance, and both launch scenes. The top-score entry, the title and every port screen
  are outside them. The attract demo runs these same states, so it rotates too.
- **Three presets ship**, ten palettes each, authored here rather than picked from the eighty ramps.
- **The custom rotations persist**: a working rotation — the one a round wears in custom mode, and the
  one the rotation screen will edit — and ninety-nine slots it can be saved into, in their own document.
- **The settings record which rotation, if any, a round follows**: off, one of the three presets, or
  custom.
- **The Palette screen's walk is shared.** The sequence the Palette screen steps — the eighty ramps,
  then every saved custom palette — lives in a module of its own, so the rotation screen can choose a
  level's palette from the same sequence with the same end stops. The Palette screen behaves exactly
  as before.

A rotation can be tried before the screen exists by setting the ninth byte of the settings document
to 1 (hotter), 2 (colder), 3 (rainbow) or 4 (custom).

## Design decisions

**Ten palettes, picked by the level modulo ten.** The game levels to twenty. Three domains were
weighed: ten palettes repeating from level 10 (the NES rule), ten holding the last palette from level
9 on, and one palette for every level 0-20. Ten, repeating, follows the reference the request named
and keeps a rotation small enough to author and to show on one screen.

**A second live palette set.** The atlas's custom set draws the player's working palette, which the
palette editor edits and the palettes document stores, so it cannot also carry a level's palette
without changing the player's own data mid-round. The rotation gets a set of its own, the same shape,
uploaded once at startup and changed in place by the same writer, so the two can never be arranged
differently. Nothing uploads a palette after startup.

**A rotation stores colors, not references.** A custom rotation holds ten palettes' colors, as a custom
palette slot holds four colors. Storing a ramp number or a slot number per level was rejected: a slot
cleared or rewritten later would silently change the rotation, and a rotation seeded from a preset
holds colors no ramp has. When the rotation screen shows which palette a level uses, it asks which
stop on the walk holds those colors.

**Presets are their own colors.** Hotter warms from a cream to an intense red, colder cools from a
slate to a deep blue, and rainbow steps the hue 36° a level from red. None of them has to coincide with
a built-in ramp, and the rainbow is a hue walk the ramp table does not hold. Each is a table of data:
adding a preset is one table, one mode value and one line.

**Several custom rotations, like the custom palettes.** A working rotation and ninety-nine slots, and
whether the working rotation is saved is a comparison against the slots, never a stored number — so
nothing can drift from the colors beside it. One custom rotation was the first design; several, kept
as the palette slots are, replaced it.

**A per-tick reconcile, not hooks in the round.** The host derives each tick which palette the round
wants and rewrites the rotation set only when that differs from what the set holds. Calling it from
the round init and the level-up instead was rejected: it would make two gameplay handlers know about
palettes, and any third place the level changes would be a palette that never updates. The reconcile
runs after the frame's last beat, so a level-up during the field wipe is drawn on the tick it happens.

**The round's own states, and none of the screens around them.** The rotation reaches play, the
game-over screens, the Type B ending and the launch scenes — the screens that belong to a round — and
not the menus, the settings or the top-score entry, which draw through the palette the player chose.
Rotating every screen was rejected: the menus are where a palette is chosen, and opening the settings
from a paused round should show the palette being chosen, not the level's.

**Off reads as the 1989 game.** A stored mode this build does not name reads as off, and a document
from an earlier release migrates forward with the rotation off.

## The presets as authored

Each entry runs dark to light by Rec.601 luminance, as every ramp does, with its lightest shade at
least 100 above its darkest; no two consecutive entries of a preset are equal; hotter's darkest red
rises and its dark green falls level over level, colder's darkest blue rises and its dark red falls,
and rainbow's ten dark shades all differ. Whether they look hotter, colder and like a rainbow is
judged by eye on a running build; the colors are in `src/render/rotation_presets.h`.

## Implementation details

| Piece | Where |
|---|---|
| The rotation index | `src/render/palettes.h` — `kRotationRamp`, `kPaletteChoiceCount` |
| The live set on the atlas | `src/render/tile_atlas.h` — `TileAtlas::rotation`, `rotationShades`, `palettesFor`, `shadesOf`, `updateRotationPalette` |
| The presets | `src/render/rotation_presets.h` — `kHotterRotation`, `kColderRotation`, `kRainbowRotation`, `presetRotation` |
| The custom rotations and their document | `src/state/custom_rotations.h` / `.cpp` — `RotationProgression`, `CustomRotations`, `savedSlotOf`, `hasUnsavedChanges`, the codec, `saveCustomRotations` / `loadCustomRotations` |
| The settings byte | `src/state/settings.h` — `PaletteRotation`, `paletteRotationOf`, `Settings::paletteRotation`, schema 7 |
| The frame's palette | `src/systems/frame_palette.h` — `roundPaletteShown`, `rotationLevelOf`, `rotationPaletteFor`, `framePaletteIndex`, `reconcileRotationPalette` |
| The shared walk | `src/systems/palette_walk.h` — `PaletteChoice`, `paletteChoiceOf`, `nextPaletteChoice`, `paletteChoiceOfColors`, `colorsOf` |
| The host | `src/main.cpp` — the load beside the custom palettes, the reconcile in the simulation tick, `framePaletteIndex` in the render callback |

The document: `"rotations"`, version 1, 12,100 bytes — one hundred records of 121 (a presence byte,
then ten levels of red, green and blue for the darkest, dark, light and lightest shades); record 0 the
working rotation, records 1 to 99 the slots. Alpha is not stored. The settings document grows to nine
bytes at version 7.

The engine-side detail is in [`../engine/settings.md`](../engine/settings.md#the-frames-palette).

## Open questions / future work

- The rotation screen: the layout above, the selector over off, the presets and the saved custom
  rotations, and the held preview with its `Level` item. How a custom rotation is edited, and how a
  preset seeds one, are still to be designed.
- The three presets' colors are to be judged by eye on a running build.
