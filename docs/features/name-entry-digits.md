# Name entry digits

**Date:** 2026-11-14
**Status:** Complete

## Concept

The digits `0` to `9` on the letter wheel a player spells a top-score name with. The Game Boy's wheel
runs `a` to `z`, a period, a hyphen and the multiplication sign — the heart too, in heart mode — and a
space, and has no digits. With this on, the digits follow the symbols, ahead of the space: Up from
the last symbol is `0`, Up from `9` is the space, and `a` still follows the space.

It is an enhancement, offered where the original lacks it: off by default, so the name entry is the
cartridge's until a player asks for more, and a build with it off runs exactly the wheel a build
without it runs.

## Design decisions

### Offered where the original lacks it, and nowhere it has nothing to offer

The rotation editor's name screen carries the digits always, because a rotation's name starts as its
slot's number, and in the same place: after the symbols, ahead of the space. The top-score entry does not, because the cartridge's did not: digits there are help the
original does not give, and they are offered the way every such help is, behind a switch that starts
off.

### A page of its own, and a screen of its own

The row is the only one on its settings page, `enhancements 2`, and the four resets move after it to
`enhancements 3`. The switch is on a full screen of its own with a description of what it does, like
the other enhancement switches, rather than as a value in a row — the ghost piece's own screen is the
nearest, one option with its switch and the prose under it.

The page breaks are declared rather than divided: the settings screen's rows keep their order, and a
table names the row each page begins at. A page of one row in the middle could not be said by
dividing the row's number by four.

### The label

A row that opens a screen has sixteen cells for its label, and `name entry digits` is seventeen, so the
row reads `name digits`. `digits in name`, fourteen, was the other choice offered.

### Where the digits go on the wheel

`a` is first after the space, always: the letters open the wheel as the cartridge's does, and the
digits join the symbols at its end. So with the setting on, the last symbol — the multiplication sign,
or the heart in heart mode — goes up to `0` instead of the space, `0` to `9` step one glyph at a time,
and `9` goes up to the space; Down is the exact reverse. Placing the digits ahead of `a` was drafted
first and rejected: it put the digits ahead of the letters a name is mostly made of. The letters, the
symbols and the space are where the cartridge has them.

## Implementation details

| Piece | Where |
|---|---|
| The setting | `src/state/settings.h` — `Settings::nameDigits`, the settings document's tenth byte |
| The wheel | `src/systems/high_scores.cpp` — `wheelUp` and `wheelDown` take whether the digits are on; `enterTopScore` and `installHighScoreHandlers` take the settings |
| The screen | `src/systems/enhancement_screens.{h,cpp}` — the third carousel instance, `kNameDigitsOptionCount`, and its description |
| The row and its page | `src/state/screen_ui_state.h` — `SettingsRow::NAME_DIGITS`, `kSettingsFirstRowOfPage`; `src/systems/settings_screen.cpp` — its label, its arrow, its opener and the page's title |
| The game states | `include/kirpich/game_state.h` — `INIT_NAME_DIGITS_SCREEN`, `NAME_DIGITS_SCREEN` |
| The host | `src/main.cpp` — the settings handed to the top-score install, and the screen's option arrows |

Adding it grew the settings document from nine bytes to ten, at **schema version 8**, with a registered
`7 → 8` migration that appends the flag as off. A version 7 document migrates on the way in rather than
being read short.

The engine-side detail is in [`../engine/high-score-state.md`](../engine/high-score-state.md) and
[`../engine/settings.md`](../engine/settings.md#rows-and-pages).

## Open questions / future work

- The screen's description is wrapped by hand and judged by eye on a running build.
