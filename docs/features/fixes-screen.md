# Fixes screen

The cartridge ships quirks a player notices, and Kirpich reproduces them, because they are what the
game does. The fixes screen is where the ones worth a way out of are offered back: each is an option
with a screen of its own — a title, an enable switch, and the room to say what the quirk is before a
player decides to keep it or lose it. Every option is **off by default**: fidelity is what a player
has until they ask for something else, the same posture the extra game types take.

Opened from the `fixes` row on the settings screen's second page. Up and down move between options —
a different option is a different screen, repainted whole — and left and right turn the shown one on
and off. The up arrow sits **above** the option's title, because the title belongs to the option: an
arrow below it would read as the description scrolling rather than as another option existing. The
down arrow sits below the description. An arrow is drawn only where there is an option in that
direction, so with one option neither appears. B returns to the settings screen.

## The options

| Option | Off (the cartridge's behavior) | On |
|---|---|---|
| `audio` | After an attract demo plays, the title screen stays silent for the rest of the session | The title music returns when a demo ends |

**Why the quirk exists.** The sound driver mutes every cue while the running-demo byte is set —
that is how a demo's recorded button presses stay silent — and nothing clears that byte until a real
round starts. The title screen re-cues its song while the byte is still set, so the driver swallows
the cue and the music never comes back. Real hardware does exactly this.

**What the fix does.** When a demo ends, the byte is cleared, so the title init's cue reaches an
open driver. The byte's second duty — remembering which recording ran, so the next demo alternates —
moves to a port-side field the launch reads when the byte is clear. The demos themselves still play
silent, as they do on hardware, and with the fix off the quirk is pinned by its own test.

## The carousel is general

The screen machinery (`src/systems/carousel_screen.h`) owns nothing per-instance: the options, the
flags their switches toggle, and the two dispatch slots an instance answers to all arrive through
its installer, so a second carousel is a second install rather than a second screen. A future mode
screen with more than one mode to offer would be one. A new fix is an entry in the options table in
`src/main.cpp` and a flag on `Settings` — nothing more.

## Where the pieces live

| Piece | Where |
|---|---|
| The carousel machine | `src/systems/carousel_screen.h` / `.cpp` |
| The option arrows (drawn) | `src/render/settings_overlay.h` — `carouselArrows` |
| The `fixes` row | `src/systems/settings_screen.cpp`, `SettingsRow::FIXES` |
| The audio fix itself | `src/systems/demo.cpp` — `checkForEndOfDemo`, `startDemo` |
| The saved flag | `Settings::fixAudio`, settings save schema version 4 |
| The options table | `src/main.cpp` |

## Tested by

`tests/test_carousel_screen.cpp` — the init's first-option paint, the toggle with its end stops, the
walk with whole-screen repaints, the toggle following the walk, the arrows under the range-end law
with up above the title, B's return, and the `fixes` row opening the screen with `new modes`
undisturbed beside it. `tests/test_demo_playback.cpp` — the fix's two terminals, the preserved
alternation, the hook wiring, the whole-demo replay under both postures, and the pinned default-off
quirk. `tests/test_settings_screen.cpp` — the settings screen's own demo-gate lift and restore.
`tests/test_settings.cpp` — the version 4 codec and the 3 → 4 migration.
