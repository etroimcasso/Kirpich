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
    bool         ghostPiece  = false;                // the falling piece's landing shadow
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

`"settings"`, **version 2**, four bytes: the fullscreen flag as 0 or 1, the window scale, the ramp,
the ghost-piece flag as 0 or 1.

Version 1 was three bytes, without the ghost-piece flag. `migrateSettingsV1ToV2` appends that flag as
off, and `loadSettings` registers it on the store before reading, so a version 1 document reaches the
decoder at version 2's length.

`decodeSettings` accepts an image **shorter** than four bytes and leaves every value the image does not
carry at its default, which keeps a truncated file costing one setting rather than all of them. It
refuses an empty image and one longer than this build writes.

**To add a setting:**

1. Append a field to `Settings` and its byte to `encodeSettings`.
2. Read it in `decodeSettings` behind an `image.size() > n` guard.
3. Raise `kSettingsImageBytes`.
4. **Raise `kSettingsSchemaVersion` and register a migration** from the previous version that appends
   the new byte at its default.

Step 4 is not optional, and the short-image path is not a substitute for it. A build that writes a
different number of bytes under an unchanged version number leaves two formats answering to one
version, which is the situation a schema version exists to prevent; the short-image path is what
keeps a *damaged* file cheap, not what carries a format change.

### One version and one migration chain per store, not per document

`SaveStore::setCurrentVersion` and `registerMigration` are properties of the **store**, and this port
keeps the settings and the top scores in the same store. Declaring version 2 therefore changes the
terms every document in that store is read under.

What keeps them apart is that each loader names its own version immediately before its own read —
`loadSettings` sets 2 and registers its step, `loadTopScores` sets 1 — so a document is never read
under another document's version. Any new document type in this store follows the same rule.

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
    FULLSCREEN, WINDOW_SCALE, SHADE_RAMP, EXIT_GAME,   // page 1
    GHOST_PIECE, RESET_SCORES                          // page 2
};
```

`kSettingsFirstPageRows` is 4, so the first four enumerators draw on page 1 and the rest on page 2.
`paintSettingsValues` takes the page and paints only that page's values.

**To add a row:** add an enumerator in the position it should be walked, give it a label in
`labelFor`, and handle it in `changeValue` (a value) or in the Confirm/Start branch of
`settingsScreen` (an action). A value row also needs an entry in `reachOf`, which is what decides
whether it draws a scroll arrow on each side, and a line in `paintSettingsValues` under its page.
Raising `kSettingsFirstPageRows` moves the page boundary; the page a row lands on and the arrow that
advertises the other page both follow from it.

A label runs from `kLabelCol` (3) to the left scroll arrow at `kOptionLeftArrowCol` (13), so **ten
cells is the most a label can be**. The existing labels are terse for that reason — the window-size
row reads `size`, the ghost row reads `ghost`.

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
struct ShadeRamp {
    retropp::Rgba8 darkest{}, dark{}, light{}, lightest{};
    bool mayBottomOutAtBlack = false;
};
inline constexpr std::array<ShadeRamp, 48> kShadeRamps{ /* ... */ };
```

Forty-eight of them: twenty-four built for this port, the eight colour schemes Windows 3.1 shipped in
its Control Panel, named as it named them, and sixteen that keep a real colour in their darkest shade.

`mayBottomOutAtBlack` says whether a ramp is meant to go black at the bottom, and the default is that
it is not. The darkest shade is what the playing field's walls, the panel's rules and every locked
block are drawn in, so it is most of what a player looks at — and a ramp whose darkest shade is within
a rounding error of black looks like every other such ramp whatever its other three shades do. Most of
these ramps are designed that way deliberately and say so; a ramp that says nothing is held to keeping
a colour there, measured by both chroma and luminance, since either alone passes for the wrong reason:
chroma alone accepts a bright colour, luminance alone accepts a dark grey.

Adding a ramp therefore means choosing which kind it is. Say nothing and the check applies.

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

**A ramp must run dark to light** by Rec.601 luminance, strictly increasing across the four shades.
The art stores a sample per pixel and the ramp says what that sample is worth, so a ramp out of order
draws the game inverted or muddy. `ShadeRamps.EveryRampRunsDarkToLight` sweeps every ramp the build
offers, so a new one authored out of order fails there rather than on screen.

## The drawn parts

`settingsOverlay(ui, ramp, viewportWidth)` returns the regions for the palette preview strip. The host
appends them to the frame's regions while the settings screen is showing:

```cpp
if (game.flow.gameState == kirpich::GameState::SETTINGS) {
    const auto overlay = kirpich::render::settingsOverlay(game.screens, settings.shadeRamp,
                                                          kViewport.width);
    frame.regions.insert(frame.regions.end(), overlay.begin(), overlay.end());
}
```

Appended rather than assigned, because the frame's regions are shared — the ghost piece
(`rendering.md`) puts its own on the background layer, but anything else reaching for `frame.regions`
would otherwise overwrite this.

`settingsPageArrows(ui, ramp, atlas)` returns the page arrow separately, as a sprite: it is the game's
own selector tile given a quarter turn, which an object cannot express because the hardware has two
flips and no rotation. Append it to the composed sprites before they are wrapped as the frame's sprite
layer.

A preview square is a four-point polygon carrying one `ColorFill`. It is a region rather than a cell
because the art has no solid-colour tile, and because a region is placed per pixel — which is what
lets the four squares abut exactly and read as one band of colour rather than as four blocks.

The scroll arrows either side of a value are neither: they are the game's own selector tile placed in
the object buffer by `drawValueArrows`, flipped for the left one. So the screen draws in the game's
own hand wherever the art has something to draw with, and reaches for a region only for colour, which
it has nothing for.

An arrow is emitted only where there is somewhere to go: none to the left of the first ramp, none to
the right of the last, none above the first page or below the last. A row that is an action rather
than a choice has neither.

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
