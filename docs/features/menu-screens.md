# Menu screens

**Date:** 2026-08-16
**Status:** Complete (selection screens; the title and copyright screens are a separate unit)

## Concept

The pre-game flow a player moves through before a round: the config screen (which lays out the
game-type and music-type cursors), the game-type and music-type selectors, and the Type A / Type B
difficulty pickers. Each screen is one game state, so each is a free function the frame dispatcher runs
once per frame, matching the shape of the piece, line-clear, and scoring systems. This unit also
delivers the first real installs into the dispatch table (the dispatcher shipped with every slot
stubbed) and the menu half of the input vocabulary.

## Design decisions

**Free functions on `GameContext`, role-named.** The original's state labels are numeric slots; the
port names each by role (`selectGameType`, `initTypeADifficultyScreen`, …). No handler owns state —
everything read or written lives on the game-state aggregate.

**A semantic menu vocabulary bound to the gameplay buttons.** The screens read `MenuUp`/`MenuDown`/
`MenuLeft`/`MenuRight`/`Confirm`/`Back` rather than raw buttons. Each binds to the same physical source
as its gameplay counterpart — Confirm to the A button (shared with rotate-clockwise), Back to B (shared
with rotate-counter-clockwise), the directions to the movement / soft-drop sources — with the up
direction, unused in gameplay, taken by `MenuUp`. A single press fires both the gameplay and the menu
action; this is harmless because a state runs only one handler family. The alternative — reusing the
piece actions directly in menu code — was rejected: it would tie menu meaning to gameplay names and
obscure which press a screen actually reads.

**The cursor blink dissolves into one helper.** The original folds two unrelated things into one
routine — reading the pressed buttons and blinking the cursor. The port already delivers the pressed
set through the frame's joypad snapshot, so the helper (`blinkCursor`) keeps only the blink: gated on
the frame timer, toggling (not setting) the cursor's visibility so a same-frame visibility write
composes.

**Two behaviors the original's structure hides, verified against the source and preserved:**

- **The difficulty-init and config screens cue the menu-move sound on entry.** Each enters the
  cursor-placement helper at its top, which cues the sound; the config screen cues it once, the Type B
  difficulty init (placing two cursors) cues it as it seeds each. The original notes the cue may not
  audibly play — whether the driver consumes it is the audio system's concern — so the port writes the
  cue faithfully and leaves that question to the audio work.
- **The Type A level picker does not unhide its cursor as it leaves.** Its transitions write the next
  state only; the Type B pickers unhide theirs. The asymmetry is in the original and is preserved.

**The top-score refresh is a seam.** The difficulty screens call the original's top-score refresh where
the port takes an optional hook (`TopScoresRefresh`). It has no simulation effect here — the rows it
stages are consumed by the renderer — so it defaults to a no-op; the top-score-entry screen wires the
real refresh, and the tests pass a probe to confirm the seam fires at the right point.

**The config-screen body is separately callable.** The demo-start and two-player paths enter the config
screen partway through, so its body (`loadConfigScreenBody`) is split from the state entry
(`initConfigScreen`, which adds only the serial-register reset those other paths do differently).

## Implementation details

- `src/systems/menu_screens.{h,cpp}` — eight state handlers (`initConfigScreen`, `selectGameType`,
  `selectMusicType`, `initTypeADifficultyScreen`, `selectTypeALevel`, `initTypeBDifficultyScreen`,
  `selectTypeBLevel`, `selectTypeBHeight`) plus `loadConfigScreenBody`, the shared helpers
  (`positionMusicTypeSprite`, `switchMusic`, `updateDigitCursor`, `blinkCursor`, `loadSceneSprites`,
  `clearOamObjects`), and `installMenuScreenHandlers`.
- `include/kirpich/action.h` — the six menu actions.
- `src/systems/input.cpp` — `defaultActionMap` and `heldActions` extended with those six.
- The cursor slots are slot 0 (music / first digit cursor) and slot 1 (game-type / second digit
  cursor). Cursor positions come from the coordinate tables in `src/data/misc.h`; cursor sprites from
  the scene lists in `src/data/scene_sprites.h`.
- Digit cursors set their sprite to the digit for the selected value (digit 0 is sprite $20).
- The music-type value ($1C–$1F) is the cursor tile; `switchMusic` maps it to a song cue, with $1F
  ("off") mapping to the stop-all cue.
- Tests: `tests/test_menu_screens.cpp` — seven device-free cases (config init, each selector, both
  difficulty grids, the inits, and the blink/installer/action-adapter). Full-grid sweeps for every
  selector.

## Open questions / future work

- The title and copyright screens (states `$06`/`$07`/`$24`/`$25`/`$35`) complete the pre-game flow and
  are a separate unit; the engine page for this area is written to be extended by it.
- The top-score refresh hook is a no-op until the top-score-entry screen wires it.
- The two-player reuse of these screens (the two-player config and music-select states call into this
  code) lands with the two-player work; the handlers are plain callables so those units can reuse them,
  and the one multiplayer-only branch here (Back on the music screen) is already in place.
- Whether the LCD-off → load → LCD-on blank frames each init runs are shown is a later presentation
  decision; the simulation carries no effect from them.
