# Heart mode

**Date:** 2026-09-05
**Status:** Complete

## Concept

A harder start. Heart mode shifts the gravity lookup ten levels up — a level-0 round falls at the
interval level 10 normally falls at, and the shift stops at the table's last row — and marks itself
with a heart: as the title-screen selector cursor while the mode is toggled, beside the heading on the
difficulty screen while a round is picked, and beside the level digit while a round is played. The
displayed level is unaffected, so the game is faster than it says it is.

It is the original's own easter egg, and it is armed there by holding Down while pressing Start on the
title screen. This port cannot be played that way, which is what this unit exists to answer.

## Design decisions

### Select turns it on and off, wherever the title cursor is standing

`Action::MenuDown` and `Action::SoftDrop` are the same physical Down. This port's title screen carries
a third item the cartridge's does not — the settings row — so a press of Down moves the cursor to that
row and returns before the Start branch that reads the held button. The only path that still reaches
the latch holds Down from before the title screen appears, where no press edge ever fires. Heart mode
is therefore unreachable by the route the original documents.

Select is the button available for it, and binding it there costs no other function: Left and Right
each move the player count one way, and between them they reach both counts. Select duplicated that on
the cartridge, from the other side. A test holds the guarantee, so a later change that narrows Left or
Right fails rather than quietly stranding the player count.

The toggle answers from the bottom row too, at the same call site rather than a branch per row. A
button that means one thing on one row and nothing on another is a rule a player has to learn twice.

### The selector cursor becomes the heart, and the toggle cues the menu-move sound

The press is answered where it happens: the selector cursor — the title screen's one moving part, and
the only object on it that belongs to the port rather than the cartridge — becomes the heart glyph the
moment the mode goes on, and the plain selector (`$58`) again the moment it goes off. That gives the
heart a placement of its own without touching artwork that is otherwise the cartridge's, which was the
objection to putting one anywhere else on the title screen. It also cues the menu-move sound, the one
every other selection on these screens makes; turning the mode off is as much a selection as turning it
on, so both directions cue it.

With the mode off the cursor is exactly the tile the cartridge shipped, so this is a pure addition:
nothing about the 1989 title screen changes unless the port's own heart mode is on.

### It lives for the session, and is not saved

Sticky until the machine is reset is the original's behaviour — `tetris.asm:698-706` writes the flag
and nothing anywhere clears it — and it is what this port does. Nothing writes it to disk, so there is
no settings schema change and no migration. A cold boot and the four-button reset chord clear it with
the rest of the flow state, through `GameFlowState::reset`.

### The cartridge's latch is kept, dead

It stays where the original has it, with its unreachability stated at the call site — the way the demo
recorder is carried dead-but-present under a proof. Removing it would be the only edit here that
changes what the port says about the original.

### The indicator is a sprite, not a write into the background map

Three reasons, each a class of defect avoided rather than a preference:

- **A gated declaration cannot strand.** The frame declares the heart while the gate is true and
  declares nothing when it is false, and the renderer reconciles by object key against the previous
  tick. A tile written into the map has to be undone by whoever wrote it, on every path out of every
  screen that could be showing it.
- **An object palette's lightest shade is see-through**, so only the ink lands and the backdrop
  survives under it. A background palette is opaque and paints out the cell it sits in.
- **Name entry draws no backdrop of its own.** It paints over whichever difficulty screen it was
  entered from, so a heart written into the map would follow the player onto it. A gate can say
  plainly whether it belongs there; a leftover cell cannot. The gate says it does — the heading it
  sits beside is still on the display.

The Type C rise values are drawn this way for the same reasons.

### The glyph is the one the round already draws

`CharTile::HEART` (`$27`), the same picture the panel puts beside the level digit during a heart-mode
round, so a player sees before choosing a round what they see while playing one. No new art.

## Implementation details

| File | What it holds |
|---|---|
| `src/render/heart_indicator.{h,cpp}` | The indicator: its gate, its placement offsets, and the one sprite it declares |
| `src/systems/title_screens.cpp` | `toggleHeartMode` (flips the mode and the selector cursor's tile) and the Select branch, above the row split so one call site serves both rows; the init seeds the cursor from the sticky mode |
| `src/systems/input.cpp` | Select bound to Backspace (the emulator convention, pairing with Enter as Start) |
| `src/systems/menu_screens.h` | `kDifficultyHeadingRow` / `Col` / `Cols` — the heading's cells, published so the indicator is placed against them |
| `src/main.cpp` | One gated append, where the frame's other bridge sprites are appended |
| `src/data/gravity.h` | `kHeartModeLevelBoost` and the cap — the speed shift itself |
| `src/systems/readouts.cpp` | The in-round heart beside the level digit |

The flag is read as zero / non-zero everywhere, never compared against a particular value: the original
latches the raw held-joypad byte there and this port's toggle writes a canonical `1`. The toggle
assigns rather than flipping bitwise, so any value reaching the field clears to exactly zero.

The heart sits one cell past the heading's last, on the heading's own row, placed by pixel with its own
nudge offsets. All three modes put their heading in the same six cells — the stored Type A and Type B
screens hold `a-type` and `b-type` there, and the Type C init writes `c-type` over the screen it
borrows — so one placement serves all three, and a `static_assert` ties the published width to the word
actually written.

## What has to be checked by hand

Every CI job is headless, so none of this is visible to the suite:

- Select (Backspace on the keyboard) at the title screen turns the selector cursor into the heart and
  cues the sound; toggling off restores the plain selector. The heart is on all three difficulty screens.
- A round started with it on falls at the shifted gravity, with the heart beside the level digit.
- A top score earned in heart mode still shows the heart on the name-entry screen.
- Toggling off clears the heart from all three screens.
- Left and Right still move between one and two players.

## Open questions / future work

None. The indicator's exact pixel offset is the one thing expected to be nudged on a running build;
both offsets are named constants at the top of `src/render/heart_indicator.h` for that reason.
