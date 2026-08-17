# Title and copyright screens

**Date:** 2026-08-16
**Status:** Complete

## Concept

The pre-game states from power-on to the moment a player picks a mode: the copyright chain that shows
the original owners' notices, and the title screen with its attract-demo countdown and its
one/two-player cursor. Each screen is one game state, so each is a free function the frame dispatcher
runs once per frame, matching the shape of the menu, piece, line-clear, and scoring systems. This unit
installs the last of the pre-game handlers into the dispatch table; from the title screen, one-player
Start enters the config screen (the selection-screens unit) that leads into a round.

## Design decisions

**The copyright screens stay and are shown verbatim.** They display the original owners' copyright
notices. Nothing renders without the player supplying their own ROM's tile graphics (the asset
posture), the copyright screen tilemap already shipped with the static screen data, and the screens are
observable behavior under the port's behavior-preservation premise — so preserving the notices is both
faithful and the conservative choice. Stripping them was rejected.

**The piece-ring over-copy is dropped.** The copyright init seeds the piece ring from the 48-entry demo
list, but the original's loop runs until the write pointer crosses a page boundary — copying 256 bytes
and over-reading 208 bytes past the end of the 48-byte list (the disassembly itself flags it as
copying "way, way too much"). The port copies the 48 real entries and leaves the rest of the ring
untouched. The over-copied tail is never read: solo play draws through the randomizer, a multiplayer
round refills the whole ring before use, and demo playback consumes only the low ring entries. So
dropping it is behavior-identical.

**Heart mode is a canonical non-zero flag.** Holding Down while pressing Start on the title screen
latches "heart mode". The original stores the raw held-joypad byte into that field, but every reader
tests it only for zero / non-zero. The port's input surface is action-based and has no joypad byte to
store, so it writes a fixed non-zero value — faithful to the only property that is ever read, without
reconstructing a raw button byte.

**The line-count clear is a whole-field clear.** The title init clears the high byte of the packed
line count (the low byte is left, and the score/stats reset does not touch the count). The port models
the line count as one decimal value, which cannot represent a partial clear of the packed byte, so it
clears the whole field. This is unobservable: nothing between this init and the next game-start reads
the count, and game-start rewrites it in full.

**The demo launch and the link-cable paths are seams.** When the attract countdown expires the original
launches an attract demo; the port fires a `StartDemoHook` there, defaulting to a no-op so a build
without the demo system idles at the title, and the demo system installs the real launch. The
title screen's serial poll (a peer-initiated two-player launch) and the two-player Start (a master-
election handshake) are link-cable mechanism with no effect without the serial subsystem; they are
recorded in the contract and left to the serial/multiplayer unit. The one-player flow — the countdown,
the cursor, and one-player Start — is complete.

**The one/two-player cursor is the multiplayer flag.** The title cursor position and the link-mode flag
are the same byte in the original (it is reused as the cursor index), and the port keeps that: Select
toggles the flag, Right moves one-player → two-player only, Left moves two-player → one-player only, and
the cursor's screen X follows.

## Implementation details

- `src/systems/title_screens.{h,cpp}` — five state handlers (`initCopyrightScreen`, `copyrightHold`,
  `copyrightSkippable`, `initTitleScreen`, `titleScreen`) plus `installTitleScreenHandlers`. The
  object-buffer clear comes from the selection-screens unit (`clearOamObjects`); the score and
  line-clear resets are the shipped scoring/line-clear functions.
- The copyright chain is timer-driven: the init arms a 250-frame display timer, the hold re-arms it and
  advances, and the skippable screen advances on any newly-pressed input or the timer expiring. "Any
  input" is a non-empty pressed set — every physical button binds to at least one action.
- The title init resets leftover round state, paints the title board (a full page of the empty tile,
  then wall columns 1 and 12 and a floor at row 18), places the one/two-player cursor as OAM object 0,
  cues the title music, arms a 125-frame timer, and seeds the attract counter (4 between attract demos,
  19 on a cold entry).
- The title loop runs the attract countdown (firing the demo seam at zero), the one/two-player cursor,
  and one-player Start (which latches heart mode if Down is held and enters the config screen with its
  entry zeroing).
- Tests: `tests/test_title_screens.cpp` — five device-free cases (the copyright flow, the ring fill, the
  title init, the cursor input, and Start + the attract countdown). The timer laws are exercised through
  the frame dispatcher; the demo seam is confirmed with a probe.

## Open questions / future work

- The demo-launch hook is a no-op until the attract-demo system wires it.
- The two-player serial paths (the peer-initiated launch and the two-player Start handshake) land with
  the serial/multiplayer work; the handlers are structured so those paths slot in.
- Whether the LCD-off → load → LCD-on blank frames each init runs are shown is a later presentation
  decision; the simulation carries no effect from them.
