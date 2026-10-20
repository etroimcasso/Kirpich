# Custom palettes

**Date:** 2026-10-19
**Status:** In progress — the palette set, the document and the Palette screen's selector are built;
the editor, its load, save and clear, and the held preview are still to come.

## Concept

The eighty color ramps are the port's. A custom palette is the player's: four colors of their own,
edited in decimal on a screen of their own, kept in ninety-nine slots they can save into, and drawn
through live — the whole game recolors as a value steps. The Palette screen's selector walks on past
ramp 80 through every slot a palette has been saved into, so a saved palette is chosen the way a ramp
is.

## What the player gets

When the feature is complete:

- **The Palette screen** offers its eighty ramps and then every saved custom palette, in slot order.
  A saved palette reads `C` and its slot number, `C01` to `C99`. A custom palette in use that is saved
  in no slot reads `C`.
- **Down on the Palette screen opens the Custom screen**; a down arrow says so, and the Custom screen's
  up arrow leads back.
- **The Custom screen** has a `preset` selector, `01` to `99`, with `load preset`, `save preset` and
  `clear preset` under it, and below those the editor: four numbered rows, one per shade, each
  showing its red, green and blue as three decimal digits, `000` to `255`, with the row's color beside
  them. Pressing A on a value enters it — it changes color to say so — and then Left and Right pick the
  place (hundreds, tens, ones), Up and Down step the value by that place, clamped to `0`-`255`, A
  confirms and B cancels. Text at the bottom of the screen says so while a value is being edited.
- **Load** copies the selected slot into the working palette (an empty slot loads the original
  grayscale), asking first when the working palette is unsaved. **Save** writes the working palette
  into the selected slot, asking first when it would overwrite another palette. **Clear** empties the
  slot, asking first. The screen's title carries an asterisk while the working palette is saved in no
  slot.
- **Holding Select** shows the palette as the game will use it: the seven pieces, text in its three
  states (selected, unselected, dimmed) and the wall tiles that flank the playfield.

## What is built

- The game can be drawn through a custom palette. Every screen that takes a palette index accepts one
  more value, past the eighty ramps, which names the custom set.
- The custom palettes persist: the working palette and the ninety-nine slots, in their own document.
- The settings record whether the custom palette is in use; the ramp the player chose stays underneath,
  so stepping off the custom palette lands back on it.
- The Palette screen's selector walks on through the saved slots and reads `C<nn>` / `C`.

A custom palette reaches a slot only by hand for now — the document is written by the code paths that
exist (stepping onto a slot writes it) and read at startup, but nothing edits a palette until the Custom
screen lands.

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

**The Palette screen keeps its one row and no state.** The Custom screen is reached by Down, with a
down arrow, rather than by a second row — so the screen's logic stays the dispatch table it was, with
one more entry.

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
| The Palette screen's walk | `src/systems/palette_settings_screen.h` — `CustomPaletteWiring`, `paletteCanStep` |
| The Palette screen's picture | `src/render/palette_settings/screen.cpp` — the `C<nn>` / `C` value, the arrows, every part through the palette in use |

The document: `"palettes"`, version 1, 1300 bytes — one hundred records of thirteen (a presence byte,
then red, green and blue for the darkest, dark, light and lightest shades); record 0 the working
palette, records 1 to 99 the slots. Alpha is not stored. The settings document grows to eight bytes at
version 6; a document from any earlier release migrates forward with the custom palette off.

The engine-side detail is in [`../engine/settings.md`](../engine/settings.md).

## Open questions / future work

- The Custom screen itself: the editor, the three actions and their questions, the asterisk glyph (the
  font has none, so it is an embedded tile of the port's own), and the hint text's exact words.
- The held preview: the pieces, the three text states — which needs a third text palette for the
  highlighted state — and the wall tiles, which are background tiles and so are drawn as cells.
