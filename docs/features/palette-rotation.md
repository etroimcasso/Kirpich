# Palette rotation

**Date:** 2026-10-23
**Status:** Complete

## Concept

A player asked for each level to wear a different color palette as a round goes on. With a palette
rotation on, a round is drawn through a palette chosen by the level it shows, so the game changes color
as the player levels up. Three rotations ship — **hotter**, **colder** and **rainbow** — and the player
can make rotations of their own, kept in ninety-nine slots the way custom palettes are. Off by default,
so a player who never turns it on sees the game they always saw.

NES Tetris is the reference for the feel: it carries ten palettes and picks one by the level's last
digit, so the cycle repeats every ten levels. This game does the same.

## What the player gets

- **Up on the Palette screen opens the rotation screen**, the mirror of Down opening the palette
  editor; the Palette screen shows an up arrow beside its down arrow. The rotation screen belongs with
  the palette screens; the settings menus do not change. Down past its selector, or B, goes back.
- **The screen's menus sit at the bottom**, because it is entered by scrolling up: the `rotation`
  selector at the bottom, the `edit rotation` item above it, and above them the ten levels as two rows
  of five, each cell the level's number over that level's four colors.
- **The selector** runs `off`, then `hotter`, `colder` and `rainbow`, then every rotation the player has
  saved — by the name it was saved under, or as `C01`, `C04` and so on where it has none. Stepping onto
  a saved rotation puts it into use. A rotation in use that is saved in no slot reads `C`.
- **The grid shows what the selector chose**, and changes as it steps. On `off` every cell shows the
  player's own palette, because with rotation off that is what every level looks like.
- **Holding Select** shows the palette editor's preview — the three kinds of text and the seven pieces
  between brick walls — through one level's palette, with a `level` item below it. While Select is
  held, Up or Right steps the level forward through `0`-`9` and Down or Left steps it back, the number
  carrying an arrow each way it can still move. On `off` the preview shows the player's own palette at
  every level.
- **`edit rotation` opens the rotation editor**, and B in the editor comes back to it.
- **The rotation editor** edits the player's own rotation level by level. At the top, `preset`,
  `load preset`, `save preset` and `clear preset`, as the palette editor has them: the `preset`
  selector runs `hotter`, `colder` and `rainbow`, then slots `1` to `99`, a saved slot reading its name
  and an empty one dimmed. Loading a shipped rotation is how a preset becomes the start of the player's
  own.
- **Below them, the ten levels** as two columns of five, each cell reading its level, a colon and the
  number of the palette that level wears — a ramp's number, `C` and a saved palette's slot, or `C` for
  colors on neither — with that palette's four colors under it. A on a cell enters its number; Left and
  Right then step it one at a time through the ramps and the saved palettes, and Up and Down ten at a
  time, stopping at either end, all held to repeat; A keeps it and B puts back what the level wore.
- **`save preset` names the rotation.** A screen of its own opens holding the slot's name, or its
  number, and takes up to seven characters the way the top-score entry takes a name: Up and Down turn
  the letter at the place through `a` to `z`, a period, a hyphen, `0` to `9` and a space; A moves on and
  finishes on the seventh; Start finishes; B moves back, and on the first place cancels the save. Saving
  again onto a rotation's own slot renames it.
- **Holding Select in the editor** shows the preview through the selected level's palette, whichever
  rotation is switched on, with the Type A panel's level box at the top showing which level it is. The
  four directions step the level while the button is down, and the cursor follows.
- **Clearing the slot that holds the rotation in use** turns the rotation off, as clearing the palette
  in use puts the game back on its ramp.

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
  one the rotation editor edits — and ninety-nine slots it can be saved into, each with the name it was
  saved under, in their own document.
- **The settings record which rotation, if any, a round follows**: off, one of the three presets, or
  custom.
- **The Palette screen's walk is shared.** The sequence the Palette screen steps — the eighty ramps,
  then every saved custom palette — lives in a module of its own, and the rotation editor's numbers
  step the same sequence with the same end stops, read the same way.
- **The rotation screen**, described above, with its own walk over `off`, the presets and the saved
  rotations.
- **The rotation editor**, described above, and the text input screen it raises to name a save.
- **The font gains a colon**, in the port's own extras sheet beside the asterisk, for the cells' `0:`.

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

**The held preview reaches the rotation set through the reconcile.** While Select is held on the
rotation screen, the per-tick reconcile writes the selected level's palette into the rotation set, and
the preview draws through it — the same set and the same writer a round uses. Having the screen write
the set itself was rejected: it would make a second writer of a set that has one, and the preview
would stop being drawn exactly as a round draws.

**The grid is color squares; the tiles are on the held preview.** Each of the ten cells is the level's
number and its four colors as solid squares, the swatch the Palette screen already draws. Tile art in
each cell was considered and is not what the screen is: the pieces, the text and the bricks belong to
the preview Select shows.

**On `off`, the screen is honest.** With rotation off a round is drawn through the player's own palette
at every level, so the grid and the held preview show exactly that rather than the last rotation
chosen.

**Seven characters is the width a rotation's name and a shipped name share.** `rainbow` is seven, and
the rotation screen's selector and the editor's `preset` selector each leave room for seven, so a
rotation the player names reads in the same place a shipped one does. The build checks both against
the selectors' width.

**A level's number is a derived identity.** A cell's number is whichever stop on the palette walk
holds the level's colors, read afresh each frame, not a number stored beside them — the same reason a
rotation stores colors. Storing a number per level was rejected for the reason above: a saved palette
cleared later would change the level under the player.

**An entered number steps from where it stands.** While a number is entered the editor holds its place
on the walk and steps from there. Re-deriving the place from the level's colors at every step was
rejected: a saved palette holding a ramp's colors reads as that ramp, so the walk would fall back onto
the ramp at each step and never reach the saved palettes after it.

**Up and Down move an entered number by ten, and clamp.** With eighty ramps before the first saved
palette, one stop at a time is slow. Up and Down move ten stops, as the palette editor's tens place
moves a value by ten, and like that value they clamp: a ten-step near either end goes as far as the
walk does and stops there. They carry no arrows; Left and Right's arrows say a step exists.

**The shipped rotations are on the preset selector.** The request asked that a preset serve as a
starting point a player then tweaks level by level. Putting `hotter`, `colder` and `rainbow` ahead of
the slots on the editor's `preset` selector makes `load preset` that starting point, with no separate
control. A shipped rotation cannot be saved over or cleared.

**Two columns of five.** A cell reads `0: C04` with an arrow either side of the number while it is
entered, which does not fit five across the screen. Levels 0 to 4 run down the left column and 5 to 9
down the right — the rotation screen's two rows stood on end.

**The level box stands where the heading would.** The held preview carries the box a Type A round shows
its level in, read from the playfield's own tilemap with the level's digit in its number cell. The box
is four cells tall and the preview's top four rows are the only free ones, so the box takes them and the
editor's preview has no heading.

**A colon in the extras sheet.** The font has none. It is drawn in the port's own sheet as the asterisk
is, through the same font palettes, so it inks as a letter does.

**Clearing the rotation in use turns the rotation off.** Clearing a slot that holds the rotation a round
is wearing, with no other slot holding it, sets the rotation off, as the palette editor's clear puts the
game back on the ramp underneath. The working rotation itself is left as it is.

**A saved rotation has a name.** A name is the slot's: seven characters, starting as the slot's number,
given when the rotation is saved, on a text input screen that takes its characters the way the
top-score entry takes a name, with digits on its wheel because a name starts as a number. It is read on
both selectors. A slot with no name reads as its number. The working rotation has none, and a name
takes no part in whether the working rotation is saved.

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
| The presets | `src/render/rotation_presets.h` — `kHotterRotation`, `kColderRotation`, `kRainbowRotation`, `presetRotation`, `presetRotationName`, `kRotationPresetNameMaxGlyphs` |
| The custom rotations and their document | `src/state/custom_rotations.h` / `.cpp` — `RotationProgression`, `RotationName`, `CustomRotationSlot`, `CustomRotations`, `rotationNameText`, `savedSlotOf`, `hasUnsavedChanges`, the codec, `saveCustomRotations` / `loadCustomRotations` |
| The settings byte | `src/state/settings.h` — `PaletteRotation`, `paletteRotationOf`, `Settings::paletteRotation` |
| The frame's palette | `src/systems/frame_palette.h` — `roundPaletteShown`, `rotationLevelOf`, `rotationPalettes`, `rotationPaletteFor`, `framePaletteIndex`, `reconcileRotationPalette` |
| The shared walk | `src/systems/palette_walk.h` — `PaletteChoice`, `paletteChoiceOf`, `nextPaletteChoice`, `paletteChoiceOfColors`, `colorsOf`, `paletteChoiceText` |
| The rotation walk | `src/systems/rotation_walk.h` — `RotationChoice`, `rotationChoiceOf`, `nextRotationChoice` |
| The rotation screen | `src/render/screens/palette_rotation/` — `RotationScreenState` (the cursor's row, `EDITOR` or `SELECTOR`; the held preview's flag and level), the logic (`RotationWiring`, `openRotationScreen`, `rotationCanStep`, `rotationScreen`, `installRotationScreen`), `layout.h`, `LevelGridDigits`, `LevelGridSwatches`, `PaletteRotationScreen`; `PALETTE_ROTATION` in `include/kirpich/game_state.h` |
| The rotation editor | `src/render/screens/rotation_editor/` — `RotationEditorState`, the logic (`openRotationEditor`, `rotationEditorPresetCanStep`, `rotationEditorActionCanAct`, `rotationPresetText`, `turnedNameWheel`, `levelPaletteChoice`, `enteredNumberCanStep`, `rotationEditor`, `installRotationEditor`), `layout.h`, `LevelCells`, `LevelCellSwatches`, `LevelPreviewBackdrop`, `RotationEditorScreen`; `ROTATION_EDITOR` in `include/kirpich/game_state.h` |
| The name screen | `src/render/components/text_entry.h` — `TextEntry` |
| The colon | `src/render/glyph_extras.h` — `kColonArt`, `kColonCell`, `glyphExtraSheet`; `src/render/components/glyphs.cpp` maps `:` |
| The level box's cell | `src/systems/readouts.h` — `kTypeALevelRow`, `kTypeALevelCol` |
| The way in | `src/systems/palette_settings_screen.cpp` — Up opens the rotation screen; the Palette screen's up arrow |
| The shared components | `src/render/components/` — `ColorSwatch`, `LevelSelector`, `TextEntry`, and the held preview's `PalettePreview` and `PreviewBackdrop`, which the palette editor draws too |
| The host | `src/main.cpp` — the load beside the custom palettes, the rotation screen's and the rotation editor's wiring and installers, the reconcile in the simulation tick, `framePaletteIndex` and both screens' cases in the render callback |

The document: `"rotations"`, version 1, 12,800 bytes — one hundred records of 128 (a presence byte,
the seven bytes of the name, then ten levels of red, green and blue for the darkest, dark, light and
lightest shades); record 0 the working rotation, records 1 to 99 the slots. Alpha is not stored. The
settings document carries the rotation mode as its ninth byte.

The engine-side detail is in [`../engine/settings.md`](../engine/settings.md#the-frames-palette),
[`../engine/settings.md`](../engine/settings.md#the-rotation-screen) and
[`../engine/settings.md`](../engine/settings.md#the-rotation-editor).

## Open questions / future work

- **Showing on the Palette screen that a rotation is on.** Without it the Palette screen, the rotation
  screen and a round can show three different palettes. One shape considered is a `rotate` stop on the
  Palette screen's own selector; whether a rotation should then reach the menus at all is open, and the
  idea is set aside for now.
- The screens' words — the editor's heading and question lines, the rotation screen's words — the
  grid's square size and row spacing, the colon's art, the level box's place and the `level` item's
  place are judged by eye on a running build, each one constant.
- The three presets' colors are to be judged by eye on a running build.
