# Rebindable controls

**Status:** Complete — the bindings, their save document, the default controls, and the controls
screen the settings page opens.

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
art in the palette before leaving. Controls opens the controls screen.

## The controls screen

The eight buttons stand in rows, one per line, with a **key** column and a **pad** column beside them,
and **restore defaults** stands below them. The arrows move a cursor between the cells; B goes back to
the settings page.

- **A or Start on a cell waits for a press of that cell's kind** — a key in the key column, a
  controller button in the pad column. The cell reads `...` while it waits, and the bottom of the screen
  says `press a key` or `press a button`, with `esc cancels` under it.
- **The press is bound at once.** It works the moment it is bound and is saved as it is made, so a
  player who rebinds and quits keeps the binding. A key or button another Game Boy button holds swaps
  with it, as above.
- **Escape leaves the binding as it was**, in either column.
- **A press of the other kind is ignored** — a controller button while a key cell waits, a key while a
  pad cell waits, or a mouse button — and the cell keeps waiting.
- **The key just bound does nothing more until it is let go.** It may mean something new the moment it
  is bound — the key bound to A is now A — so it would otherwise act on the screen as if it had been
  pressed again: start another wait, or, taken from B, leave the screen. The screen waits until nothing
  is held before it reads input again. Restoring the defaults waits the same way, since the key that
  answered may mean something else under the defaults.
- **Any controller button can be bound,** shoulders, triggers and stick directions included. The
  player's own pad decides only what the names say.
- **Restore defaults puts every binding back** to the controls the game ships with, keys and
  controller buttons alike — after asking. A or Start on it asks `restore every key and button`, the
  way the settings page's reset rows ask, opening on `no`; `yes` restores and saves at once, and `no`
  or B leaves everything as it was. When the bindings already are the defaults there is nothing to
  restore, and it asks nothing.

### How the cells read

Each name fits five cells, in the game's font: lowercase letters, digits, a period and a hyphen.

- **Keys:** letters and digits as themselves; short names for the common keys — `enter`, `esc`,
  `bksp`, `tab`, `space`, `up`, `down`, `left`, `right`, `lshft`, `rshft`, `lctrl`, `rctrl`, `lalt`,
  `ralt`, `lmeta`, `rmeta` (the Windows or Command keys), `caps`, `ins`, `del`, `home`, `end`, `pgup`,
  `pgdn`, `f1` to `f12`, the keypad as `kp0` to `kp9`, `kpent`, `kp.`, `kp-`, `kpadd`, `kpmul` and
  `kpdiv`, the minus key as `-` and the period key as `.`. Any other key reads `k` and its number,
  because the font has no other punctuation to name it with.
- **Controller buttons** read as they are printed on the pad that is connected: the face buttons by
  their letter, so the same position reads `a` on a Nintendo pad and `b` on an Xbox pad; shoulders and
  triggers as `lb` / `rt` on an Xbox pad, `l1` / `r2` on a PlayStation pad and `l` / `zr` on a Nintendo
  pad; then `start`, `selct`, `home`, `share`, the d-pad by direction and the sticks as `lsup`, `rslft`
  and the like. PlayStation pads have symbols rather than letters, which the font cannot draw, so their
  face buttons read with the Xbox letters. With no pad connected, the names are the Xbox ones.
