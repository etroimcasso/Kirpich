# Pre-game screens

The whole pre-game flow, from power-on to the start of a round: the copyright screens, the title
screen, the config screen, the game-type and music-type selectors, and the difficulty pickers for all
three modes. Two units make it up — the **selection screens** (config through the difficulty pickers)
and the **title and copyright screens** — sharing one dispatcher, one helper set, and this page.

The behavioral specifications — what the original game does, line by line — are in
[`../contracts/menu-screens.md`](../contracts/menu-screens.md) and
[`../contracts/title-screens.md`](../contracts/title-screens.md); the design rationale is in
[`../features/menu-screens.md`](../features/menu-screens.md) and
[`../features/title-screens.md`](../features/title-screens.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/menu_screens.h` / `.cpp` | The `kirpich::systems` selection-screen handlers, their shared helpers, and the installer. |
| `src/systems/title_screens.h` / `.cpp` | The copyright and title-screen handlers and their installer. |
| `include/kirpich/action.h` | The six menu actions (`MenuUp`/`MenuDown`/`MenuLeft`/`MenuRight`/`Confirm`/`Back`). |
| `src/systems/input.cpp` | Their default bindings and their place in the held-action walk. |
| `tests/test_menu_screens.cpp` / `tests/test_title_screens.cpp` | The behavioral tests. |

Each handler takes a `GameContext&` (`src/systems/game_context.h`) and reads or writes through it. They
own no state: the menu selections, the frame timer, and the game state live on `GameFlowState`; the
cursor sprites on `SpriteRendererState`; the object buffer on `EngineState`; the audio cues on the
`AudioCues` member; and the multiplayer flag on `MultiplayerState`. The cursor coordinate tables and the
cursor sprite lists come from the data layer (`src/data/misc.h`, `src/data/scene_sprites.h`).

## The surface

```cpp
namespace kirpich::systems {

// State handlers — one per game state; the dispatcher runs one per frame.
void initConfigScreen(GameContext& game);          // $08: reset serial, then run the body below
void loadConfigScreenBody(GameContext& game);      // $08 body, also entered by demo-start / two-player
void selectGameType(GameContext& game);            // $0E
void selectMusicType(GameContext& game);           // $0F
void initTypeADifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});  // $10
void selectTypeALevel(GameContext& game, const TopScoresRefresh& refresh = {});           // $11
void initTypeBDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});  // $12
void selectTypeBLevel(GameContext& game, const TopScoresRefresh& refresh = {});           // $13
void selectTypeBHeight(GameContext& game, const TopScoresRefresh& refresh = {});          // $14
void initTypeCDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});  // $47
void selectTypeCLevel(GameContext& game, const TopScoresRefresh& refresh = {});           // $48
void selectTypeCRise(GameContext& game, const TopScoresRefresh& refresh = {});            // $4D

// Shared helpers.
void positionMusicTypeSprite(GameContext& game, bool playSfx);
void switchMusic(GameContext& game);
void updateDigitCursor(GameContext& game, std::size_t slot,
                       std::span<const SpriteCoordinate> coords, std::uint8_t index, bool playSfx);
void blinkCursor(GameContext& game, std::size_t slot);
void loadSceneSprites(SpriteRendererState& renderer, std::span<const SceneSprite> sprites);
void clearOamObjects(EngineState& engine);

// Install the eight handlers into their dispatch slots.
void installMenuScreenHandlers(GameStateDispatcher& dispatcher);

}
```

## Using them

Install the handlers on the dispatcher once, and the flow runs itself as the game state advances:

```cpp
kirpich::systems::GameStateDispatcher dispatcher;
kirpich::systems::installMenuScreenHandlers(dispatcher);
// ... each frame: dispatcher.tick(game, heldActions(inputState));
```

The config screen enters game-type selection; Confirm there moves to music selection, Start to the
chosen difficulty screen; a level/height picker begins the game (entering the init-game state `$0A`) or
steps back. The screens read the menu action vocabulary, which binds to the same buttons as the piece
controls (see [`input.md`](input.md)) — Confirm is the A button, Back is B, the directions move the
cursor.

Each difficulty screen takes an optional `TopScoresRefresh` — a `std::function<void(GameContext&)>`
called where the on-screen top-score table is restaged. It defaults to a no-op (the staged rows are
consumed by the renderer, so there is no simulation effect), and the installer binds one per mode:

```cpp
kirpich::systems::initTypeADifficultyScreen(game, [](GameContext& g) { /* refresh Type A top scores */ });
```

### The Type C screen picks two things

Type C is picked as a level **and** a rise, so it uses the two-box screen Type B uses, with the heading
changed to name Type C and the right-hand box labelled `rise` instead of `high`. Both words are four
letters and land in the same four cells, so the backdrop is otherwise the stored Type B screen, cell
for cell.

The level box is walked exactly as Type B's is. The rise box is not: a rise is a two-digit number where
a starting height is one digit, so its six values are drawn over the box by the render layer
([`rising-floor.md`](rising-floor.md), `src/render/type_c_difficulty.h`) and the current one is
highlighted rather than pointed at by a cursor sprite. `selectTypeCRise` therefore blinks
`ScreenUiState::cursorVisible` — the flag the port's own screens share — where the other pickers toggle
a cursor slot's visibility.

`GameFlowState::typeCRise` holds an index into `kTypeCRiseValues`, not the interval. Start or Confirm
from the rise picker begins the round; Back returns to the level picker.

## Gotchas

- **These handlers do not draw.** Tile and tilemap loads, turning the LCD on, and compiling the cursor
  slots into the display are the renderer's job. A handler only mutates the sprite slots, the object
  buffer, the audio cues, and the game-flow selections and state.
- **The blink shares the frame timer.** `blinkCursor` reloads `flow.timer1` to 16 and toggles the cursor
  only on the frames the timer reaches zero; the dispatcher's per-frame timer decrement drives the
  16-frame cadence. Calling a selection handler with a nonzero `timer1` holds the cursor steady — useful
  in a test that wants to isolate a transition from the blink.
- **`selectTypeALevel` does not unhide its cursor on exit.** Start, Confirm, and Back write the next
  state and leave the cursor as-is — an asymmetry the Type B pickers do not share (they unhide). It is
  faithful to the original and preserved.
- **The music value is a cursor tile, not an index.** `MusicType` holds the sprite tile ($1C–$1F);
  `switchMusic` subtracts $17 to get the song, and $1F ("off") maps to the stop-all cue. The music-type
  cursor moves as a 2×2 grid.
- **The difficulty inits cue the menu-move sound on entry.** Each enters `updateDigitCursor` at its top
  (which cues the sound), so `initTypeBDifficultyScreen` — placing two cursors — cues it as it seeds
  each. Whether the sound driver actually plays a cue is the audio system's concern.
- **`loadSceneSprites` always starts at slot 0** and hides the slot past the last (the terminator). All
  menu screens use slots 0 and 1 for their cursors.

## Changing behavior

- **A screen's input law** (which button does what, the grid geometry, the transitions) is that screen's
  handler; the shared cursor placement is `updateDigitCursor` / `positionMusicTypeSprite` and the shared
  blink is `blinkCursor`.
- **The cursor positions** are the coordinate tables in `src/data/misc.h`
  (`kTypeALevelCursorCoordinates`, `kTypeBLevelCursorCoordinates`, `kTypeBStartHeightCursorCoordinates`,
  and `musicTypeSpriteCoordinate`); the cursor sprites are the scene lists in `src/data/scene_sprites.h`.
- **The default button bindings** are `defaultActionMap` in `src/systems/input.cpp`; the menu actions
  themselves are in `include/kirpich/action.h`.
- **The song mapping** ($1C–$1F → a music cue) is `switchMusic`.

## Title and copyright screens

The states before the config screen: the copyright chain and the title screen. Same shape — free
functions on `GameContext`, installed on the dispatcher.

```cpp
namespace kirpich::systems {

// The seam the title screen fires when its attract countdown expires (the demo system fills it).
using StartDemoHook = std::function<void(GameContext&)>;

void initCopyrightScreen(GameContext& game);   // $24: seed the piece ring, arm the display timer
void copyrightHold(GameContext& game);         // $25: hold, then advance
void copyrightSkippable(GameContext& game);    // $35: any press or the timer advances to the title
void initTitleScreen(GameContext& game);       // $06: reset state, paint the title board, arm the countdown
void titleScreen(GameContext& game, const StartDemoHook& startDemo = {});  // $07

void installTitleScreenHandlers(GameStateDispatcher& dispatcher);

}
```

Install both handler sets on the dispatcher and the flow runs from power-on:

```cpp
kirpich::systems::installTitleScreenHandlers(dispatcher);
kirpich::systems::installMenuScreenHandlers(dispatcher);
// each frame: dispatcher.tick(game, heldActions(inputState));
```

The copyright screens show in sequence, then the title screen; one-player Start there enters the config
screen. To wire the attract demo, pass a `StartDemoHook` — but the installer registers `titleScreen`
with the default no-op hook, so the demo system installs its own handler for the title state when it
lands.

### Gotchas

- **The copyright screens are timed and skippable.** `initCopyrightScreen` arms a 250-frame timer;
  `copyrightHold` re-arms it and advances; `copyrightSkippable` advances on any newly-pressed input or
  the timer reaching zero. The timers count down through the dispatcher's per-frame decrement, the same
  as the cursor blink.
- **The piece ring is seeded with the 48 demo entries, and no more.** The original over-copies past the
  demo list; the port copies the 48 real entries and leaves the rest of the ring untouched. The tail is
  never read.
- **Heart mode is a non-zero flag.** Holding Down while pressing Start on the title screen sets
  `flow.heartMode` to a non-zero value; it is read only as zero / non-zero.
- **The two-player paths are not wired here.** The title screen's serial poll and its two-player Start
  are link-cable mechanism, left to the serial/multiplayer work; one-player Start, the cursor, and the
  attract countdown are complete. The demo launch is the `StartDemoHook` seam.
- **The 1P/2P cursor is the multiplayer flag.** `titleScreen` toggles `multiplayer.isMultiplayer` with
  Select, moves it one way with Right (1P→2P) and the other with Left (2P→1P), and places OAM object 0
  accordingly.

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^MenuScreens\.|^TitleScreens\.'
```
