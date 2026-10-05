# Rebindable controls

**Status:** In progress — the bindings, their save document, the default controls and the settings
page that reaches them are in place; the controls screen itself is not built yet, so the page's
Controls row opens nothing.

A player can choose which keyboard key and which controller button stands for each of the Game Boy's
eight buttons — Up, Down, Left, Right, A, B, Start and Select. The choice is kept across launches, in
its own save document beside the settings.

## Bindings belong to the Game Boy's buttons

The game reads its input as actions — rotate, shift, soft drop, and a separate set for walking menus —
but several actions share a button: A rotates the piece clockwise and confirms a menu choice; Down
drops the piece and walks a menu cursor. Binding the eight **buttons** rather than the thirteen actions
keeps those pairs together, so moving A moves both meanings and a player cannot end up with confirm on
one key and the clockwise rotation on another, or a soft drop on one key and menu-down on another.

| Button | What it does |
|---|---|
| Up | walks a menu up (gameplay leaves it free) |
| Down | soft drop; walks a menu down |
| Left / Right | shifts the piece; walks a menu sideways |
| A | rotates clockwise; confirms |
| B | rotates counter-clockwise; goes back |
| Start | pauses; starts; advances |
| Select | toggles the next-piece preview and heart mode |

Each button has exactly one key and one controller button. The engine's action map is built from the
eight bindings, so there is one place a binding is written and every action reads it.

## The defaults

Arrows, X for A, Z for B, Enter for Start and Backspace for Select on the keyboard; the d-pad, the
printed A and B, Start and Select on a controller — the emulator convention. A player who never opens
the controls plays with these.

## Changing a binding

- **A key or button another Game Boy button holds swaps with it.** Taking Z for A gives B the old X.
  Every key and every controller button stands for one Game Boy button at most, so one press is never
  two buttons, and no button is ever left without a binding.
- **Escape is never bound.** It backs out of choosing a new binding, so it cannot also be one.
- **Controller buttons are bound by where they sit.** The defaults name the face buttons by their
  printed letter, and that letter is on the east button of a Nintendo pad and the south button of an
  Xbox pad. The first time a player binds a controller button, every lettered binding becomes the
  position it has on the pad they are holding, so from then on each binding means the same physical
  button on every pad.

## The save document

`controls`, version 1: the eight bindings in button order. A document that is not a complete, usable
set — the wrong length, a key that does not exist, Escape, a controller button the engine does not
know, or one key bound to two buttons — is set aside rather than read in part, because a partial set
can leave a button that nothing presses. The defaults stand in, and the file is left where it is.

## The fullscreen shortcut

Alt+Enter (Cmd+Enter on macOS) toggles fullscreen, and Enter is also a Game Boy button — Start, unless
the player moved it. While the shortcut is held, whichever button Enter is bound to is held back, so
going fullscreen never also starts a round or pauses one.

## The settings page

The first settings page reads **Display**, **Palette**, **Controls** and **Exit Game**. Display opens a
screen holding fullscreen and the window size; Palette opens a screen holding the palette selector, a
swatch of the palette's four colors, and the seven pieces drawn in it, so a player sees the game's own
art in the palette before leaving. Controls is on the page and does nothing yet.

## Still to come

- The **controls screen**, behind the page's Controls row: the eight buttons in rows with a keyboard column and a controller column.
  Choosing a cell waits for a press of that kind; Escape leaves the binding as it was. The engine
  captures the press, bound or not, and reports a controller button by its position on the pad.
