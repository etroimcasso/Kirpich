# Menu screens

The pre-game selection flow: the config screen, the game-type and music-type selectors, and the
Type A / Type B difficulty pickers a player moves through before a round starts. The behavioral
specification — what the original game does, line by line — is in
[`../contracts/menu-screens.md`](../contracts/menu-screens.md); the design rationale is in
[`../features/menu-screens.md`](../features/menu-screens.md).

## Where it lives

| File | Holds |
|---|---|
| `src/systems/menu_screens.h` / `.cpp` | The `kirpich::systems` selection-screen handlers, their shared helpers, and the installer. |
| `include/kirpich/action.h` | The six menu actions (`MenuUp`/`MenuDown`/`MenuLeft`/`MenuRight`/`Confirm`/`Back`). |
| `src/systems/input.cpp` | Their default bindings and their place in the held-action walk. |
| `tests/test_menu_screens.cpp` | The behavioral tests. |

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

The two difficulty screens take an optional `TopScoresRefresh` — a `std::function<void(GameContext&)>`
called where the original refreshes the on-screen top-score table. It defaults to a no-op (the staged
rows are consumed by the renderer, so there is no simulation effect), and the top-score-entry screen
wires the real refresh when it lands:

```cpp
kirpich::systems::initTypeADifficultyScreen(game, [](GameContext& g) { /* refresh Type A top scores */ });
```

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

## Build and test

```
cmake --build build --parallel
ctest --test-dir build -R '^MenuScreens\.'
```
