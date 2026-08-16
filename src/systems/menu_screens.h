#pragma once

// The pre-game selection screens: the config screen and the game-type, music-type, and difficulty
// pickers a player moves through before a round starts. Each screen is one game state — the frame
// dispatcher runs its handler once per frame — so these are free functions on GameContext, the same
// shape as the piece, line-clear, and scoring systems; they own no state of their own.
//
// The flow: the config screen (initConfigScreen) lays out the game-type and music-type cursors and
// hands off to game-type selection (selectGameType); Confirm there moves to music selection
// (selectMusicType), Start to the chosen difficulty screen. The Type A difficulty screen
// (initTypeADifficultyScreen -> selectTypeALevel) picks a starting level; the Type B one
// (initTypeBDifficultyScreen -> selectTypeBLevel -> selectTypeBHeight) picks a level and a starting
// garbage height. From a level/height picker, Start (and Confirm, on Type B) begins the game by
// entering the init-game state, and Back steps back through the flow. A handful of shared helpers do
// the repeated work: placing and blinking a cursor, loading a screen's cursor sprites, clearing the
// object buffer, and turning a music-type choice into an audio cue. The exact per-screen input laws,
// grid geometry, cursor placement, and transitions — with source line anchors — are in
// docs/contracts/menu-screens.md.
//
// What these do NOT do: draw anything. Loading tiles and tilemaps, turning the screen on, and
// compiling the cursor slots into the display are the renderer's job; the handlers only mutate the
// sprite-object slots, the object buffer, the audio cue mailbox, and the game-flow selections and
// state. The top-score refresh each difficulty screen performs is a seam (TopScoresRefresh) the
// top-score-entry unit wires later.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include <kirpich/game_state.h>

#include "data/misc.h"           // SpriteCoordinate
#include "data/scene_sprites.h"  // SceneSprite
#include "state/engine_state.h"
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// The seam each difficulty screen calls where the original refreshes the on-screen top-score table
// (Update{TypeA,TypeB}TopScores). It has no simulation effect in this unit — the rows it stages are
// consumed by the renderer — so the default is a no-op; the top-score-entry unit installs the real
// refresh, and tests pass a probe to confirm the seam fires at the right point.
using TopScoresRefresh = std::function<void(GameContext&)>;

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// GameState_08 — the config screen. Resets the serial hardware (link-cable mechanism the serial unit
// owns) and runs the shared body below.
void initConfigScreen(GameContext& game);

// GameState_08 .loadTiles — the config-screen body: clear the object buffer, load the two cursors,
// place the music-type and game-type cursors, cue the music, and enter game-type selection. Split out
// because the demo-start and two-player paths enter here directly.
void loadConfigScreenBody(GameContext& game);

// GameState_0E — game-type selection. Left picks Type A, Right picks Type B; Confirm advances to music
// selection, Start to the chosen difficulty screen.
void selectGameType(GameContext& game);

// GameState_0F — music-type selection: a 2x2 grid over the four music choices. Start / Confirm advance
// like the game-type screen; Back returns to game-type selection (one-player) or is inert (two-player).
void selectMusicType(GameContext& game);

// GameState_10 — init the Type A difficulty screen: load the level cursor, place it at the current
// level, refresh the top scores, and enter level selection (or name entry if a top score was earned).
void initTypeADifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_11 — Type A level selection: a 2x5 grid over levels 0-9. Start / Confirm begin the game;
// Back returns to the config screen (without unhiding the cursor — the Type B pickers differ).
void selectTypeALevel(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_12 — init the Type B difficulty screen: two cursors (level and starting garbage height),
// each placed at its current value; then enter level selection (or name entry).
void initTypeBDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_13 — Type B level selection: a 2x5 grid over levels 0-9. Start begins the game, Confirm
// advances to the height picker, Back returns to the config screen.
void selectTypeBLevel(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_14 — Type B starting-height selection: a 2x3 grid over heights 0-5. Start / Confirm begin
// the game; Back returns to level selection.
void selectTypeBHeight(GameContext& game, const TopScoresRefresh& refresh = {});

// ── Shared helpers ──────────────────────────────────────────────────────────────────────────────

// PositionMusicTypeSprite — place the music-type cursor (slot 0) at the coordinate for the current
// music type and set its sprite to the music-type tile. playSfx cues the menu-move sound (the config
// and selection screens pass true; the two-player init uses the no-cue entry).
void positionMusicTypeSprite(GameContext& game, bool playSfx);

// SwitchMusic — map the music-type cursor value to a song cue (or the stop-all cue for "music off").
void switchMusic(GameContext& game);

// UpdateDigitCursor — move a digit cursor (the given slot) to the coordinate for `index` and set its
// sprite to that digit. playSfx cues the menu-move sound (every selection and init caller passes true).
void updateDigitCursor(GameContext& game, std::size_t slot,
                       std::span<const SpriteCoordinate> coords, std::uint8_t index, bool playSfx);

// The blink half of ReadJoypadAndBlinkCursor: on the frames the frame timer reaches zero, toggle the
// cursor slot's visibility and reload the 16-frame blink interval. (The pressed snapshot the original
// reads in the same routine already lives on game.joypad.)
void blinkCursor(GameContext& game, std::size_t slot);

// LoadSprites — copy each scene record into consecutive sprite slots from slot 0, then hide the slot
// past the last (the terminator). All menu callers start at slot 0.
void loadSceneSprites(SpriteRendererState& renderer, std::span<const SceneSprite> sprites);

// ClearObjects — zero the whole 40-entry OAM staging buffer.
void clearOamObjects(EngineState& engine);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install the eight selection-screen handlers into their dispatch slots. The bare $09 slot keeps its
// default stub; the remaining pre-game states (title / copyright) are installed by their own unit.
void installMenuScreenHandlers(GameStateDispatcher& dispatcher);

}  // namespace kirpich::systems
