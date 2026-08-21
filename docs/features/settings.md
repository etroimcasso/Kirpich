# Settings

**Status:** Complete

## Concept

A settings screen, drawn with the game's own tiles and operated like the game-selection screen, where
a player chooses how the game is shown and what it is drawn in. It holds fullscreen, the window size,
a colour palette, an erase of the high-score tables, and a way to quit. Every choice is the player's
own rather than the game's, so all of them outlive a round, a reset and the program itself.

The Game Boy has no settings screen, so nothing about this comes from the cartridge. It borrows the
cartridge's manners — the same cursor, the same blink, the same sounds — because the screen has to sit
beside screens that do come from it.

## Reaching it

Two ways in, and it returns to whichever one it came from:

- **The title screen**, as a third item under `1 PLAYER` and `2 PLAYER`.
- **A paused round**, by pressing A. The paused screen says so, in the field area it leaves blank.

Leaving a paused round's settings screen puts the round back exactly as it was — the paused screen,
the hidden piece sprites, the suspended music. Nothing about the round is disturbed by having looked
at the settings.

## The screen

Two pages. Down from the last row of a page turns to the next, up from the first turns back, and an
arrow at the edge of a page says the other one is there. The header counts: `settings 1`, `settings 2`.

| Page | Row | Values |
|---|---|---|
| 1 | `fullscreen` | `on` / `off` |
| 1 | `size` | `1x` – `8x`, screen pixels per Game Boy pixel |
| 1 | `palette` | `◄ n ►`, twelve colour ramps, with the chosen one previewed beneath the row |
| 1 | `exit game` | asks first |
| 2 | `reset scores` | asks first |

Erasing the scores sits on its own page deliberately: it is the one thing on the screen a player
cannot undo, and it should not be one press away from the row above it.

Both of the rows that end something go through the same confirm, which opens on `no` every time. A
player who arrives at one by accident leaves it by pressing whatever brought them.

## Design decisions

**The font is the layout constraint.** The game's font is one case of 26 letters, ten digits, and
about ten punctuation glyphs — no colon, no slash, no question mark, no arrows. So a row reads
`fullscreen   on` rather than `Fullscreen: On`, the confirm asks its question without a question mark,
and the page header reads `settings 1` rather than `settings/1`. Every string on the screen goes
through the same character map the cartridge's own text does, which refuses anything it cannot spell.

**Arrows and colour are shapes, not tiles.** The palette scroller's two arrows, the preview strip
under it, and the page arrows are drawn as filled regions over the finished frame. The art has no
arrow and no solid-colour tile, and a region is placed per pixel rather than per cell — which is also
what lets the preview's four squares touch each other, so the strip reads as one palette instead of
four blocks.

**A palette changes what the four shades are, and nothing else.** The Game Boy draws everything
through four shades; a ramp replaces those four colours and leaves every tile, every sample and every
layout untouched. Objects take the same ramp with its last entry made see-through, which is the
hardware's own rule — so a ramp gives three visible colours to a sprite and all four to the
background. Ramp 1 is the greyscale the hardware's shades map to, so a player who never opens the
screen sees exactly what they always saw.

**The screen borrows the caller's screen rather than rebuilding it.** Opening the settings screen
copies the background map the display is reading and the object buffer, and leaving puts both back.
That is what makes returning to a paused round exact: nothing has to know how to reconstruct a paused
screen, because the paused screen was never lost. It paints on whichever map is being displayed,
which is also the one nothing else is writing — during a round the game keeps drawing into the other.

**The window's size is editable while fullscreen is on.** It has no visible effect there, and it takes
effect the moment fullscreen is turned off. The alternative — greying the row out or skipping it in
the cursor's walk — makes the cursor behave differently depending on an unrelated row's value, and
hides a setting that is still perfectly meaningful.

**Confirming an exit does not go back to the settings screen first.** The confirm stays up until the
run ends. Returning first would show the player a screen they have just left, and then quit out of it.

**A changed setting is written out immediately.** There is no save button and no exit-to-apply. A
player who changes something and quits comes back to it.

## Persistence

The settings live in their own save document beside the top scores, under the same identity: three
bytes, one per setting. An absent document is an ordinary first run and leaves the defaults; a damaged
one is reported, leaves the defaults, and is left on disk rather than overwritten.

A **shorter** document is read as far as it goes and every setting it does not carry keeps its default,
so a document written before a setting existed costs the player that one setting rather than all of
them. A **longer** one is refused, because nothing can be said about bytes this build does not
understand. A value out of the range this build offers — a window size or a palette number from a
build that offered more — is clamped rather than refused, for the same reason.

The settings are read before the window is opened, so a player who chose fullscreen never sees a
windowed frame first, and the window opens at their size rather than at a default and then jumping.

## The fullscreen chord

`Alt`+`Enter`, and `Cmd`+`Enter` on macOS. It sets the same setting the row sets and is written out
the same way. It cannot be an action binding — the engine's action map has no notion of a modifier —
so the two keys are read from the platform directly.

## Leaving fullscreen from outside the game

A player can leave fullscreen without going through the game: macOS lets a fullscreen window be
dragged out of its Space, and every desktop has an equivalent. The game listens for the window's own
report of the change and adopts it, so the `fullscreen` row always says what the player is looking at.
It listens rather than asking every frame, because the state changes a handful of times in a session.

## Open questions / future work

- Answering "am I fullscreen?" belongs to the engine's window surface rather than to the game; the
  game currently listens for the platform's event itself. A request is filed upstream, and when it
  lands the listener comes out and the setting reads the engine's answer.
- Pixel-art and CRT filters are a separate piece of work — they are post-process stages rather than
  anything this screen selects between.
- The palette applies to everything drawn. A per-screen or per-element palette is not offered and is
  not planned.
