# Custom palettes

**Date:** 2026-10-19
**Status:** Complete

## Concept

The eighty color ramps are the port's. A custom palette is the player's: four colors of their own,
edited in decimal on a screen of their own, kept in ninety-nine slots they can save into, and drawn
through live — the whole game recolors as a value steps. The Palette screen's selector walks on past
ramp 80 through every slot a palette has been saved into, so a saved palette is chosen the way a ramp
is.

## What the player gets

- **The Palette screen** offers its eighty ramps and then every saved custom palette, in slot order.
  A saved palette reads `C` and its slot number, `C01` to `C99`. A custom palette in use that is saved
  in no slot reads `C`.
- **Down on the Palette screen opens the palette editor**; a down arrow says so, and the editor's up
  arrow leads back.
- **The palette editor** has a `preset` selector, `01` to `99`, with `load preset`, `save preset` and
  `clear preset` under it, and below those four numbered rows, one per shade, each
  showing its red, green and blue as three decimal digits, `000` to `255`, with the row's color beside
  them. Pressing A on a value enters it — it changes color to say so — and then Left and Right pick the
  place (hundreds, tens, ones), Up and Down step the value by that place, clamped to `0`-`255`, A
  confirms and B cancels. Text at the bottom of the screen says so while a value is being edited.
- **Load** copies the selected slot into the working palette, asking first when the working palette
  has unsaved changes; an empty slot has nothing to load. **Save** writes the working palette into the
  selected slot, asking first when it would overwrite another palette. **Clear** empties the slot,
  asking first; clearing the palette in use returns the game to the ramp underneath. An empty slot's
  number is dimmed. The screen's title carries an asterisk while the working palette has unsaved
  changes.
- **Holding Select** shows the palette as the game will use it: the seven pieces, text in its three
  states (selected, unselected, dimmed) and the wall tiles that flank the playfield.

## What is built

- The game can be drawn through a custom palette. Every screen that takes a palette index accepts one
  more value, past the eighty ramps, which names the custom set.
- The custom palettes persist: the working palette and the ninety-nine slots, in their own document.
- The settings record whether the custom palette is in use; the ramp the player chose stays underneath,
  so stepping off the custom palette lands back on it.
- The Palette screen's selector walks on through the saved slots and reads `C<nn>` / `C`.
- **The palette editor**, opened by Down on the Palette screen (which shows a down arrow) and left by Up
  on its first row or B. Its title reads `custom palette`, with an asterisk after it while the working
  palette has unsaved changes - saved in no slot and not the untouched grayscale. A `preset` selector
  picks the slot `load preset`, `save preset` and `clear preset` act on, its number dimmed while the
  slot is empty; each action's label is dimmed while it would change nothing. Load asks first while
  the working palette has unsaved changes, save asks first when it would replace a different palette,
  and clear always asks. Clearing the slot that holds the palette in use puts the game back on the ramp
  the player chose underneath it. Below them, the four shades as numbered rows, each with its red, green and blue
  as three decimal digits, and the working palette's four colors as a band beside them.
- **Entering a value**: A on a value highlights it in the dark shade, with arrows either side where the
  place can move and over and under the digit being stepped where the value can move. Left and Right
  pick the place, Up and Down step the value by it - clamped to 0-255, repeating while held - A keeps
  it and B puts back what it was. `a confirms b cancels` stands at the bottom meanwhile. Every step is
  saved as it is made, so a palette being edited survives a relaunch.
- **The held preview**: while Select is held, in any mode of the editor, the screen shows the working
  palette as the game will use it — the heading, the words `selected`, `unselected` and `dimmed` in the
  three inks text is drawn in, the seven pieces where the Palette screen shows them, and a column of
  the playfield's brick wall down each edge of the screen. It lasts exactly as long as the button is
  down; nothing on the editor moves, steps or answers meanwhile, and on release the editor is where it
  was, an entered value still entered and a question still up.

The editor itself is drawn through the built-in ramp underneath, so it never recolors while a value
steps; the band shows the colors as they are. The held preview is drawn through the custom palette,
whether or not it is the one in use.

## Design decisions

**One live palette set, changed in place.** Every ramp's palettes are uploaded once at startup and a
ramp is chosen by picking between handles. A custom palette gets one more set of the same shape,
uploaded at startup from the grayscale, and the engine changes its colors in place under the same
handles whenever the working palette changes — one call per edit, drawn from the next frame on, and the
palette store never grows. The alternative of uploading a set per slot was rejected: ninety-nine mostly
empty sets, and a slot's colors would still have to change in place when edited.

**Saved is a comparison, never a stored number.** Nothing records which slot the working palette came
from. It is saved when some slot holds exactly its colors, and that slot's number is what the Palette
screen shows; it is unsaved when none does. So nothing can drift from the colors beside it — a slot
cleared from under a palette in use simply makes it `C`.

**The slots and the working palette are one document.** The working palette is the palette the game is
drawn through while a custom palette is in use, so it has to survive a relaunch, or a player who quits
with an unsaved palette comes back to a different game than they left. It is player content rather than
a setting, so it rides in the palettes document as record 0 rather than in the settings.

**A fresh palette is the original grayscale.** What an empty slot loads, and what the working palette
starts as: the four shades the hardware's own map to.

**Decimal, always.** A color channel is one 8-bit value shown as three decimal digits. The place being
edited is a cursor position over that one value, never a separate digit: stepping the hundreds up from
`099` gives `199`, then `255`.

**The Palette screen keeps its one row and no state.** The palette editor is reached by Down, with a
down arrow, rather than by a second row — so the screen's logic stays the dispatch table it was, with
one more entry.

**The entered value is a selector, and nothing blinks.** A value being edited is highlighted whole,
with an arrow either side and an arrow over and under the digit being stepped, each only where a press
would move — the rule every selector in the game follows. Only the idle row cursor blinks, as it does
on every sibling screen. Blinking digits were rejected.

**The editor does not recolor as a value steps.** It draws through the built-in ramp underneath, and
the band beside the shade rows shows the working palette's colors directly. Judging the palette in use
is what the held preview is for.

**An action that would change nothing is an end stop, and its label is dimmed.** Loading an empty
slot or one that already holds the working palette, saving into a slot that already holds it, and
clearing an empty slot do nothing and ask nothing. A question is asked only where its answer cannot be
taken back: loading over unsaved changes, saving over a different palette, and clearing. An empty
slot's number is dimmed too, so its state reads at a glance.

**Unsaved means there is something to lose.** The asterisk and the load question both mean the
working palette has changes no slot holds. A palette that is saved in a slot has nothing to lose, and
neither does the untouched grayscale a palette starts as, even though no slot holds it.

**Clearing the palette in use returns the game to its ramp.** A palette in use that is cleared from its
only slot would otherwise leave the Palette screen reading `C` with the colors still drawn. The game
goes back to the built-in ramp the player chose underneath it, as stepping off the slots does; the
working palette stays as it was.

**Every position on the editor is a pixel.** Nothing on it is placed on the background's cell grid but
the held preview's two brick columns; its layout is its own, in `layout.h`, with the space between the
shade rows, the arrows' distance from a digit and the space between the preview's words each one
number.

**The held preview is the one part of the editor drawn through the custom palette.** Every part of it
— the backdrop, the bricks, the heading, the words and the pieces — draws through the custom set, so
the player judges the palette in exactly the palettes the game will use. It is a level, read every
frame, rather than a mode entered and left: holding Select shows it, releasing ends it, and no input
table runs while it is up, so the editor resumes untouched. It reuses the Palette screen's piece
preview rather than drawing its own.

**The bricks stand at the two screen edges, as background cells.** The wall is the playfield's own left
wall, read from the playfield's tilemap row by row, so its three tiles repeat exactly as they do beside
the well. It is drawn as cells of the backdrop through the content palette, because the brick's
lightest shade is a real color there; drawn as sprites through an object palette, that shade would be
see-through. The two edges were chosen over the playfield's own columns because the heading and the
widest row of pieces both cross where the well's right wall would stand; at the edges the bricks flank
everything and touch nothing. These two columns are the one thing on the screen placed by cell.

**The order of a step onto a slot.** The atlas is told the colors, then the document is written, then
the setting is stored — so a frame never names colors the set does not yet hold.

**No rule binds a custom palette's order.** The ramps run dark to light because they are authored; a
custom palette's four entries are the four shades in order, and whatever colors the player puts there
are what draws. The clamp is the only check a value gets.

## Implementation details

| Piece | Where |
|---|---|
| The custom palette index and the fresh palette | `src/render/palettes.h` — `kCustomRamp`, `kPaletteChoiceCount`, `freshCustomPalette` |
| The live set on the atlas | `src/render/tile_atlas.h` — `TileAtlas::custom`, `customShades`, `rampPaletteColors`, `palettesFor`, `shadesOf`, `updateCustomPalette` |
| The palettes and their document | `src/state/custom_palettes.h` / `.cpp` — `CustomPalettes`, `savedSlotOf`, `nextPresentSlot`, `previousPresentSlot`, the codec, `saveCustomPalettes` / `loadCustomPalettes` |
| The settings byte | `src/state/settings.h` — `Settings::customPalette`, `paletteIndexOf`, schema 6 |
| The Palette screen's walk | `src/systems/palette_walk.h` — `PaletteChoice`, `paletteChoiceOf`, `nextPaletteChoice`; `src/systems/palette_settings_screen.h` — `CustomPaletteWiring`, `paletteCanStep` |
| The Palette screen's picture | `src/render/screens/palette_settings/screen.cpp` — the `C<nn>` / `C` value, the arrows, every part through the palette in use |
| The palette editor | `src/render/screens/palette_editor/` — `PaletteEditorState`, the logic (`openPaletteEditor`, `paletteEditor`, `steppedValue`, `presetCanStep`, `paletteEditorActionCanAct`), `layout.h`, `PaletteEditorScreen` |
| The held preview | `PaletteEditorState::previewing`, set each frame from Select held in `paletteEditor`; `PreviewBackdrop` (`preview_backdrop.h`) and `PalettePreview` (`palette_preview.h`) beside the screen; `kPreviewWords`, `kPreviewWordPitch` and the wall columns in `layout.h` |
| The seven pieces | `src/render/components/piece_preview.h` — `PiecePreview(keyStem, atlas, ramp)`, drawn by the Palette screen as `palette-piece` and by the held preview as `preview-piece` |
| One channel of a palette | `src/state/custom_palettes.h` — `ColorChannel`, `channelValue`, `setChannelValue` |
| The selected text ink and the asterisk | `src/render/tile_atlas.h` — `RampPalettes::fontSpriteSelected`, `TileAtlas::glyphExtras`; `src/render/glyph_extras.h`; `SelectedGlyphs` in `src/render/components/glyphs.h` |

The document: `"palettes"`, version 1, 1300 bytes — one hundred records of thirteen (a presence byte,
then red, green and blue for the darkest, dark, light and lightest shades); record 0 the working
palette, records 1 to 99 the slots. Alpha is not stored. The settings document grows to eight bytes at
version 6; a document from any earlier release migrates forward with the custom palette off.

The engine-side detail is in [`../engine/settings.md`](../engine/settings.md).

## Open questions / future work

- Three spacings are set by eye on a running build: the distance between shade rows
  (`kEntryPitch`), the arrows' distance from a digit (`kArrowRise`) and the distance between the held
  preview's words (`kPreviewWordPitch`).
