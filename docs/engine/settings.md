# Settings

The player's display choices, the screens that edit them, and the color ramps the game is drawn in.

| Piece | Where |
|---|---|
| The values and their save document | `src/state/settings.h` / `.cpp` |
| The settings screen and its confirm | `src/systems/settings_screen.h` / `.cpp` |
| The Display and Palette screens' logic | `src/systems/display_settings_screen.h`, `src/systems/palette_settings_screen.h` (and `.cpp`) |
| The Display and Palette screens' components | `src/render/display_settings/`, `src/render/palette_settings/`, and the shared `src/render/option_row.h`, `scroller_arrows.h`, `piece_shape.h` |
| The Controls screen | `src/systems/controls_screen.h` / `.cpp`, `src/state/controls_screen_state.h`, `src/render/controls/` |
| The color ramps | `src/render/palettes.h` |
| The page arrows of the screens drawn into the map | `src/render/settings_overlay.h` / `.cpp` |

## The values

```cpp
struct Settings {
    bool         fullscreen  = false;
    std::uint8_t windowScale = kDefaultWindowScale;  // 4
    std::uint8_t shadeRamp   = 0;                    // the greyscale ramp
    bool         ghostPiece  = false;                // the falling piece's landing shadow
    bool         newModes    = false;                // the game types the cartridge never had
    bool         fixAudio    = false;                // the title music returns after a demo
    bool         showStats   = false;                // the statistics are offered on the title screen
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

`"settings"`, **version 5**, seven bytes: the fullscreen flag as 0 or 1, the window scale, the ramp,
then the ghost-piece, new-modes, audio-fix and show-stats flags, each as 0 or 1.

Each earlier version was one byte shorter — version 1 stopped after the ramp, version 2 added the
ghost-piece flag, version 3 the new-modes flag, version 4 the audio-fix flag — and each step's
migration (`migrateSettingsV1ToV2` / `V2ToV3` / `V3ToV4` / `V4ToV5`) appends its flag as off.
`loadSettings` registers all four on the store before reading, so a document written at any released
version reaches the decoder at version 5's length.

`decodeSettings` accepts an image **shorter** than seven bytes and leaves every value the image does not
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
keeps every document it saves in the same store — the settings, the controls, the top scores, the
statistics and the achievements. Declaring one version therefore changes the terms every document in
that store is read under.

What keeps them apart is that each loader names its own version immediately before its own read —
`loadSettings` sets 5 and registers its chain, `loadControls` sets 1, `loadTopScores` sets 3 and
registers its own — so a document is never read under another document's version. Any new document
type in this store follows the same rule.

## The screens

The settings screen and its confirm are four game states, installed together:

```cpp
kirpich::systems::installSettingsHandlers(
    dispatcher, kirpich::systems::SettingsWiring{
                    .settings   = &settings,
                    .apply      = [&](const Settings& s) { /* put s into effect */ },
                    .save       = [&](const Settings& s) { kirpich::saveSettings(s, saves); },
                    .saveScores = [&](const HighScoreState& h) { kirpich::saveTopScores(h, saves); },
                    .saveStats  = [&](const StatsState& s) { kirpich::saveStats(s, saves); },
                    .saveAchievements =
                        [&](const AchievementState& a) { kirpich::saveAchievements(a, saves); },
                    .exit       = [&] { loop.exitRequest(); },
                });
```

The two screens the first page opens take the same wiring, one slot each:

```cpp
kirpich::systems::installDisplaySettingsScreen(dispatcher, settingsWiring);
kirpich::systems::installPaletteSettingsScreen(dispatcher, settingsWiring);
```

Every seam defaults to inert. `apply` and `save` fire on each change; `exit` fires when the confirm is
answered yes for quitting. The three save seams each fire for their own reset row, and for the
all-in row they all fire — a record and its documents go together, which is what lets one row clear
one kind and leave the others alone.

`openSettings(GameContext&)` is how the screen is entered — it records the current state as the one to
return to and enters `GameState::INIT_SETTINGS`. The title screen and the pause handler both call it.

### What the screen borrows

`initSettingsScreen` copies the background map the display is reading, the object buffer, and the
frame timer into `ScreenUiState` (`src/state/screen_ui_state.h`), and leaving puts all three back.
That is why returning to a paused round is exact — the paused screen is restored rather than rebuilt.

**Returning to the title screen is the exception: its objects are re-derived, not put back.** The
title's bottom row is a function of `Settings::showStats`, and that is one of the things this screen
changes — so the snapshot is the row the player arrived with. Leaving calls
`refreshTitleScreenObjects` (`src/systems/title_screens.h`) for the settings as they now stand.

Re-deriving on the way out rather than leaving it to the title screen's own per-frame redraw matters
because that redraw does not happen until the title's next tick, and frames are submitted before it.
Those frames would carry the old row — and an object the game writes into the buffer itself is named
for the entry it sits in (`src/render/sprites.cpp`), so the renderer matches the two rows and glides
one word into the other's place.

It paints on **whichever map is displayed**, so it never covers the map something else is writing:
at the title screen that is the first map, and in a paused round it is the second.

### Rows and pages

`SettingsRow` is the walk order, and **the declaration order is the layout**: a page holds
`kSettingsRowsPerPage` consecutive rows, so where a row sits follows from where it is declared.

```cpp
enum class SettingsRow : std::uint8_t {
    DISPLAY, PALETTE, CONTROLS, EXIT_GAME,                                // settings 1
    GHOST_PIECE, NEW_MODES, FIXES, STATS,                                 // enhancements 1
    RESET_SCORES, RESET_STATS, RESET_ACHIEVEMENTS, RESET_ALL              // enhancements 2
};
```

`kSettingsRowsPerPage` is 4 and `kSettingsPageCount` is derived from it and `kSettingsRowCount`, so
there is no split point to keep in step — `settingsPageOf` is a division and `settingsRowWithinPage`
the remainder. The header names each page for what it holds: `settings 1` for the screens that hold
the window's settings, the palette and the controls, then `enhancements 1` and `enhancements 2` for
the screens, switches and resets the cartridge never had, each family counting from one.

No row carries a value of its own. The first page's rows are `display`, `palette` and `controls`, which
open their screens, and `exit game`, which opens the confirm.

**To add a row:** add an enumerator in the position it should be walked and give it a label in
`labelFor`. Then handle it as exactly one kind:

- **A screen opener** — a case in the opener switch in `settingsScreen`, plus a right-only `reachOf`
  so the arrow points at the screen it leads to. Right, Confirm and Start all open it. A value the
  player edits lives on the screen the row opens, the way fullscreen and the window size live on the
  Display screen.
- **An action** — a row in `confirmFor`, which maps a row to the `ConfirmAction` it raises, and a
  question for that action in `confirmContentFor`. Nothing else: the confirm screen itself is general,
  and the branch that acts on `yes` is the only other place an action is named.

The page a row lands on, and the arrow that advertises the page beyond it, both follow from the
enumerator's position. Adding four rows adds a page without any constant changing.

A label runs from `kLabelCol` (3) to the left scroll arrow at `kOptionLeftArrowCol` (13), so **ten
cells is the most a label can be**. The labels are terse for that reason — the ghost row reads
`ghost`, and the Display screen's window-size row reads `size`.

**To change where a row sits**, edit `kSettingsFirstRow` and `kSettingsRowStride` in
`src/systems/settings_screen.h`. Every screen in the family reads that geometry, along with
`kLabelCol`, `kCursorCol`, `kScreenTitleRow` and the `kOption*` columns, so a row on one of them lines
up with a row on any other and with its own arrows.

### Text

Every string goes through `writeMapText` (`src/systems/screen.h`), which encodes through the character
map and writes nothing at all for text it cannot spell. The font is one case, digits, and about ten
punctuation glyphs — **no colon, slash, question mark or arrow**. Check any new string against
`include/kirpich/char_tile.h` before committing to it.

The Display and Palette screens draw their text as sprites through `Glyphs` (`src/render/glyphs.h`),
which spells only letters, digits, a period and a hyphen, and leaves a cell empty for anything else.

## The colour ramps

```cpp
struct ShadeRamp {
    retropp::Rgba8 darkest{}, dark{}, light{}, lightest{};
    bool mayBottomOutAtBlack = false;
};
inline constexpr std::array<ShadeRamp, 80> kShadeRamps{ /* ... */ };
```

Eighty of them: twenty-four built for this port, the eight colour schemes Windows 3.1 shipped in its
Control Panel, named as it named them, and three further sets of sixteen that keep a real colour in
their darkest shade.

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

`settingsPageArrows(ui, ramp, atlas)` returns the page arrow as a sprite: the game's own selector tile
given a quarter turn, which an object cannot express because the hardware has two flips and no
rotation. Append it to the composed sprites before they are wrapped as the frame's sprite layer:

```cpp
if (game.flow.gameState == kirpich::GameState::SETTINGS) {
    const auto arrows = kirpich::render::settingsPageArrows(game.screens, settings.shadeRamp, tiles);
    sprites.insert(sprites.end(), arrows.begin(), arrows.end());
}
```

The arrow on a row that opens a screen is the same selector tile, unturned, placed in the object
buffer by `drawValueArrows`. An arrow is emitted only where there is somewhere to go: none above the
first page or below the last, and on a row only when the row opens a screen.

## The Display and Palette screens

The first page's `display` and `palette` rows each open a screen of their own. Both are built from
components rather than written into the background map: the screen is a function of the settings and
of its own small state, called every frame, and returns its layers.

| Screen | Holds | Logic | Components |
|---|---|---|---|
| Display | fullscreen and the window size | `src/systems/display_settings_screen.h` | `src/render/display_settings/` |
| Palette | the palette selector, a swatch of its four colors, and the seven pieces drawn in it | `src/systems/palette_settings_screen.h` | `src/render/palette_settings/` |

### Opening and leaving

`openDisplaySettings(GameContext&)` puts the Display screen's cursor on its first row, shows the
cursor, arms the shared blink and enters `GameState::DISPLAY_SETTINGS`. `openPaletteSettings` does the
same for `GameState::PALETTE_SETTINGS`, which has one row and so no cursor position to reset. The
settings screen calls them from its rows, on Right, Confirm or Start, with the screen-change cue.

Neither screen writes the background map, touches the object buffer, or saves and restores the
caller's picture: the settings screen saved that when it opened, and still holds it. B on either
screen calls `returnToSettings`, which repaints the settings screen with its cursor on the row that
opened the screen.

There is no initializing state. A screen that paints nothing up front has nothing to do on the frame
it is entered, so opening it is a function call rather than a state of its own.

### Input

Each screen's logic is a dispatch table mapping an action to what it does, run after
`blinkScreenCursor`:

| Screen | MenuUp / MenuDown | MenuLeft / MenuRight | Back |
|---|---|---|---|
| Display | move between the two rows, with end stops | change the row's value | return |
| Palette | nothing | step the palette | return |

A move cues TINK; an end stop moves nothing and makes no sound. A value change goes through
`changeSettings(game, wiring, next)` (`src/systems/settings_screen.h`), which stores the settings,
cues TINK and fires `apply` then `save`. A change that lands on the value already held is an end stop:
nothing is written, nothing is cued, and neither seam fires. Right turns fullscreen on, steps the size
up and steps to the next palette; Left does the opposite. The size is clamped by `clampWindowScale`
and the palette by `render::clampShadeRamp`.

The Display screen's cursor position is `DisplaySettingsState` (`src/state/display_settings_state.h`),
a `GameContext` member of its own: it is the screen's state, not the player's and not the game's.

### The components

```cpp
Layers DisplaySettingsScreen(const DisplaySettingsState& ui, const Settings& s, bool blinkOn,
                             const TileAtlas& atlas);
Layers PaletteSettingsScreen(const Settings& s, bool blinkOn, const TileAtlas& atlas);
```

Each returns two layers: a backdrop of tiles at depth 0, and a sprite layer of everything else. The
Display screen's sprite layer holds the `display` heading, the `fullscreen` and `size` rows, and the
cursor. The Palette screen's holds the `palette` heading, the `palette` row, the cursor and the piece
preview, and carries the swatch as its `regions`.

They are built from shared components, each returning the primitives it is:

| Component | Returns |
|---|---|
| `OptionRow(label, value, line, left, right, atlas, ramp)` (`render/option_row.h`) | the label at `kLabelCol`, the value at `kOptionValueCol` and the row's `ScrollerArrows`, on map row `line` |
| `ScrollerArrows(line, left, right, atlas, ramp)` (`render/scroller_arrows.h`) | the selector tile at `kOptionLeftArrowCol`, flipped, when `left` is set, and at `kOptionRightArrowCol` when `right` is set |
| `PieceShape(kind, x, y, z, keyStem, atlas, ramp)` (`render/piece_shape.h`) | one piece at its spawn orientation, its top-left pixel at (x, y), each part named `keyStem` plus `-` and its index |
| `RampSwatch(ramp)` (`render/palette_settings/ramp_swatch.h`) | four opaque `ColorFill` squares, darkest to lightest, abutting, centered on the line under the palette row |
| `PiecePreview(atlas, ramp)` (`render/palette_settings/piece_preview.h`) | the seven shapes in two rows, L J I O over S Z T, each row centered from the shapes' own widths |

An arrow is drawn only where the value can still move that way: no left arrow when fullscreen is off
or at the smallest size or the first palette, no right arrow at the other ends. The palette number is
counted from one and starts on the value column, one or two digits.

`option_row.h` also converts the shared cell grid to pixels — `optionPixels`, `optionHeadingX`,
`kOptionHeadingY`, `kOptionCursorX` — and each screen's `layout.h` names its own positions from those,
so both screens sit on exactly the cells the settings screen writes into. `PieceShape` is what the
statistics pages draw their shapes with too.

The swatch is a set of regions rather than cells or sprites because the art has no solid-color tile,
and because a region is placed per pixel, which lets the four squares abut and read as one band.

### How the host draws them

The render callback picks the screen from the game state in one place, ahead of the screens drawn into
the map, and renders the layers it returns:

```cpp
switch (game.flow.gameState) {
    case kirpich::GameState::DISPLAY_SETTINGS: {
        retropp::FrameDrawState screen;
        screen.layers = kirpich::render::DisplaySettingsScreen(
            game.displaySettings, settings, game.screens.cursorVisible, tiles);
        renderer.renderFrame(screen);
        return;
    }
    case kirpich::GameState::PALETTE_SETTINGS: { /* PaletteSettingsScreen, the same way */ }
    default:
        break;
}
```

The ramp each screen draws through comes from `settings`, so the whole Palette screen recolors the
frame the palette steps.

**To add a screen like these:** mint its game state, write its logic as an `openX` function, a frame
handler with a dispatch table, and an installer; write its screen function from the shared components
with a `layout.h` derived from the shared grid; and add a case to the render callback's switch.

## The Controls screen

The first page's `controls` row opens the screen that rebinds the Game Boy's eight buttons. It is
built the way the Display and Palette screens are: logic with no drawing types, a screen function that
returns its layers, a case in the render callback's switch, and no initializing state. What it edits is
the player's `Controls` (`src/state/controls.h`), and every binding goes through `assignKey` and
`assignPad` (`src/systems/controls.h`). [`input.md`](input.md) covers those laws and the save document.

### Wiring

```cpp
kirpich::systems::installControlsScreen(
    dispatcher, settingsWiring,
    kirpich::systems::ControlsWiring{
        .controls = &controls,
        .listen   = [&] { platform.captureRequest(); },
        .captured = [&] { return platform.capturedSource(); },
        .apply    = [&](const Controls& c) { platform.actions(kirpich::systems::actionMapFor(c)); },
        .save     = [&](const Controls& c) { kirpich::saveControls(c, saves); },
    });
```

The settings wiring is only for the way back: B calls `returnToSettings` with it. `controls` is the
host's, like `settings`, because it outlives a reset and is saved. `listen` and `captured` are the
platform's press capture; `apply` hands the platform the action map the new bindings derive, and
`save` writes them out. `apply` then `save` fire once per binding and once per confirmed restore. Every seam
defaults to inert, and a wiring with no `controls` treats every captured press as a cancel and the
restore row as an end stop.

### Opening, walking and leaving

`openControlsSettings(GameContext&)` resets the screen's state — the cursor on the keyboard cell of
the Up row, not waiting — shows the cursor, arms the shared blink and enters
`GameState::CONTROLS_SETTINGS`. The settings screen calls it from its row on Right, Confirm or Start,
with the screen-change cue. Like the other two screens, it writes no background map and never saves or
restores the caller's picture.

| Action | Does |
|---|---|
| MenuUp / MenuDown | move between the eight button rows and the restore row under them, with end stops |
| MenuLeft / MenuRight | move between the keyboard and controller columns, with end stops; on the restore row, both are end stops |
| Confirm / Start | on a button row, wait for a press on the cell the cursor is on; on the restore row, ask whether to restore the defaults |
| Back | return to the settings screen on its Controls row |

A move cues TINK; an end stop moves nothing and makes no sound. Starting to wait makes no sound.

The cursor's row is a `ControlsRow` (`src/state/controls_screen_state.h`): the eight `GbButton`s in
order, then `RESTORE_DEFAULTS`. `rowOf(GbButton)` and `buttonOf(ControlsRow)` convert between them,
`buttonOf` answering nothing for the restore row. The column is kept while the cursor is on the restore
row, so going back up returns to the cell it left.

### Restoring the defaults

Confirm or Start on the restore row asks first, the way the settings screen's reset rows do. It sets
`ControlsScreenState::confirmingRestore`, puts the answer on "no" (`confirmYes` false), arms the blink
and cues the screen change. When the controls already are the defaults there is nothing to restore, so
the press is an end stop instead: no question, nothing cued.

While the question is up, the frame runs its own table:

| Action | Does |
|---|---|
| MenuLeft / MenuRight | move to "no" / "yes", with TINK; a press toward the answer already chosen is an edge |
| Confirm / Start | answer, and close the question with the screen-change cue |
| Back | close the question as "no" |

"Yes" sets the controls to `kDefaultControls`, then fires `apply`, then `save`, and sets
`awaitingRelease` (below). "No" changes nothing. Either way the cursor is back on the restore row.

The question is drawn on the cells every settings confirm uses — the title on the heading row, the
question on `kConfirmQuestionFirstRow` and `kConfirmQuestionSecondRow`, and the answers on
`kConfirmChoiceRow` at `confirmChoiceColumns(...)`, all in `src/systems/settings_screen.h` — so it reads
as those do. `RestoreConfirm(yes, blinkOn, atlas, ramp)` (`src/render/controls/restore_confirm.h`)
returns it as a sprite layer of its own, and `ControlsScreen` returns that layer in place of the
bindings' while it is asked.

### Waiting for a press

Confirm or Start sets `ControlsScreenState::listening` and calls `listen` once. The engine never
answers with a press already down when it was asked, so the button that started the wait is not bound
to the cell. While the screen waits, the frame reads `captured()` and does nothing else — no action of
the screen's own runs, so B cannot leave it and the arrows cannot move it. What it does with an answer:

| Captured | On the keyboard column | On the controller column |
|---|---|---|
| nothing yet | keep waiting | keep waiting |
| Escape (`kCancelKey`) | stop waiting, bindings untouched | the same |
| any other key | `assignKey`, then `apply`, `save`, TINK; stop waiting | ask again (`listen`) and keep waiting |
| a controller button | ask again and keep waiting | `assignPad` with the pad's family, then `apply`, `save`, TINK; stop waiting |
| a mouse button | ask again and keep waiting | the same |

Asking again matters because the engine keeps its answer until it is asked for a new one: without the
second `listen`, the screen would read the same unwanted press every frame.

### The release guard

Whenever the screen stops waiting, and whenever "yes" restores the defaults, it sets `awaitingRelease`,
and while that is set a frame does nothing but clear the flag once `GameContext::joypad.held` is empty.
The game's press edge is per action — an action held this frame and not the last is a press
(`src/systems/input.h`) — and a binding or a restore hands the platform a new action map between two
frames. A key the player is still holding therefore reads, on the next frame, as a fresh press of
whatever it now means: on the A row that is Confirm, which would start another wait on the same cell; a
key taken from the B row becomes Back, which would leave the screen; and a "yes" pressed with a key the
defaults give to B leaves the screen the same way. Waiting for the release keeps the key from
acting on the screen until the player lets go.

### The picture

```cpp
Layers ControlsScreen(const ControlsScreenState& ui, const Controls& controls,
                      retropp::ControllerType padFamily, bool blinkOn, const TileAtlas& atlas,
                      std::uint8_t ramp);
```

Two layers: the backdrop, and a sprite layer holding the `controls` heading, the `key` and `pad`
column heads, a row per button, the `restore defaults` row and the cursor. The button rows stand one
per line from the first option line, because the settings pages' three-line spacing cannot hold eight
rows; the label starts on column 1, the key's name on column 9 and the controller button's name on
column 15, each cursor in the cell before its name (`src/render/controls/layout.h`). The restore row
stands on line 14, a line below the buttons, laid out as a settings row is: label from column 3, cursor
on column 1. The cursor blinks while the screen is idle and is held while it waits. While it waits, the
waited-on cell reads `...`, line 16 reads `press a key` or `press a button`, and line 17 reads
`esc cancels`.

A name fits five cells and uses only what `Glyphs` draws:

- `keyName(SDL_Scancode)` (`src/render/controls/names.h`) — letters and digits as themselves, a
  short name for the common keys (`enter`, `bksp`, `space`, `lshft`, `lmeta`, `pgup`, `f1`, `kp0`,
  `kpent`, …), and `k` with the scancode in decimal for anything else, the comma, slash and bracket
  keys among them.
- `padButtonName(PadButton, ControllerType)` — a face position takes the letter printed there on
  `padFamily` (the east button is `a` on a Nintendo pad and `b` on an Xbox pad; PlayStation and pads
  without letters take the Xbox letters), a lettered face button is its letter, shoulders, triggers
  and stick clicks take the family's names (`lb` / `l1` / `l`, `lt` / `l2` / `zl`), and the rest
  have one name each (`start`, `selct`, `home`, `share`, `up`, `lslft`, …).

The host passes the family of the first connected pad, or `ControllerType::Standard` when none is
connected, reading `platform.connectedGamepads()` each frame:

```cpp
case kirpich::GameState::CONTROLS_SETTINGS: {
    const std::vector<retropp::GamepadInfo> pads = platform.connectedGamepads();
    const retropp::ControllerType padFamily =
        pads.empty() ? retropp::ControllerType::Standard : pads.front().family;
    retropp::FrameDrawState screen;
    screen.layers = kirpich::render::ControlsScreen(game.controlsScreen, controls, padFamily,
                                                    game.screens.cursorVisible, tiles,
                                                    settings.shadeRamp);
    renderer.renderFrame(screen);
    return;
}
```

**To name another key,** add it to the table in `names.cpp`; a name longer than five characters, or one
with a character the font cannot draw, fails `ControlsScreen.KeyNames`, which sweeps every scancode.

## The screens the opener rows lead to

The ghost and fixes rows open **carousel** instances (`src/systems/carousel_screen.h`): one option
to a screen — a title, an enable row in this screen's own scroller geometry, and a description —
with up and down moving between options, left and right toggling the shown one, and B returning
here. The new-modes row opens the mode screen (`src/systems/mode_screen.h`): one option, eleven rows
of prose, and an optional preview seam that draws into the map below them.

Both are machines, and neither owns a word of what it shows. The options, the flags their switches
toggle, and the two dispatch slots an instance answers to all arrive through their installers.

**The content is one unit.** `src/systems/enhancement_screens.h` holds what all three screens say,
which flag each option binds, and the install that puts them on the dispatcher:

```cpp
kirpich::systems::installEnhancementScreens(
    dispatcher, settings,
    [&] { /* apply and save, as a settings row would */ },
    settingsWiring);             // leaving any of them repaints the settings screen
```

The host passes only what is genuinely its own: the `Settings` the flags reach into, and the seam a
change fires. `settings` is held by reference in the installed handlers, so it must outlive the
dispatcher — the same lifetime the settings wiring's own pointer demands. The seam and the wiring are
copied.

**To add an option to an instance,** append a `CarouselOption` to its table in
`enhancement_screens.cpp` and give its flag a home on `Settings` (with the schema bump above), then
raise the instance's published count in the header. `kFixesOptionCount` and `kGhostOptionCount` are
static-asserted against their tables, so the count the render layer reads cannot drift from what the
table holds — a table that grows without its count fails to compile.

**To add an instance,** mint two `GameState` slots, add the table and its prose beside the others,
and install it in the same call. The shown-option index on `ScreenUiState` is shared, because only
one carousel is ever on screen.

An option's body is wrapped by hand to the twenty-cell screen, in the font's vocabulary (no comma, no
apostrophe); an empty line is a paragraph break.

**An option table has to outlive the handlers that read it.** `CarouselWiring::options` is a borrowed
span, and `installCarouselHandlers` copies the wiring into both slots it fills. A table cannot be
static either, since each option points into one particular `Settings`. So the unit allocates each
table on install and hands its owner to the installed handlers along with the wiring: the table lives
as long as a handler that can read it.

Option arrows are `carouselArrows(ui, ramp, atlas, optionCount)`, appended to the composed sprites
the way the page arrow is. The up arrow sits **above** the shown option's title — the title belongs
to the option, so an arrow inside what the option owns would read as the description scrolling — and
the down arrow below the description; with one option neither is drawn.

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
`ENTER_FULLSCREEN` / `LEAVE_FULLSCREEN` reports, adopts the value, and writes it out. It listens
rather than asking each frame, because the state changes a handful of times in a session.

The fullscreen shortcut (Alt+Enter or Cmd+Enter) sets the same setting. The Display screen
reads `settings` every frame, so its `fullscreen` row says what the player is looking at whichever way
it changed, with no code of its own for either.
