# Settings

The player's display choices, the screen that edits them, and the colour ramps the game is drawn in.

Three pieces, in three places:

| Piece | Where |
|---|---|
| The values and their save document | `src/state/settings.h` / `.cpp` |
| The screens that edit them | `src/systems/settings_screen.h` / `.cpp` |
| The colour ramps and the parts of the screen that are shape rather than text | `src/render/palettes.h`, `src/render/settings_overlay.h` / `.cpp` |

## The values

```cpp
struct Settings {
    bool         fullscreen  = false;
    std::uint8_t windowScale = kDefaultWindowScale;  // 4
    std::uint8_t shadeRamp   = 0;                    // the greyscale ramp
};
```

`Settings` is not game state and is not on `GameContext`: it outlives a round and a reset, and it is
saved to disk. The host owns one and hands the screen a pointer to it.

```cpp
retropp::SaveStore saves = retropp::SaveStore::atPath(retropp::userDataDir(identity));
kirpich::Settings  settings;
kirpich::loadSettings(saves, settings);   // false when there is nothing saved yet
```

`loadSettings` returns `false` for an absent document (an ordinary first run) and for a damaged one,
leaving `settings` at its defaults either way and leaving a damaged file on disk. `saveSettings`
returns whatever the atomic write reports.

`clampWindowScale(int)` and `render::clampShadeRamp(int)` bring a value into the range this build
offers. Both are applied on the way in from disk, so a stored value can never name nothing.

### The save document

`"settings"`, version 1, three bytes: the fullscreen flag as 0 or 1, the window scale, the ramp.

`decodeSettings` accepts an image **shorter** than three bytes and leaves every value the image does
not carry at its default — which is what lets a document written before a setting existed cost the
player only that setting. It refuses an empty image and one longer than this build writes.

**To add a setting:** append a field to `Settings`, append its byte to `encodeSettings`, read it in
`decodeSettings` behind an `image.size() > n` guard, and raise `kSettingsImageBytes`. Existing saves
keep working without a version bump, because a short image is already the supported case.

## The screens

Four game states, installed together:

```cpp
kirpich::systems::installSettingsHandlers(
    dispatcher, kirpich::systems::SettingsWiring{
                    .settings   = &settings,
                    .apply      = [&](const Settings& s) { /* put s into effect */ },
                    .save       = [&](const Settings& s) { kirpich::saveSettings(s, saves); },
                    .saveScores = [&](const HighScoreState& h) { kirpich::saveTopScores(h, saves); },
                    .exit       = [&] { loop.exitRequest(); },
                });
```

Every seam defaults to inert. `apply` and `save` fire on each change; `saveScores` fires when the
confirm is answered yes for the score reset; `exit` fires when it is answered yes for quitting.

`openSettings(GameContext&)` is how the screen is entered — it records the current state as the one to
return to and enters `GameState::INIT_SETTINGS`. The title screen and the pause handler both call it.

### What the screen borrows

`initSettingsScreen` copies the background map the display is reading, the object buffer, and the
frame timer into `ScreenUiState` (`src/state/screen_ui_state.h`), and leaving puts all three back.
That is why returning to a paused round is exact — the paused screen is restored rather than rebuilt.

It paints on **whichever map is displayed**, so it never covers the map something else is writing:
at the title screen that is the first map, and in a paused round it is the second.

### Rows and pages

`SettingsRow` is the walk order; `kSettingsFirstPageRows` is how many of them the first page holds.

```cpp
enum class SettingsRow : std::uint8_t {
    FULLSCREEN, WINDOW_SCALE, SHADE_RAMP, EXIT_GAME, RESET_SCORES
};
```

**To add a row:** add an enumerator in the position it should be walked, give it a label in
`labelFor`, and handle it in `changeValue` (a value) or in the Confirm/Start branch of
`settingsScreen` (an action). Raising `kSettingsFirstPageRows` moves the page boundary; the page a row
lands on and the arrow that advertises the other page both follow from it.

**To change where a row sits**, edit `kSettingsFirstRow` and `kSettingsRowStride` in
`src/systems/settings_screen.h`. Those are the geometry the drawn parts read too, so a row and its
arrows cannot drift apart.

### Text

Every string goes through `writeMapText` (`src/systems/screen.h`), which encodes through the character
map and writes nothing at all for text it cannot spell. The font is one case, digits, and about ten
punctuation glyphs — **no colon, slash, question mark or arrow**. Check any new string against
`include/kirpich/char_tile.h` before committing to it.

## The colour ramps

```cpp
struct ShadeRamp { retropp::Rgba8 darkest, dark, light, lightest; };
inline constexpr std::array<ShadeRamp, 12> kShadeRamps{ /* ... */ };
```

Darkest first, matching the order the extractor's decode produces. `uploadTileAtlas` builds five
palettes per ramp — background font, background content, and the three object variants — and uploads
them all at startup, so choosing a ramp picks between handles and uploads nothing.

`resolveTile(index, sheet, atlas, ramp)` and `resolveSpriteTile(index, sheet, palette1, atlas, ramp)`
take the ramp; `composeBackground` and `composeSprites` pass it through. A ramp decides which colours
a sample resolves to and never which art a tile index names.

Object palettes take the ramp with its **last** entry replaced by transparency, which is the
hardware's rule: an object's lightest colour is see-through. A ramp therefore contributes three
visible colours to a sprite and four to the background.

**To add a ramp:** append a `ShadeRamp` to `kShadeRamps` and raise the array's size. Everything else
follows — the upload, the screen's count, and the clamp. Past ninety-nine ramps the two cells the
number is drawn in run out, which a `static_assert` says.

## The drawn parts

`settingsOverlay(ui, ramp, viewportWidth)` returns the regions for the palette scroller's arrows, the
preview strip, and the page arrow. The host pushes them onto the frame's regions while the settings
screen is showing:

```cpp
if (game.flow.gameState == kirpich::GameState::SETTINGS) {
    frame.regions = kirpich::render::settingsOverlay(game.screens, settings.shadeRamp,
                                                     kViewport.width);
}
```

Each is a polygon carrying one `ColorFill` — three points for an arrow, four for a preview square.
They are regions rather than cells because the art has neither an arrow nor a solid-colour tile, and
because a region is placed per pixel, which is what lets the preview's squares abut exactly.

An arrow is emitted only where there is somewhere to go: none to the left of the first ramp, none to
the right of the last, none above the first page or below the last.

## Applying a change

`apply` is the host's, because what a setting means is the host's business. In this port:

```cpp
retropp::Window& window = platform.window();
window.fullscreen(current.fullscreen);
if (!current.fullscreen) {
    window.size(retropp::PixelSize{kViewport.width * current.windowScale,
                                   kViewport.height * current.windowScale});
}
```

The size is applied only when windowed, where it is the only thing that can be seen.

## Fullscreen from outside the game

A player can leave fullscreen without the game's involvement. The host listens for the platform's
`ENTER_FULLSCREEN` / `LEAVE_FULLSCREEN` reports, adopts the value, and writes it out — so the
`fullscreen` row says what the player is looking at rather than what the game last set. It listens
rather than asking each frame, because the state changes a handful of times in a session.
