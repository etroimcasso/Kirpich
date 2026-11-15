# Settings

The player's display choices, the screens that edit them, and the color ramps the game is drawn in.

| Piece | Where |
|---|---|
| The values and their save document | `src/state/settings.h` / `.cpp` |
| The player's custom palettes and their document | `src/state/custom_palettes.h` / `.cpp` |
| The player's custom rotations and their document | `src/state/custom_rotations.h` / `.cpp` |
| The shipped rotation presets | `src/render/rotation_presets.h` |
| The palette a frame draws through, and the rotation set's reconcile | `src/systems/frame_palette.h` / `.cpp` |
| The palette walk the Palette screen steps | `src/systems/palette_walk.h` / `.cpp` |
| The rotation walk the rotation screen steps | `src/systems/rotation_walk.h` / `.cpp` |
| The settings screen and its confirm | `src/systems/settings_screen.h` / `.cpp` |
| The Display and Palette screens' logic | `src/systems/display_settings_screen.h`, `src/systems/palette_settings_screen.h` (and `.cpp`) |
| The Display and Palette screens' components | `src/render/screens/display_settings/`, `src/render/screens/palette_settings/`, and the shared `src/render/components/option_row.h`, `scroller_arrows.h`, `piece_shape.h` |
| The Controls screen | `src/systems/controls_screen.h` / `.cpp`, `src/state/controls_screen_state.h`, `src/render/screens/controls/` |
| The palette editor | `src/render/screens/palette_editor/` — its state, its logic, its layout and its screen function |
| The rotation screen | `src/render/screens/palette_rotation/` — its state, its logic, its layout, its grid components and its screen function |
| The rotation editor | `src/render/screens/rotation_editor/` — its state, its logic, its layout, its grid and backdrop components and its screen function |
| The text input screen the rotation editor raises | `src/render/components/text_entry.h` / `.cpp` |
| The held palette preview all three screens draw | `src/render/components/palette_preview.h`, `preview_backdrop.h` (and `.cpp`) |
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
    bool         customPalette = false;              // the player's custom palette is in use
    PaletteRotation paletteRotation = PaletteRotation::OFF;  // a round's palette follows its level
    bool         nameDigits  = false;                // the top-score name wheel carries 0 to 9
};

std::uint8_t paletteIndexOf(const Settings&);

enum class PaletteRotation : std::uint8_t { OFF = 0, HOTTER = 1, COLDER = 2, RAINBOW = 3, CUSTOM = 4 };
inline constexpr std::size_t kPaletteRotationCount =
    static_cast<std::size_t>(PaletteRotation::CUSTOM) + 1;   // 5
constexpr PaletteRotation paletteRotationOf(std::uint8_t byte) noexcept;
```

`paletteRotation` says whether a round is drawn through its level's palette and through which ten:
one of the shipped presets, or `CUSTOM` for the player's own working rotation
([below](#the-custom-rotations)). It reaches only the round's own states; every menu and port screen
keeps drawing through `paletteIndexOf`, and `shadeRamp` and `customPalette` keep their values
underneath. `paletteRotationOf` turns a stored byte into a mode: 0 to 4 name the five, and any other
value reads as `OFF`, so a file written by a build that offered more presets costs the player that
one value.

`nameDigits` puts the digits `0` to `9` on the top-score name entry's letter wheel, after its symbols
and ahead of the space ([high-score-state.md](high-score-state.md)). It is off by default, so the wheel is the
cartridge's until the player turns it on.

`Settings` is not game state and is not on `GameContext`: it outlives a round and a reset, and it is
saved to disk. The host owns one and hands the screen a pointer to it.

`paletteIndexOf` is the one derivation of which palette the frame draws through:
`render::kCustomRamp` while `customPalette` is set, the clamped `shadeRamp` otherwise. `shadeRamp`
keeps its value underneath, so a player stepping back off the custom palette lands on the ramp they
left. Every frame read in the host goes through it rather than through `shadeRamp` directly.

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

`"settings"`, **version 8**, ten bytes: the fullscreen flag as 0 or 1, the window scale, the ramp,
then the ghost-piece, new-modes, audio-fix, show-stats and custom-palette flags, each as 0 or 1, then
the palette-rotation mode as its `PaletteRotation` value, read back through `paletteRotationOf`, then
the name-digits flag as 0 or 1.

Each earlier version was one byte shorter — version 1 stopped after the ramp, version 2 added the
ghost-piece flag, version 3 the new-modes flag, version 4 the audio-fix flag, version 5 the show-stats
flag, version 6 the custom-palette flag, version 7 the palette-rotation mode — and each step's
migration (`migrateSettingsV1ToV2` / `V2ToV3` / `V3ToV4` / `V4ToV5` / `V5ToV6` / `V6ToV7` /
`V7ToV8`) appends its byte as 0, which is off for a flag and `OFF` for the mode. `loadSettings`
registers all seven on the store before reading, so a document written at any earlier version reaches
the decoder at version 8's length.

`decodeSettings` accepts an image **shorter** than ten bytes and leaves every value the image does not
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
keeps every document it saves in the same store — the settings, the controls, the custom palettes and
rotations, the top scores, the statistics and the achievements. Declaring one version therefore
changes the terms every document in that store is read under.

What keeps them apart is that each loader names its own version immediately before its own read —
`loadSettings` sets 8 and registers its chain, `loadControls` sets 1, `loadCustomPalettes` sets 1,
`loadCustomRotations` sets 1, `loadTopScores` sets 3 and registers its own — so a document is never read under another document's
version. Any new document type in this store follows the same rule.

## The custom palettes

The player's own palettes: ninety-nine slots a palette can be saved into, and the **working palette**,
the one being edited — which is also the one the game draws through while `Settings::customPalette`
is set.

```cpp
inline constexpr std::size_t kCustomPaletteSlotCount = 99;

struct CustomPaletteSlot {
    bool              present = false;                       // something has been saved here
    render::ShadeRamp shades  = render::freshCustomPalette();
};

struct CustomPalettes {
    render::ShadeRamp                                      working = render::freshCustomPalette();
    std::array<CustomPaletteSlot, kCustomPaletteSlotCount> slots{};
};
```

A custom palette is a `render::ShadeRamp` whose `mayBottomOutAtBlack` is always off — that flag is the
built-in table's note about how a ramp was authored, so a slot compares equal to the working palette
color for color. `render::freshCustomPalette()` is what an empty slot holds and what the working
palette starts as: the default grayscale's four colors.

A slot is **empty** until a palette is saved into it, and empty again once cleared. An empty slot is
not offered on the Palette screen's selector. Slot numbers are one-based at every surface, as they are
on screen: `C01` is `slots[0]`.

Like the settings and the controls, the palettes are the host's, outside `GameContext`, loaded before
the window opens and untouched by a reset.

One value of a palette is read and written by its shade and channel:

```cpp
enum class ColorChannel : std::uint8_t { RED, GREEN, BLUE };
inline constexpr std::size_t kShadeCount = 4;

std::uint8_t channelValue(const render::ShadeRamp&, std::size_t shade, ColorChannel) noexcept;
void setChannelValue(render::ShadeRamp&, std::size_t shade, ColorChannel, std::uint8_t value) noexcept;
```

`shade` counts 0-3 from the darkest (`darkest`, `dark`, `light`, `lightest`); a larger value reads and
writes the lightest. A value is one 8-bit number; nothing stores its digits.

### Saved is a comparison

```cpp
std::optional<std::uint8_t> savedSlotOf(const CustomPalettes&);
std::optional<std::uint8_t> nextPresentSlot(const CustomPalettes&, std::uint8_t after);
std::optional<std::uint8_t> previousPresentSlot(const CustomPalettes&, std::uint8_t before);
```

Nothing records which slot the working palette "came from". `savedSlotOf` returns the first present
slot holding exactly the working palette's colors, or nothing — and nothing is what **unsaved** means.
The Palette screen's `C<nn>` / `C` reads it. `hasUnsavedChanges(const CustomPalettes&)` is the
narrower question the palette editor asks — saved in no slot **and** not the fresh grayscale — because
a palette never edited has nothing to lose: its asterisk and its load question both read it. The two
walk helpers give the present slots in order: `nextPresentSlot(p, 0)` is the first of all,
`previousPresentSlot(p, kCustomPaletteSlotCount + 1)` the last of all.

### The palettes document

`"palettes"`, **version 1**, 1300 bytes: one hundred records of thirteen — a presence byte, then red,
green and blue for the darkest, dark, light and lightest shades. Record 0 is the working palette (its
presence byte written as 1 and not read); records 1 to 99 are the slots. Alpha is not stored, and a
decoded color's alpha is 255.

```cpp
std::array<std::uint8_t, kCustomPalettesImageBytes> encodeCustomPalettes(const CustomPalettes&);
bool decodeCustomPalettes(std::span<const std::uint8_t> image, CustomPalettes&);
bool saveCustomPalettes(const CustomPalettes&, retropp::SaveStore&);
bool loadCustomPalettes(retropp::SaveStore&, CustomPalettes&);
```

`decodeCustomPalettes` refuses anything but exactly 1300 bytes and leaves the value untouched; any
non-zero presence byte means present. `loadCustomPalettes` follows `loadControls`: it sets the store's
version to 1 immediately before its read, returns `false` for an absent document (leaving the
defaults), a corrupt one (logged, defaults, file left in place) and a wrong length (logged, defaults).

### The wiring

```cpp
struct CustomPaletteWiring {
    CustomPalettes*                               palettes = nullptr;
    std::function<void(const render::ShadeRamp&)> apply;   // the atlas's custom set takes these colors
    std::function<void(const CustomPalettes&)>    save;    // the document is written
};
```

Every seam is inert by default, and with no `palettes` behind it the Palette screen offers no custom
slot. `apply` and `save` fire, in that order, whenever the working palette changes; the host's `apply`
is `render::updateCustomPalette` (below, under the color ramps).

## The custom rotations

The player's own palette rotations, shaped as the custom palettes are: ninety-nine slots a rotation
can be saved into, and the **working rotation**, the one being edited — which is also the one a round
is drawn through while `Settings::paletteRotation` is `CUSTOM`. A rotation is ten palettes, one for
each level a round can show modulo ten, level 0 first.

```cpp
inline constexpr std::size_t kRotationLevelCount      = 10;
inline constexpr std::size_t kCustomRotationSlotCount = 99;
inline constexpr std::size_t kRotationNameLength      = 7;

constexpr std::array<render::ShadeRamp, kRotationLevelCount> freshRotationLevels() noexcept;

using RotationName = std::array<char, kRotationNameLength>;
constexpr RotationName blankRotationName() noexcept;   // seven spaces
std::string rotationNameText(const RotationName&);     // trailing spaces dropped

struct RotationProgression {
    std::array<render::ShadeRamp, kRotationLevelCount> levels = freshRotationLevels();
};

struct CustomRotationSlot {
    bool                present = false;                     // something has been saved here
    RotationName        name    = blankRotationName();       // what it was saved under
    RotationProgression progression{};
};

struct CustomRotations {
    RotationProgression                                      working{};
    std::array<CustomRotationSlot, kCustomRotationSlotCount> slots{};
};
```

`freshRotationLevels()` is ten `render::freshCustomPalette()`s, so a fresh rotation — what the working
rotation starts as and what an empty slot holds — draws exactly as no rotation at all. Every palette
of a rotation is a `ShadeRamp` whose `mayBottomOutAtBlack` is off, as a custom palette's is, so a slot
compares equal to the working rotation color for color. All three types have a defaulted `==`.

A slot is **empty** until a rotation is saved into it, and empty again once cleared, name and all. A
slot's `name` is the one the rotation was saved under: seven characters, each a lowercase letter, a
digit, a period, a hyphen or a space, padded on the right with spaces. `rotationNameText` is the name
as a screen reads it, its trailing spaces dropped, so a blank name reads as nothing and a slot with no
name reads as its number. The working rotation has no name. Slot numbers are one-based at every
surface: slot 1 is `slots[0]`. The rotations are the host's, outside `GameContext`,
loaded before the window opens and untouched by a reset. The shipped presets are not stored here
([below](#the-presets)).

### Saved is a comparison

```cpp
std::optional<std::uint8_t> savedSlotOf(const CustomRotations&) noexcept;
bool hasUnsavedChanges(const CustomRotations&) noexcept;
std::optional<std::uint8_t> nextPresentSlot(const CustomRotations&, std::uint8_t after) noexcept;
std::optional<std::uint8_t> previousPresentSlot(const CustomRotations&, std::uint8_t before) noexcept;
```

These are the custom palettes' four, over rotations — overloads, told apart by the type. Nothing
records which slot the working rotation came from: `savedSlotOf` returns the first present slot holding
exactly its colors, level for level, or nothing, which is what **unsaved** means. A slot's name takes no
part in the comparison.
`hasUnsavedChanges` is saved in no slot **and** not the fresh rotation, because a rotation never edited
has nothing to lose. `nextPresentSlot(r, 0)` is the first present slot of all and
`previousPresentSlot(r, kCustomRotationSlotCount + 1)` the last.

### The rotations document

`"rotations"`, **version 1**, 12,800 bytes: one hundred records of 128 — a presence byte, the seven
bytes of the name, then level 0 to 9, each red, green and blue for the darkest, dark, light and
lightest shades, twelve bytes a level (level `j` at offset `8 + 12j` within the record). Record 0 is
the working rotation: its presence byte is written as 1 and not read, and its name is written as seven
spaces and not read. Records 1 to 99 are the slots. A name byte is its character; on the way in an
uppercase letter reads as its lowercase and any other byte outside the name's characters reads as a
space. Alpha is not stored, and a decoded color's alpha is 255.

```cpp
std::array<std::uint8_t, kCustomRotationsImageBytes> encodeCustomRotations(const CustomRotations&);
bool decodeCustomRotations(std::span<const std::uint8_t> image, CustomRotations&);
bool saveCustomRotations(const CustomRotations&, retropp::SaveStore&);
bool loadCustomRotations(retropp::SaveStore&, CustomRotations&);
```

`decodeCustomRotations` refuses anything but exactly 12,800 bytes and leaves the value untouched; any
non-zero presence byte means present. `loadCustomRotations` sets the store's version to 1 immediately
before its read and returns `false` for an absent document (leaving the defaults), a corrupt one
(logged, defaults, file left in place) and a wrong length (logged, defaults). The host loads it beside
the custom palettes:

```cpp
kirpich::CustomRotations rotations;
kirpich::loadCustomRotations(saves, rotations);
```

## The presets

```cpp
using RotationPalettes = std::array<ShadeRamp, kRotationLevelCount>;
inline constexpr RotationPalettes kHotterRotation{ /* ... */ };
inline constexpr RotationPalettes kColderRotation{ /* ... */ };
inline constexpr RotationPalettes kRainbowRotation{ /* ... */ };

constexpr const RotationPalettes* presetRotation(PaletteRotation mode) noexcept;

inline constexpr int kRotationPresetNameMaxGlyphs = 7;
constexpr std::string_view presetRotationName(PaletteRotation mode) noexcept;
```

`src/render/rotation_presets.h` holds the rotations the game ships, ten palettes each, level 0 first:
hotter runs from a warm cream to an intense red, colder from a cool slate to a deep blue, and rainbow
steps the hue 36° a level from red. A preset's colors are its own, not picks from the ramp table.
`presetRotation` returns the table a mode names, and null for `OFF` and `CUSTOM`.
`presetRotationName` is the name a preset reads as on both selectors — `hotter`, `colder`,
`rainbow` — and nothing for `OFF` and `CUSTOM`; a `static_assert` holds every name to
`kRotationPresetNameMaxGlyphs`, the room a selector leaves for one.

Every entry runs dark to light by Rec.601 luminance, as every ramp does, with its lightest shade at
least 100 above its darkest, and no two consecutive entries of a preset are equal. Hotter's darkest
red rises and its dark green falls level over level; colder's darkest blue rises and its dark red
falls; rainbow's ten dark shades are all different. `tests/test_rotation_presets.cpp` holds every
preset to all of that.

**To add a preset:** add its table here, an enumerator to `PaletteRotation` before `CUSTOM`, a line
to `presetRotation`, and its name to `presetRotationName`.

## The frame's palette

```cpp
bool roundPaletteShown(GameState state) noexcept;
constexpr std::size_t rotationLevelOf(std::uint8_t level) noexcept;   // level % 10

const render::RotationPalettes* rotationPalettes(const Settings&,
                                                 const RotationProgression&) noexcept;
std::optional<render::ShadeRamp> rotationPaletteFor(const Settings&, const RotationProgression&,
                                                    const GameContext&) noexcept;
std::uint8_t framePaletteIndex(const Settings&, const RotationProgression&,
                               const GameContext&) noexcept;
void reconcileRotationPalette(const Settings&, const RotationProgression&, const GameContext&,
                              const render::ShadeRamp& current,
                              const std::function<void(const render::ShadeRamp&)>& write);
```

`src/systems/frame_palette.h` says which palette a frame is drawn through. `roundPaletteShown` is true
for exactly the round's own states, from its init until the game returns to a menu: `INIT_GAME`,
`NORMAL_GAMEPLAY` (paused included), the game-over states (`INIT_GAME_OVER`, `GAME_OVER_CURTAIN`,
`GAME_OVER_SCREEN`, `STATE_0C_UNKNOWN`), the Type B ending (`TYPE_B_VICTORY_JINGLE`,
`INIT_TYPE_B_SCOREBOARD`, `INIT_TYPE_B_BONUS`, `DANCERS`), the Buran scene (`INIT_BURAN`,
`PREPARE_BURAN_LAUNCH`, `BURAN_IGNITION`, `BURAN_IGNITION_2`, `BURAN_LIFTOFF`, `BURAN_RISING`,
`PRINT_CONGRATULATIONS`, `CONGRATULATIONS`) and the rocket scene (`GAME_OVER_TO_BONUS`,
`INIT_ROCKET_LAUNCH`, `ROCKET`, `ROCKET_IGNITION`, `ROCKET_LIFTOFF`, `ROCKET_MAIN_ENGINE_FIRE`,
`END_OF_BONUS_SCENE`) — twenty-five states, listed by name in the `.cpp`. Every other state is out,
the top-score entry and every port screen among them. The attract demo runs these same states, so it
is drawn the same way.

`rotationPalettes` is the mode's ten palettes, level 0 first: the preset's table (`presetRotation`),
the `RotationProgression`'s own `levels` for `CUSTOM`, and null for `OFF`. The pointer refers to the
preset table or into the progression passed in.

`rotationPaletteFor` first yields, whatever the mode:

- in the rotation editor with its held preview up (`gameState == ROTATION_EDITOR` and
  `rotationEditor.previewing`), the `RotationProgression`'s palette at `game.rotationEditor.level` —
  `OFF` and the presets included, since the editor edits the working rotation whichever one is on
  ([below](#the-rotation-editor)).

Otherwise it yields nothing while the mode is `OFF`, and else one of the mode's ten:

- on the rotation screen with its held preview up (`gameState == PALETTE_ROTATION` and
  `rotationScreen.previewing`), the palette at `game.rotationScreen.previewLevel`
  ([below](#the-rotation-screen));
- in a round's state, the palette at `rotationLevelOf(game.flow.level)`. The level the round shows
  picks: a Type B round wears its starting level's palette all round, and heart mode, which shifts the
  gravity, does not shift the palette;
- in any other state, nothing.

The two held previews are the only places outside a round the rotation set is written for. It reads
`game.flow.gameState`, `game.flow.level`, `game.rotationScreen` and `game.rotationEditor` and nothing
else of the context. The host passes the player's working rotation as the `RotationProgression`.

`framePaletteIndex` is `render::kRotationRamp` while `rotationPaletteFor` yields a palette, and
`paletteIndexOf(settings)` otherwise. The render callback reads it once per frame:

```cpp
const std::uint8_t paletteIndex =
    kirpich::systems::framePaletteIndex(settings, rotations.working, game);
```

The Display, Palette, palette editor, rotation screen and rotation editor call
`paletteIndexOf(settings)` themselves: they are menus and show the player's own palette. The two held
previews are the exception: the rotation screen's draws through `kRotationRamp` while a rotation is
on, and the rotation editor's always does.

### Keeping the rotation set current

The atlas's rotation set ([below](#the-custom-set)) holds the colors the frame reads at
`kRotationRamp`. `reconcileRotationPalette` keeps it equal to the palette a round wants: when
`rotationPaletteFor` yields a palette that differs from `current`, `write` is called once with it, and
otherwise nothing happens. The set keeps its colors while no palette is wanted, so returning to a round
at the same level writes nothing. The host calls it once per simulation tick, after the frame's last
beat, so a level-up during the field wipe is drawn on the tick it happens:

```cpp
kirpich::systems::reconcileRotationPalette(
    settings, rotations.working, game, tiles.rotationShades,
    [&renderer, &tiles](const kirpich::render::ShadeRamp& shades) {
        kirpich::render::updateRotationPalette(renderer, tiles, shades);
    });
```

No gameplay handler knows about palettes: the round init and the level-up change the level, and the
next reconcile sees it. No screen writes the set either: a step of the rotation screen's preview level
changes `previewLevel`, and a step of the rotation editor's changes `rotationEditor.level`; the
reconcile on the same tick rewrites the set, so the preview's next frame draws through the new level.

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

The two screens the first page opens take the same wiring, one slot each; the Palette screen takes the
custom palettes' wiring beside it, because stepping onto a saved slot changes the atlas's custom set
and writes the palettes document:

```cpp
kirpich::systems::installDisplaySettingsScreen(dispatcher, settingsWiring);
kirpich::systems::installPaletteSettingsScreen(
    dispatcher, settingsWiring,
    kirpich::systems::CustomPaletteWiring{
        .palettes = &palettes,
        .apply    = [&](const kirpich::render::ShadeRamp& s) {
            kirpich::render::updateCustomPalette(renderer, tiles, s);
        },
        .save     = [&](const kirpich::CustomPalettes& p) { kirpich::saveCustomPalettes(p, saves); },
    });
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

`SettingsRow` is the walk order, and **the declaration order is the layout**: the rows are drawn in
the order they are declared, and a page begins at the row `kSettingsFirstRowOfPage` names for it.

```cpp
enum class SettingsRow : std::uint8_t {
    DISPLAY, PALETTE, CONTROLS, EXIT_GAME,                                // settings 1
    GHOST_PIECE, NEW_MODES, FIXES, STATS,                                 // enhancements 1
    NAME_DIGITS,                                                          // enhancements 2
    RESET_SCORES, RESET_STATS, RESET_ACHIEVEMENTS, RESET_ALL              // enhancements 3
};

inline constexpr std::array kSettingsFirstRowOfPage{SettingsRow::DISPLAY, SettingsRow::GHOST_PIECE,
                                                    SettingsRow::NAME_DIGITS,
                                                    SettingsRow::RESET_SCORES};
inline constexpr std::uint8_t kSettingsPageCount   = kSettingsFirstRowOfPage.size();   // 4
inline constexpr std::uint8_t kSettingsRowsPerPage = 4;

constexpr std::uint8_t settingsPageOf(SettingsRow row) noexcept;
constexpr std::uint8_t settingsRowWithinPage(SettingsRow row) noexcept;
```

A page holds every row from its first row up to the next page's. `settingsPageOf` is the last page
whose first row is at or before the row, and `settingsRowWithinPage` the row's place counted from that
first row. `kSettingsRowsPerPage` is the most a page holds — every page lays its rows on the same four
lines — and a `static_assert` holds the table to it: the pages begin at `DISPLAY`, ascend, and none
holds more than four. The header names each page for what it holds: `settings 1` for the screens that
hold the window's settings, the palette and the controls, then `enhancements 1`, `enhancements 2` and
`enhancements 3` for the screens, switches and resets the cartridge never had, each family counting
from one. `enhancements 2` holds the `name digits` row alone.

No row carries a value of its own. The first page's rows are `display`, `palette` and `controls`, which
open their screens, and `exit game`, which opens the confirm.

**To add a row:** add an enumerator in the position it should be walked and give it a label in
`labelFor`. If it starts a page, or a page would hold more than four rows, add or move an entry in
`kSettingsFirstRowOfPage` and a title in `pageTitle`. Then handle it as exactly one kind:

- **A screen opener** — a case in the opener switch in `settingsScreen`, plus a right-only `reachOf`
  so the arrow points at the screen it leads to. Right, Confirm and Start all open it. A value the
  player edits lives on the screen the row opens, the way fullscreen and the window size live on the
  Display screen.
- **An action** — a row in `confirmFor`, which maps a row to the `ConfirmAction` it raises, and a
  question for that action in `confirmContentFor`. Nothing else: the confirm screen itself is general,
  and the branch that acts on `yes` is the only other place an action is named.

The page a row lands on, and the arrow that advertises the page beyond it, both follow from the
enumerator's position and the page table.

A label starts at `kLabelCol` (3). On a row that opens a screen it runs to the right arrow at
`kOptionRightArrowCol` (19), so **sixteen cells is the most an opener's label can be**; an action row
has no arrow and runs to the screen's edge, seventeen cells. The name-digits row reads `name digits`
because `name entry digits` is seventeen, and the achievements reset reads `reset achvmnts` because
`reset achievements` is eighteen.

**To change where a row sits**, edit `kSettingsFirstRow` and `kSettingsRowStride` in
`src/systems/settings_screen.h`. Every screen in the family reads that geometry, along with
`kLabelCol`, `kCursorCol`, `kScreenTitleRow` and the `kOption*` columns, so a row on one of them lines
up with a row on any other and with its own arrows.

### Text

Every string goes through `writeMapText` (`src/systems/screen.h`), which encodes through the character
map and writes nothing at all for text it cannot spell. The font is one case, digits, and about ten
punctuation glyphs — **no colon, slash, question mark or arrow**. Check any new string against
`include/kirpich/char_tile.h` before committing to it.

The Display and Palette screens, the editors and the rotation screen draw their text as sprites
through `Glyphs` (`src/render/components/glyphs.h`), which spells letters, digits, a period and a
hyphen from the font, an asterisk and a colon from the port's extras sheet, and leaves a cell empty for
anything else.

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

Darkest first, matching the order the extractor's decode produces. `uploadTileAtlas` builds nine
palettes per ramp — the background font and content, the dim font, the two object palettes, the font
as an object, the two dim object palettes, and the selected font object palette, inked in the dark
shade (`fontSpriteSelected`); `rampPaletteColors(const ShadeRamp&)` is the one
builder that arranges them — and uploads them all at startup, so choosing a ramp picks between
handles and uploads nothing.

`resolveTile(index, sheet, atlas, ramp)` and `resolveSpriteTile(index, sheet, palette1, atlas, ramp)`
take the ramp; `composeBackground` and `composeSprites` pass it through. A ramp decides which colours
a sample resolves to and never which art a tile index names.

### The custom set

```cpp
inline constexpr std::uint8_t kCustomRamp         = kShadeRampCount;      // 80
inline constexpr std::uint8_t kRotationRamp       = kShadeRampCount + 1;  // 81
inline constexpr std::size_t  kPaletteChoiceCount = kShadeRampCount + 2;
constexpr ShadeRamp           freshCustomPalette() noexcept;

const RampPalettes&           palettesFor(const TileAtlas&, std::uint8_t index) noexcept;
std::array<retropp::Rgba8, 4> shadesOf(const TileAtlas&, std::uint8_t index) noexcept;
void updateCustomPalette(retropp::Renderer&, TileAtlas&, const ShadeRamp& shades);
void updateRotationPalette(retropp::Renderer&, TileAtlas&, const ShadeRamp& shades);
```

The two palette indices past the table are live sets. `kCustomRamp` is the player's custom palette:
`TileAtlas` carries it as one more `RampPalettes`, `custom`, uploaded at startup from
`freshCustomPalette()` and arranged exactly as a built-in's is, with `customShades` recording what it
currently draws. `kRotationRamp` is the palette a round is drawn through while its palette follows the
level: a second set, `rotation`, uploaded the same way, with `rotationShades` beside it, which only the
host's reconcile writes ([above](#keeping-the-rotation-set-current)). Every component that takes a
`ramp` accepts both; `palettesFor` returns the live set for either and the clamped built-in for any
other index, so an index past `kRotationRamp` still clamps to the last ramp.

`updateCustomPalette` gives the set new colors: nine `Renderer::updatePalette` calls under the nine
handles, which keep their ids and draw the new colors from the next frame on, then `customShades`
updated. One call per change is the whole cost — the colors travel in the next frame's copy pass, and
the palette store does not grow. The host calls it once at startup with the working palette and from
`CustomPaletteWiring::apply` after that. `updateRotationPalette` is the same writer over the rotation
set, so the two sets are always arranged alike; nothing uploads a palette after startup.

`shadesOf` is for anything that **shows** a palette's colors rather than draws through them — the
swatch, the ghost piece's fill, the achievements screen's corner selector: a live set's current colors
for `kCustomRamp` and `kRotationRamp`, `rampColours(index)` otherwise. `rampColours` itself is the table's, and is
what the ramp invariants sweep.

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

`settingsPageArrows(ui, ramp, atlas)` returns the settings screen's page arrows as sprites —
`PageArrows` (`src/render/components/page_arrows.h`) named `settings-page`, the game's own selector
tile given a quarter turn, which an object cannot express because the hardware has two flips and no
rotation. Append it to the composed sprites before they are wrapped as the frame's sprite layer:

```cpp
if (game.flow.gameState == kirpich::GameState::SETTINGS) {
    const auto arrows = kirpich::render::settingsPageArrows(game.screens, settings.shadeRamp, tiles);
    sprites.insert(sprites.end(), arrows.begin(), arrows.end());
}
```

`settingsRotationNote(ui, settings, ramp, atlas)` returns the settings screen's rotation note:
`RotationNote` (`src/render/components/rotation_note.h`), the words `(rotating)` in `DimGlyphs`, from
the label column on the empty line under the palette row (`kSettingsRotationNoteX`,
`kSettingsRotationNoteY` in `src/render/settings_overlay.h`). The palette row's label and its arrow
leave nine cells between them and the note takes ten, so it stands on the next line. It is drawn while
the page shown holds the palette row and `Settings::paletteRotation` is not `OFF`, and is empty
otherwise. Append it beside the page arrows:

```cpp
const auto note = kirpich::render::settingsRotationNote(game.screens, settings, paletteIndex, tiles);
sprites.insert(sprites.end(), note.begin(), note.end());
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
| Display | fullscreen and the window size | `src/systems/display_settings_screen.h` | `src/render/screens/display_settings/` |
| Palette | the palette selector — the eighty ramps, then every saved custom palette — a swatch of the chosen one's four colors, and the seven pieces drawn in it | `src/systems/palette_settings_screen.h` | `src/render/screens/palette_settings/` |

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
| Palette | Up opens the rotation screen and Down the palette editor, each with the screen-change cue | step the palette: the ramps, then the saved custom slots, repeating while held | return |

The Palette screen reads Left and Right held, ahead of its table, through `keyRepeatFire` on
`GameFlowState::keyRepeatTimer` (`src/systems/input.h`): a press steps at once, a hold steps again 23
frames later and every 9 after that, Left read ahead of Right, and a held direction is the whole
frame's input. `openPaletteSettings` zeroes the timer, so a direction still held from the screen before
waits out the whole repeat rather than stepping at once.

A move cues TINK; an end stop moves nothing and makes no sound. A value change goes through
`changeSettings(game, wiring, next)` (`src/systems/settings_screen.h`), which stores the settings,
cues TINK and fires `apply` then `save`. A change that lands on the value already held is an end stop:
nothing is written, nothing is cued, and neither seam fires. Right turns fullscreen on and steps the
size up; Left does the opposite. The size is clamped by `clampWindowScale`.

The Display screen's cursor position is `DisplaySettingsState` (`src/state/display_settings_state.h`),
a `GameContext` member of its own: it is the screen's state, not the player's and not the game's. The
Palette screen has one row and keeps no state.

### The Palette screen's walk

The selector is one sequence: the eighty ramps in order, then every **present** slot of the custom
palettes in slot order. With slots 1, 4 and 7 saved it runs `… 79, 80, C01, C04, C07`.

- From a ramp, Right steps to the next ramp, and from the last ramp to the first present slot; Left
  steps to the previous ramp, and from the first it is an end stop.
- **Landing on a slot** copies the slot's palette into the working palette, fires the custom wiring's
  `apply` then `save`, and sets `customPalette` through `changeSettings` — `shadeRamp` is left as it
  was. A step from one slot to another changes no setting, so the step cues TINK itself.
- From a slot, Right steps to the next present slot, an end stop after the last; Left steps to the
  previous present slot, and from the first lands on the last ramp with `customPalette` cleared and
  the working palette left alone — so stepping forward again returns to the same slot.
- A working palette saved in no slot is **off the walk**: the row reads `C`, Left lands on the last
  ramp, Right on the first present slot.

```cpp
bool paletteCanStep(const Settings&, const CustomPalettes* palettes, int delta) noexcept;
```

The step and the row's arrows both ask `paletteCanStep`, so an arrow stands exactly where a press
would move. With `palettes` null the walk is the ramps alone, which is also what a null
`CustomPaletteWiring::palettes` gives the screen.

The walk itself lives in `src/systems/palette_walk.h`, so anything else that picks one of the game's
palettes steps the same sequence with the same end stops:

```cpp
struct PaletteChoice { bool custom = false; std::uint8_t value = 0; };   // a ramp 0-79, or a slot 1-99

std::optional<PaletteChoice> paletteChoiceOf(const Settings&, const CustomPalettes*) noexcept;
std::optional<PaletteChoice> nextPaletteChoice(std::optional<PaletteChoice> from,
                                               const CustomPalettes*, int delta) noexcept;
std::optional<PaletteChoice> paletteChoiceOfColors(const render::ShadeRamp&,
                                                   const CustomPalettes*) noexcept;
render::ShadeRamp colorsOf(PaletteChoice, const CustomPalettes&) noexcept;
std::string paletteChoiceText(std::optional<PaletteChoice>);
```

`paletteChoiceOf` is where the settings stand: the clamped ramp while the custom palette is not in use,
the slot the working palette is saved in while it is, and nothing while it is in use and saved in no
slot (the row's `C`) or in use with no palettes behind it. `nextPaletteChoice` is one step of the walk
described above from any position, nothing included, and returns nothing at an end stop; the Palette
screen steps with `nextPaletteChoice(paletteChoiceOf(s, p), p, delta)`. `paletteChoiceOfColors` names
the stop holding exactly four colors — the first equal ramp, else the first equal present slot, else
nothing; only the colors are compared, so a ramp's `mayBottomOutAtBlack` does not keep it from
matching, and a ramp wins over a slot holding the same colors. `colorsOf` is a stop's four colors with
the flag off: a ramp's from the table, clamped as a stored ramp is, and a slot's from the palettes, the
fresh custom palette for a slot number outside 1-99. `paletteChoiceText` is how a stop reads on a
screen: a ramp as its number counted from one (`1` to `80`), a slot as `C` and two digits (`C01` to
`C99`), and nothing as `C` alone. The Palette screen's row reads
`paletteChoiceText(paletteChoiceOf(s, &palettes))`, and the rotation editor's cells read it too.

### The components

```cpp
Layers DisplaySettingsScreen(const DisplaySettingsState& ui, const Settings& s, bool blinkOn,
                             const TileAtlas& atlas);
Layers PaletteSettingsScreen(const Settings& s, const CustomPalettes& palettes, bool blinkOn,
                             const TileAtlas& atlas);
```

Each returns two layers: a backdrop of tiles at depth 0, and a sprite layer of everything else. The
Display screen's sprite layer holds the `display` heading, the `fullscreen` and `size` rows, and the
cursor. The Palette screen's holds the `palette` heading, the `palette` row, the cursor, the piece
preview and both page arrows — `PageArrows` named `palette`, always shown, the up arrow saying the
rotation screen is above and the down arrow that the palette editor is below — and carries the swatch
as its `regions`.

They are built from shared components, each returning the primitives it is:

| Component | Returns |
|---|---|
| `OptionRow(label, value, line, left, right, atlas, ramp)` (`render/components/option_row.h`) | the label at `kLabelCol`, the value at `kOptionValueCol` and the row's `ScrollerArrows`, on map row `line` |
| `ScrollerArrows(line, left, right, atlas, ramp)` (`render/components/scroller_arrows.h`) | the selector tile at `kOptionLeftArrowCol`, flipped, when `left` is set, and at `kOptionRightArrowCol` when `right` is set |
| `PieceShape(kind, x, y, z, keyStem, atlas, ramp)` (`render/components/piece_shape.h`) | one piece at its spawn orientation, its top-left pixel at (x, y), each part named `keyStem` plus `-` and its index |
| `RampSwatch(atlas, ramp)` (`render/screens/palette_settings/ramp_swatch.h`) | `ColorSwatch("palette-swatch", shadesOf(atlas, ramp), …)`: four opaque squares, darkest to lightest, abutting, centered on the line under the palette row, in the colors `shadesOf` gives the index |
| `ColorSwatch(keyStem, colors, x, y, square)` (`render/components/color_swatch.h`) | four opaque `ColorFill` squares of `square` pixels, `colors[0]` leftmost, abutting from (x, y); square `i` is named `<keyStem>-<i>`, and each color is drawn with alpha 255 |
| `PiecePreview(keyStem, atlas, ramp)` (`render/components/piece_preview.h`) | the seven shapes in two rows, L J I O over S Z T, each row centered from the shapes' own widths, at a fixed place (`kPiecePreviewFirstRowY`, `kPiecePreviewSecondRowY`); each part is named `<keyStem>-<kind>-<part>`, and the Palette screen's stem is `palette-piece` |

An arrow is drawn only where the value can still move that way: no left arrow when fullscreen is off
or at the smallest size or the first palette, no right arrow at the largest size or at the end of the
palette walk. The palette number is counted from one and starts on the value column, one or two
digits; a saved custom palette reads `C` and its slot as two digits (`C01`, `C47`), and a custom
palette saved in no slot reads `C` — three cells at most, which is the value field's width. Every part
of the Palette screen draws through `paletteIndexOf(s)`, the custom set included.

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
    case kirpich::GameState::PALETTE_SETTINGS: {
        // PaletteSettingsScreen(settings, palettes, game.screens.cursorVisible, tiles), the same way
    }
    default:
        break;
}
```

The palette each screen draws through is `paletteIndexOf(settings)`, read once per frame, so the whole
Palette screen recolors the frame the palette steps — onto a saved custom palette as much as onto a
ramp.

**To add a screen like these:** mint its game state, write its logic as an `openX` function, a frame
handler with a dispatch table, and an installer; write its screen function from the shared components
with a `layout.h` derived from the shared grid; and add a case to the render callback's switch.

## The palette editor

Down on the Palette screen opens the editor for the player's working palette and their preset slots.
Everything it has lives in `src/render/screens/palette_editor/`:

| File | Holds |
|---|---|
| `palette_editor_state.h` | `PaletteEditorState` — the row, the channel, the preset, the entered value, the question and the held preview's flag — and the enums it is made of |
| `palette_editor.h` / `.cpp` | the logic: opening, the Select level, the three input tables, the installer, and the answers the picture asks for |
| `layout.h` | every position, in viewport pixels, and the checks that keep the heading clear of the held preview |
| `screen.h` / `.cpp` | `PaletteEditorScreen`, the layers |

The held preview is drawn from two shared components the rotation screen and the rotation editor draw
too:

| Component | Holds |
|---|---|
| `PreviewBackdrop(atlas, ramp)` (`render/components/preview_backdrop.h`) | the backdrop with the brick walls the preview stands between; the walls' columns and pixel edges (`kPreviewWallLeftColumn`, `kPreviewWallRightColumn`, `kPreviewWallLeftX`, `kPreviewWallRightX`, `kPreviewBrickSide`) |
| `PalettePreview(atlas, ramp, level = std::nullopt)` (`render/components/palette_preview.h`) | the words in their three inks and the seven pieces, and — the preview's one switch — the `level` item when `level` holds one: `LevelSelector("preview-level", level, kPalettePreviewLevelX, kPalettePreviewLevelY, …)`, the word `level` and the level's number with an arrow each way it can still step, under the pieces. The palette editor passes no level; the rotation screen and the rotation editor pass the level shown, so every preview that shows a level shows it this one way. Also the words' place (`kPalettePreviewWords`, `kPalettePreviewWordFirstY`, `kPalettePreviewWordPitch`, `palettePreviewWordX`, `palettePreviewWordY`) and `palettePreviewWordsInside(left, right)`, which a screen asserts against its walls |

### The rows

```
custom palette *       the heading, with an asterisk while the working palette has unsaved changes
preset        < 04 >   the slot the three actions name, 1-99
load preset            copy the slot into the working palette
save preset            write the working palette into the slot
clear preset           empty the slot
1  000 000 000  ▮      one row per shade, darkest first: red, green and blue, and the shade's color
2  ...
```

`PaletteEditorRow` walks `PRESET`, `LOAD`, `SAVE`, `CLEAR`, `SHADE_1`-`SHADE_4`; `shadeOf(row)` gives a
shade row's shade, 0-3. The channel (`ColorChannel`) is kept while the cursor is on the rows above the
shades.

### Opening and leaving

```cpp
void openPaletteEditor(GameContext&, const CustomPalettes* palettes);
void installPaletteEditor(GameStateDispatcher&, SettingsWiring, CustomPaletteWiring);
```

`openPaletteEditor` resets `GameContext::paletteEditor`, puts the preset on `savedSlotOf(*palettes)` or
on slot 1 when the working palette is saved in no slot or `palettes` is null, shows the cursor, arms the
shared blink and enters `GameState::PALETTE_EDITOR`. The Palette screen calls it on Down with the
screen-change cue. The editor takes the same `CustomPaletteWiring` the Palette screen takes; the host
builds one and hands it to both installers. It also takes the `SettingsWiring`, for the one change it
makes to the settings: clearing the slot that holds the palette in use.

Up on the `preset` row and B on any row return to the Palette screen through `openPaletteSettings`,
with the screen-change cue. The settings screen's cursor is untouched, so B from the Palette screen
still lands on the Palette row. The editor writes no background map and no object buffer, and saves no
caller.

### Input

Each frame runs `blinkScreenCursor`, then reads Select as a level: `PaletteEditorState::previewing` is
set to whether Select is held this frame, and while it is set the frame ends there. No table runs, so
no press moves, steps, answers or leaves, nothing is cued and the key-repeat timer is untouched; on
release the editor is where it was, a question still up and an entered value still entered at its
place. A tap shows the preview for one frame. `reset()` and `openPaletteEditor` clear the flag.

Otherwise one of three dispatch tables runs: a question's while one is up, an entered value's while a
value is entered, and the walk's otherwise.

| Mode | Up / Down | Left / Right | A / Start | B |
|---|---|---|---|---|
| Walk | move the row; Up past `preset` leaves, Down at the last row is an end stop | the preset on its row, the channel on a shade row, nothing on an action row | the action on its row; enter the value on a shade row | leave |
| Entered value | step the value by its place, on the key repeat | move the place | keep the value | put back the value it had when entered |
| Question | — | pick `no` or `yes` | answer | `no` |

A move cues TINK and an end stop cues nothing, everywhere on the screen.

### An entered value

A value is one `std::uint8_t`. While it is entered, `place` (`DigitPlace::HUNDREDS`, `TENS`, `ONES`)
is the digit Up and Down step:

```cpp
std::uint8_t steppedValue(std::uint8_t value, DigitPlace place, int delta) noexcept;
```

`steppedValue` adds or takes `placeStep(place)` (100, 10 or 1) and clamps to 0-255: from `099` at the
hundreds Up gives `199`, then `255`, then nothing. A step that returns the value unchanged is an end
stop. Otherwise the working palette's channel takes the new value, the wiring's `apply` and then
`save` fire, and the step cues TINK — so the atlas holds the new colors before anything names them.

Up and Down are read held, ahead of the table, through `keyRepeatFire` on `GameFlowState::keyRepeatTimer`
(`src/systems/input.h`): a press steps at once, a hold steps again 23 frames later and every 9 frames
after that. Entering a value zeroes the timer and cues nothing. B writes `valueOnEntry` back through
`apply` and `save`, unless the value already is that, and cues nothing.

### The actions and their questions

```cpp
bool paletteEditorActionCanAct(PaletteEditorRow action, std::uint8_t preset,
                               const CustomPalettes* palettes) noexcept;
bool presetCanStep(std::uint8_t preset, const CustomPalettes* palettes, int delta) noexcept;
```

| Action | An end stop while | Asks first while | Does |
|---|---|---|---|
| load preset | the slot is empty, or holds the working palette | the working palette has unsaved changes (`hasUnsavedChanges`) | working palette = the slot; `apply`, `save` |
| save preset | the slot already holds the working palette | the slot holds a different palette | the slot = the working palette, present; `save` |
| clear preset | the slot is empty | always | the slot emptied; `save`. The working palette is untouched. When the slot held the palette in use and no other slot holds it, `customPalette` is cleared through `changeSettings`, so the game returns to the ramp underneath |

A question opens on `no` with the screen-change cue and the blink re-armed. `yes` does the action;
`no` and B change nothing. Either way the question closes with the screen-change cue and the cursor
still on the row. With no `palettes` behind the wiring, the preset step, entering a value and all three
actions are end stops; the walk and the way out still work.

### The picture

```cpp
Layers PaletteEditorScreen(const PaletteEditorState& ui, const Settings& s,
                           const CustomPalettes& palettes, bool blinkOn, const TileAtlas& atlas);
```

Two layers: the backdrop (`editor-backdrop`, depth 0), and one of `editor-preview` while the held
preview is up (below), `editor-question` while a question is up — `YesNoQuestion` with the question's
words — or `editor-content`:

| Question | Title | Lines |
|---|---|---|
| `LOAD_UNSAVED` | `load preset` | `your palette is` / `not saved` |
| `SAVE_OVER` | `save preset` | `replace` / `preset nn` |
| `CLEAR` | `clear preset` | `empty` / `preset nn` |

`editor-content` holds the up arrow (`PageArrows` named `editor`), the heading and its asterisk, the
preset row — its number through `DimGlyphs` while the slot is empty — with `preset-left` /
`preset-right` where `presetCanStep` allows, the three action labels —
each through `DimGlyphs` while `paletteEditorActionCanAct` says it can do nothing — the four shade rows
and the row cursor. While a value is entered its digits draw through `SelectedGlyphs`, the cursor is
not drawn, `value-left` / `value-right` stand either side where the place can move, `digit-up` /
`digit-down` stand over and under the digit being stepped where `steppedValue` would move it, and the
hint `a confirms b cancels` stands at the bottom. The layer's `regions` are the band: the working
palette's four colors as opaque `ColorFill` rectangles down the right of the shade rows.

**The editor draws through the built-in ramp underneath** — `clampShadeRamp(s.shadeRamp)`, never
`paletteIndexOf(s)` — so stepping a value never recolors the editor itself. The band shows the working
palette's colors directly.

**While `previewing` is set the held preview takes the screen**, from the editor and from any question.
Its two layers are drawn entirely through the custom set, `kCustomRamp`, whatever the settings say,
because the preview is where the palette is judged:

| Layer | Holds |
|---|---|
| `editor-backdrop` | `PreviewBackdrop(atlas, kCustomRamp)`: the plain backdrop with columns `kPreviewWallLeftColumn` (0) and `kPreviewWallRightColumn` (19) replaced, row by row, by the playfield's own left wall from `kTypeAGameplayTilemap`, so the bricks repeat as they do beside the well. They are background cells through the content palette, which draws the brick's lightest shade as a color |
| `editor-preview` | the heading and its asterisk, as the editor draws them, then `PalettePreview(atlas, kCustomRamp, std::nullopt)`: `selected` through `SelectedGlyphs`, `unselected` through `Glyphs` and `dimmed` through `DimGlyphs`, each centered at `palettePreviewWordY(i)`, then `PiecePreview("preview-piece", …)` where the Palette screen draws its pieces. A custom palette belongs to no level, so the `level` item is off. No regions |

Nothing else is drawn while it is up: no arrow, no preset row, no labels, no shade rows, no cursor and
no hint. The words and the pieces stand between the two walls; `kPalettePreviewWordPitch` sets the
words' spacing and moves nothing else. The host's render callback has the screen's case:

```cpp
case kirpich::GameState::PALETTE_EDITOR: {
    retropp::FrameDrawState screen;
    screen.layers = kirpich::render::PaletteEditorScreen(
        game.paletteEditor, settings, palettes, game.screens.cursorVisible, tiles);
    renderer.renderFrame(screen);
    return;
}
```

## The rotation screen

Up on the Palette screen opens the screen that chooses which rotation a round's palette follows
(`Settings::paletteRotation`), and shows the ten palettes that rotation gives. Everything it has lives
in `src/render/screens/palette_rotation/`; the walk its selector steps is in `src/systems/`:

| File | Holds |
|---|---|
| `rotation_screen_state.h` | `RotationScreenState` — the cursor's row, the held preview's flag and its level — and `RotationScreenRow` |
| `rotation_screen.h` / `.cpp` | the logic: `RotationWiring`, opening, `rotationCanStep`, the two input tables and the installer |
| `layout.h` | every position, in viewport pixels, the grid's place functions, and the fit checks |
| `level_grid_digits.h` / `.cpp` | `LevelGridDigits`, the grid's ten level numbers |
| `level_grid_swatches.h` / `.cpp` | `LevelGridSwatches` and `LevelColors`, the grid's ten swatches |
| `screen.h` / `.cpp` | `PaletteRotationScreen`, the layers |
| `src/systems/rotation_walk.h` / `.cpp` | `RotationChoice`, `rotationChoiceOf`, `nextRotationChoice` — the walk |

### The rows

```
palette rotation       the heading
0    1    2    3    4  the ten levels, each its number over its four colors
5    6    7    8    9
edit rotation          opens the rotation editor
rotation   < hotter >  the rotation a round follows
```

The menu's two rows are `RotationScreenRow::EDITOR` and `SELECTOR`, top to bottom. The screen opens
with the cursor on the selector from the Palette screen, and on the editor item when the rotation
editor returns to it.

### Opening and leaving

```cpp
struct RotationWiring {
    CustomRotations*                            rotations = nullptr;
    std::function<void(const CustomRotations&)> save;
};

void openRotationScreen(GameContext&, RotationScreenRow row = RotationScreenRow::SELECTOR);
void installRotationScreen(GameStateDispatcher&, SettingsWiring, RotationWiring);
```

`openRotationScreen` resets `GameContext::rotationScreen` — the preview down, its level 0 — puts the
cursor on `row`, shows the cursor, arms the shared blink and enters `GameState::PALETTE_ROTATION`. The
Palette screen calls it on Up with the screen-change cue; the rotation editor calls it with `EDITOR`
when it is left. Down on the selector and B on either row return to the Palette screen through
`openPaletteSettings`, with the screen-change cue. A or Start on the editor item opens the rotation
editor with the screen-change cue ([below](#the-rotation-editor)). The screen writes no background map
and no object buffer, and saves no caller.

`RotationWiring::rotations` is the host's live rotations; `save` writes their document. With
`rotations` null the selector offers the presets alone and the editor item opens nothing. The rotation
editor takes the same wiring. The host builds it beside the palette wiring:

```cpp
const kirpich::systems::RotationWiring rotationWiring{
    .rotations = &rotations,
    .save = [&saves](const kirpich::CustomRotations& current) {
        kirpich::saveCustomRotations(current, saves);
    },
};
kirpich::systems::installRotationScreen(dispatcher, settingsWiring, rotationWiring);
```

### Input

Each frame runs `blinkScreenCursor`, then reads Select as a level: `RotationScreenState::previewing` is
set to whether Select is held this frame. While it is set the held preview's table runs and nothing
else; otherwise the menu's.

| Mode | Up | Down | Left / Right | A / Start | B |
|---|---|---|---|---|---|
| Menu | move to the editor item; an end stop there | move to the selector; on the selector, leave | step the rotation on the selector; nothing on the editor item | open the rotation editor on the editor item, with rotations behind the wiring; nothing on the selector | leave |
| Held preview | the next level | the previous level | Left the previous level, Right the next | nothing | nothing |

The preview's level runs 0-9 and stops at both ends. It keeps its value from one hold to the next
until the screen is left. A move or a step cues TINK, an end stop cues nothing, and Select itself cues
nothing.

### The rotation walk

```cpp
struct RotationChoice { PaletteRotation mode = PaletteRotation::OFF; std::uint8_t slot = 0; };

std::optional<RotationChoice> rotationChoiceOf(const Settings&, const CustomRotations*) noexcept;
std::optional<RotationChoice> nextRotationChoice(std::optional<RotationChoice> from,
                                                 const CustomRotations*, int delta) noexcept;
bool rotationCanStep(const Settings&, const CustomRotations*, int delta) noexcept;
```

The selector is one sequence: `off`, the three presets in `PaletteRotation`'s order, then every
**present** slot of the custom rotations in slot order. With slots 1, 4 and 7 saved it runs
`off, hotter, colder, rainbow, C01, C04, C07`.

- From `off` or a preset, Right steps to the next of those, and from `rainbow` to the first present
  slot; Left steps to the previous, and from `off` it is an end stop.
- **Landing on a slot** copies the slot's rotation into the working rotation, fires the wiring's `save`,
  and sets `paletteRotation` to `CUSTOM` through `changeSettings`. A step from one slot to another
  changes no setting, so the step cues TINK itself.
- From a slot, Right steps to the next present slot, an end stop after the last; Left steps to the
  previous present slot, and from the first lands on `rainbow`, the working rotation left alone.
- A working rotation saved in no slot is **off the walk**: the value reads `C`, Left lands on
  `rainbow`, Right on the first present slot.

`rotationChoiceOf` is where the settings stand: `off` and the presets are themselves, `CUSTOM` is the
slot the working rotation is saved in, and nothing while it is saved in no slot or there are no
rotations. The step and the selector's arrows both ask `rotationCanStep`, so an arrow stands exactly
where a press would move. The walk follows the enum's declared order; a `static_assert` in
`rotation_walk.cpp` holds the order, so reordering `PaletteRotation` fails to build rather than
reordering the walk.

### The picture

```cpp
using LevelColors = std::array<std::array<retropp::Rgba8, 4>, kRotationLevelCount>;

Layers PaletteRotationScreen(const RotationScreenState& ui, const Settings& s,
                             const CustomRotations& rotations, bool blinkOn,
                             const TileAtlas& atlas);
Sprites LevelGridDigits(const TileAtlas& atlas, std::uint8_t ramp);
Regions LevelGridSwatches(const LevelColors& levels);
```

Two layers: the backdrop (`rotation-backdrop`, depth 0), and either `rotation-preview` while the held
preview is up or `rotation-content`.

`rotation-content` holds the heading, the grid's digits, the editor item, the selector — its label,
its value and `rotation-left` / `rotation-right` where `rotationCanStep` allows — the blinking cursor
before the row it is on, and the down page arrow (`PageArrows` named `rotation`). The value reads `off`
or the preset's name (`presetRotationName`); for `CUSTOM`, the name of the slot the working rotation is
saved in, `C` and that slot as two digits (`C01`) where the slot has no name, or `C` alone where it is
saved in none. `layout.h` asserts that `kSelectorValueMaxGlyphs` (7) has room for a preset's name and a
saved rotation's. The layer's `regions` are the grid's swatches.

The grid is two rows of five: level `i` in column `i % 5` of row `i / 5`, its digit at
`gridDigitX(i)`, `gridDigitY(i)` over its swatch at `gridSwatchX(i)`, `gridSwatchY(i)`. Each swatch is a
`ColorSwatch` of `kGridSquare`-pixel squares named `rotation-level-<i>`, so a square is
`rotation-level-<i>-<shade>`. The colors are the selected mode's ten (`rotationPalettes`), so the grid
changes as the selector steps; on `off` every cell is the player's own palette (`shadesOf` at
`paletteIndexOf(s)`), which is what every level of a round looks like then. The menu draws through
`paletteIndexOf(s)`.

**While `previewing` is set the held preview takes the screen.** It is the palette editor's preview
through one level's palette, with the level beside it:

| Layer | Holds |
|---|---|
| `rotation-backdrop` | `PreviewBackdrop(atlas, preview)`, the backdrop with its two brick walls |
| `rotation-preview` | the heading, where the menu draws it, and `PalettePreview(atlas, preview, previewLevel)`: the words, the pieces and the `level` item. No regions |

`preview` is `kRotationRamp` while a rotation is on, and `paletteIndexOf(s)` on `off`. The rotation set
holds the mode's palette at `previewLevel` while the preview is up (`rotationPaletteFor`, above), so the
preview shows the level as a round will draw it. Nothing of the menu is drawn while it is up.

The two shared components it adds:

| Component | Returns |
|---|---|
| `ColorSwatch(keyStem, colors, x, y, square)` (`render/components/color_swatch.h`) | four opaque `ColorFill` squares of `square` pixels, abutting from (x, y), named `<keyStem>-<i>` |
| `LevelSelector(keyStem, level, x, y, atlas, ramp)` (`render/components/level_selector.h`) | the word `level` at (x, y), the number `kLevelSelectorNumberDx` (48) after it, and `<keyStem>-left` / `-down` while `level > 0`, `<keyStem>-right` / `-up` while `level < 9` — in the cells either side of the number and a glyph above and below it. `kLevelSelectorWidth` (64) is the whole item's width |

The words, `kGridSquare`, `kGridRowPitch` and `kPreviewLevelY` are each one constant in `layout.h`.
The render callback has the screen's case:

```cpp
case kirpich::GameState::PALETTE_ROTATION: {
    retropp::FrameDrawState screen;
    screen.layers = kirpich::render::PaletteRotationScreen(
        game.rotationScreen, settings, rotations, game.screens.cursorVisible, tiles);
    renderer.renderFrame(screen);
    return;
}
```

## The rotation editor

`edit rotation` on the rotation screen opens the editor for the player's working rotation and their
rotation slots. Everything it has lives in `src/render/screens/rotation_editor/`; the text input
screen it raises to name a save is the shared `src/render/components/text_entry.h`:

| File | Holds |
|---|---|
| `rotation_editor_state.h` | `RotationEditorState` — the row and the level cell, the preset, the entered number, the name being typed, the question and the held preview's flag — and `RotationEditorRow`, `RotationEditorQuestion` |
| `rotation_editor.h` / `.cpp` | the logic: opening, the Select level, the five input tables, the name wheel, the installer, and the answers the picture asks for |
| `layout.h` | every position, in viewport pixels, the grid's cell functions, and the fit checks |
| `level_cells.h` / `.cpp` | `LevelCells`, the grid's text: each level's digit, colon and number, the entered number's arrows and the grid's cursor |
| `level_cell_swatches.h` / `.cpp` | `LevelCellSwatches`, each level's four colors under its text |
| `screen.h` / `.cpp` | `RotationEditorScreen`, the layers |

### The rows

```
custom rotation *      the heading, with an asterisk while the working rotation has unsaved changes
preset   < neon    >   what the three actions name: hotter, colder, rainbow, then slots 1-99
load preset            copy the preset into the working rotation
save preset            name the working rotation and write it into the slot
clear preset           empty the slot
 0: 12       5: 31     the ten levels, two columns of five: the level, a colon, the number of the
 ▮▮▮▮        ▮▮▮▮      palette it wears, and that palette's four colors under it
 1: C04      6: 80
 ...
```

`RotationEditorRow` walks `PRESET`, `LOAD`, `SAVE`, `CLEAR`, `LEVELS`; `LEVELS` is the grid, and
`RotationEditorState::level` (0-9) is the cell. The level is kept while the cursor is on the rows above
the grid, and the held preview shows it.

### Opening and leaving

```cpp
void openRotationEditor(GameContext&, const CustomRotations* rotations);
void installRotationEditor(GameStateDispatcher&, SettingsWiring, RotationWiring,
                           const CustomPalettes* palettes);
```

`openRotationEditor` resets `GameContext::rotationEditor`, puts the preset on the slot
`savedSlotOf(*rotations)` names, or on slot 1 when the working rotation is saved in no slot or
`rotations` is null, shows the cursor, arms the shared blink and enters `GameState::ROTATION_EDITOR`.
The rotation screen calls it from its editor item with the screen-change cue. The editor takes the
rotation screen's `RotationWiring` and the `SettingsWiring`, for the one change it makes to the
settings, and reads the custom palettes for the walk its numbers step along; it never writes them:

```cpp
kirpich::systems::installRotationEditor(dispatcher, settingsWiring, rotationWiring, &palettes);
```

B returns to the rotation screen with the cursor on its editor item, through
`openRotationScreen(game, RotationScreenRow::EDITOR)`, with the screen-change cue. The editor writes no
background map and no object buffer, and saves no caller.

### Input

Each frame runs `blinkScreenCursor`, then reads Select as a level: `RotationEditorState::previewing` is
set to whether Select is held this frame. While it is set, the held preview's table runs in the walk,
and in every other mode no table runs, so an entered number, the name screen and a question keep their
place until Select is released. Otherwise one table runs: the name screen's while it is up, a
question's while one is up, an entered number's while a number is entered, and the walk's otherwise.

| Mode | Up / Down | Left / Right | A / Start | B |
|---|---|---|---|---|
| Walk | move the row; in the grid, walk the column — Up from its top cell returns to `clear preset`, Down from `clear preset` enters the top of the column the cursor was last in | the preset on its row; the other column in the grid; nothing on an action row | the action on its row; enter the number on a cell; nothing on the preset row | leave |
| Entered number | Up ten stops forward along the palette walk, Down ten back, on the key repeat | one stop along the palette walk, on the key repeat | keep the number | put back the palette the level wore when entered |
| Name screen | turn the character at the place, on the key repeat | nothing | A: the next place, finishing on the seventh; Start: finish | the previous place; on the first, cancel the save |
| Question | — | pick `no` or `yes` | answer | `no` |
| Held preview (in the walk) | Up the next level, Down the previous | Right the next level, Left the previous | nothing | nothing |

Up on `preset` and Down on a column's bottom cell are end stops, and so are Left in the left column and
Right in the right one. A move, a step and a turn cue TINK; an end stop cues nothing, and so does Select
itself. The preview's level is the cursor's own cell, so it opens on the selected cell and, released in
the grid, leaves the cursor on the level last shown.

### The preset selector

```cpp
bool rotationEditorPresetCanStep(RotationChoice preset, const CustomRotations*, int delta) noexcept;
std::string rotationPresetText(RotationChoice preset, const CustomRotations&);
```

The preset is a `RotationChoice` (`src/systems/rotation_walk.h`): a shipped rotation with slot 0, or
`CUSTOM` with a slot 1-99. Its stops are `hotter`, `colder` and `rainbow` in `PaletteRotation`'s order,
then every slot, present or empty. Left from `hotter` and Right from slot 99 are end stops; Right from
`rainbow` is slot 1 and Left from slot 1 is `rainbow`. The step and the two arrows
(`rotation-editor-preset-left` / `-right`) both ask `rotationEditorPresetCanStep`, which is false with
no rotations.

`rotationPresetText` is what the selector reads: a shipped rotation's name, a slot's name where it has
one, and the slot's number — `1` to `99`, no leading zero — where it has none. An empty slot's value is
drawn through `DimGlyphs`. The selector's value has room for seven characters.

### The actions and their questions

```cpp
bool rotationEditorActionCanAct(RotationEditorRow action, RotationChoice preset,
                                const CustomRotations*) noexcept;
```

| Action | Acts on a shipped rotation | Acts on a slot | Asks first while | Does |
|---|---|---|---|---|
| load preset | while its table differs from the working rotation | while present and holding another rotation | the working rotation has unsaved changes (`hasUnsavedChanges`) | the working rotation takes the table or the slot's rotation; `save` |
| save preset | never | always | the slot is present and holds a different rotation | raises the name screen ([below](#naming-a-save)) |
| clear preset | never | while present | always | the slot emptied, name and all; `save`. When the mode is `CUSTOM`, the slot held the working rotation and no other slot holds it, `paletteRotation` is set to `OFF` through `changeSettings`; the working rotation is left as it is |

Loading a shipped rotation is how a preset seeds a rotation of the player's own. Saving onto the slot
that already holds the working rotation is how a saved rotation is renamed. An action that cannot act
asks nothing and cues nothing, and its label is drawn through `DimGlyphs`; the action and its label
both ask `rotationEditorActionCanAct`, which is false with no rotations and for any row that is not an
action.

A question opens on `no` with the screen-change cue and the blink re-armed. `yes` does the action;
`no` and B change nothing. The question closes with the screen-change cue — and after `save preset`'s,
`yes` raises the name screen in its place.

| Question | Title | Lines |
|---|---|---|
| `LOAD_UNSAVED` | `load preset` | `your rotation is` / `not saved` |
| `SAVE_OVER` | `save preset` | `replace` / `preset nn` |
| `CLEAR` | `clear preset` | `empty` / `preset nn` |

### Naming a save

`save preset` raises the name screen: `naming` set, `namePlace` 0, the key repeat zeroed, the cursor
shown and the blink armed, with the screen-change cue. `typed` opens holding `rotationPresetText` of
the slot — its name, or its number where it has none — left-aligned in seven places. Nothing is written
until the name is finished.

```cpp
char turnedNameWheel(char current, int delta) noexcept;
```

The screen takes its characters as the top-score name entry does ([high-score-state.md](high-score-state.md)):
Up and Down turn the character at `namePlace` on a wheel of `a` to `z`, `.`, `-`, `0` to `9` and the
space — the top-score wheel's order, letters first and the digits after the symbols — wrapping both
ways, read on the key repeat with Up ahead of Down, a held direction being the
whole frame's input. `turnedNameWheel` is one turn — `delta` positive turns up — and a character the
wheel does not hold turns as the space does. A moves to the next place with the screen-change cue, a
blank place reached that way becoming `a`, and on the seventh place finishes; Start finishes from any
place; B moves back a place with no cue, and on the first place cancels the save with the
screen-change cue, writing nothing.

Finishing is the save: the slot becomes `CustomRotationSlot{.present = true, .name = typed,
.progression = working}` — the slot's number where every place is a space — `save` fires and the
finish cues TINK. The name reads on this screen's selector and on the rotation screen's.

### An entered number

```cpp
std::optional<PaletteChoice> levelPaletteChoice(const RotationEditorState&, const CustomRotations&,
                                                const CustomPalettes*, std::size_t level) noexcept;
bool enteredNumberCanStep(const RotationEditorState&, const CustomPalettes*, int delta) noexcept;
```

A level stores colors; its number is a derived identity. `levelPaletteChoice` is the stop a cell reads:
`entered` for the cell being entered, and for every other cell `paletteChoiceOfColors` of the level's
colors — a ramp's number, `C` and a saved palette's slot, or `C` alone for colors on no stop. A or
Start on a cell enters it: `entering` set, `entered` the cell's stop, `colorsOnEntry` its colors, the
key repeat zeroed, no cue.

Left and Right step `entered` one stop along the Palette screen's walk
([above](#the-palette-screens-walk)) — the eighty ramps, then the present saved palettes — and Up and
Down ten stops, forward and back. All four are read on the key repeat, in that order, a held direction
being the whole frame's input: a press steps at once, a hold steps again 23 frames later and every 9
after that. A ten-step clamps: it runs as far as the walk goes and stops at its end, so Up from ramp 76
with slots 1, 4 and 7 saved lands on `C07`, and Down from ramp 4 on ramp 1. Each step copies the stop's
colors into the level (`colorsOf`), writes the rotations document through `save`, and cues TINK; a step
that cannot move at all is an end stop and writes and cues nothing. **The step moves from `entered`,
never from a stop read back off the level's colors**: a saved palette holding a ramp's colors reads as
that ramp, and a walk that re-derived its place each step would fall back onto the ramp and never
reach the slots after it. With no palettes the walk is the ramps alone. The step and the number's
arrows both ask `enteredNumberCanStep`: `rotation-editor-number-left` and `-down` stand where a step
back exists, `-right` and `-up` where a step forward does. Because a ten-step clamps, it can move
exactly where a single step can, so Up shows with Right and Down with Left. A keeps the number with a
TINK; B puts `colorsOnEntry` back, writing the document only when the colors changed, with no cue.

### The held preview

While Select is held the editor shows the palette editor's preview through the working rotation's
palette for the selected level — whichever rotation is switched on, `OFF` included — with the `level`
item under the pieces showing which level it is, exactly as the rotation screen's preview does. `rotationPaletteFor` yields that palette while
`rotationEditor.previewing` is set ([above](#the-frames-palette)), so the reconcile puts it on the
rotation set and the preview draws through `kRotationRamp`.

### The picture

```cpp
Layers RotationEditorScreen(const RotationEditorState& ui, const Settings& s,
                            const CustomRotations& rotations, const CustomPalettes& palettes,
                            bool blinkOn, const TileAtlas& atlas);
Sprites LevelCells(const RotationEditorState&, const CustomRotations&, const CustomPalettes&,
                   bool blinkOn, const TileAtlas&, std::uint8_t ramp);
Regions LevelCellSwatches(const RotationProgression&);
```

Two layers: the backdrop (`rotation-editor-backdrop`, depth 0), and one of `rotation-editor-preview`
while the held preview is up, `rotation-editor-name` while the name screen is up — `TextEntry` titled
`save preset` holding `typed` with `namePlace` blinking — `rotation-editor-question` while a question
is up, or `rotation-editor-content`.

`rotation-editor-content` holds the heading and its asterisk (`hasUnsavedChanges`), the preset row with
its arrows, the three action labels, the row cursor before a label on the four upper rows, and
`LevelCells`. The layer's `regions` are `LevelCellSwatches(rotations.working)`. Level `i` stands in
column `i / 5` of row `i % 5`; from its cell's left edge (`cellX(i)`, `cellY(i)`) come the cursor
(`kCellCursorDx`), the level's digit (`kCellLevelDx`), the colon (`kCellColonDx`), the entered number's
left arrow (`kCellLeftArrowDx`), the number (`kCellNumberDx`, at most three characters, `C04`) and its
right arrow (`kCellRightArrowDx`). The entered number's up and down arrows stand centered on the
number's field (`kCellVerticalArrowDx`), the up arrow at `kCellUpArrowDy` and the down arrow at
`kCellDownArrowDy`. Those offsets are set by where each arrow inks (`kUpArrowInkTop`,
`kDownArrowInkBottom`) rather than by a whole glyph, so each stands one pixel off the number. The
swatch stands under the digit at `cellSwatchX(i)`,
`cellSwatchY(i)`, four squares of `kCellSquare` pixels named `rotation-editor-level-<i>`, so a step
recolors its cell on the same frame. The entered number draws through `SelectedGlyphs` and no cursor is
drawn while a number is entered. The menu, the name screen and the questions draw through
`paletteIndexOf(s)`.

**While `previewing` is set the held preview takes the screen**, from the editor, the name screen and
any question, drawn entirely through `kRotationRamp`:

| Layer | Holds |
|---|---|
| `rotation-editor-backdrop` | `PreviewBackdrop(atlas, kRotationRamp)`, the backdrop with its two brick walls |
| `rotation-editor-preview` | the heading, where the editor draws it but without the asterisk, and `PalettePreview(atlas, kRotationRamp, level)`: the words, the pieces and the `level` item. No regions |

The heading's words, the question lines, the colon's art, `kGridRowPitch` and `kCellSquare` are each
one constant. The render callback has the screen's case:

```cpp
case kirpich::GameState::ROTATION_EDITOR: {
    retropp::FrameDrawState screen;
    screen.layers = kirpich::render::RotationEditorScreen(
        game.rotationEditor, settings, rotations, palettes, game.screens.cursorVisible, tiles);
    renderer.renderFrame(screen);
    return;
}
```

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
as those do. `RestoreConfirm(yes, blinkOn, atlas, ramp)` (`src/render/screens/controls/restore_confirm.h`)
returns it as a sprite layer of its own — `YesNoQuestion` (`src/render/components/yes_no_question.h`)
named `controls-confirm` with the screen's words — and `ControlsScreen` returns that layer in place of
the bindings' while it is asked.

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
column 15, each cursor in the cell before its name (`src/render/screens/controls/layout.h`). The restore row
stands on line 14, a line below the buttons, laid out as a settings row is: label from column 3, cursor
on column 1. While the bindings already are the defaults the label is drawn through `DimGlyphs`
(`src/render/components/glyphs.h`), the ramp's dim sprite palette a locked achievement badge uses, because the
row has nothing to restore and a press on it does nothing. The cursor blinks while the screen is idle and is held while it waits. While it waits, the
waited-on cell reads `...`, line 16 reads `press a key` or `press a button`, and line 17 reads
`esc cancels`.

A name fits five cells and uses only what `Glyphs` draws:

- `keyName(SDL_Scancode)` (`src/render/screens/controls/names.h`) — letters and digits as themselves, a
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

The ghost, fixes and name-digits rows open **carousel** instances (`src/systems/carousel_screen.h`):
one option to a screen — a title, an enable row in this screen's own scroller geometry, and a
description — with up and down moving between options, left and right toggling the shown one, and B
returning here. The name-digits instance (`INIT_NAME_DIGITS_SCREEN` / `NAME_DIGITS_SCREEN`) holds one
option, `name digits`, bound to `Settings::nameDigits`. The new-modes row opens the mode screen
(`src/systems/mode_screen.h`): one option, eleven rows of prose, and an optional preview seam that
draws into the map below them.

Both are machines, and neither owns a word of what it shows. The options, the flags their switches
toggle, and the two dispatch slots an instance answers to all arrive through their installers.

**The content is one unit.** `src/systems/enhancement_screens.h` holds what all four screens say,
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
raise the instance's published count in the header. `kFixesOptionCount`, `kGhostOptionCount` and
`kNameDigitsOptionCount` are static-asserted against their tables, so the count the render layer reads cannot drift from what the
table holds — a table that grows without its count fails to compile.

**To add an instance,** mint two `GameState` slots, add the table and its prose beside the others,
install it in the same call, and add its loop state and published count to the host's carousel-arrows
condition in `src/main.cpp`. The shown-option index on `ScreenUiState` is shared, because only
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
