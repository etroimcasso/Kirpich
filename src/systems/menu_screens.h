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
#include "state/sprite_renderer_state.h"
#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// The seam each difficulty screen calls where the original refreshes the on-screen top-score table
// (Update{TypeA,TypeB}TopScores). It has no simulation effect in this unit — the rows it stages are
// consumed by the renderer — so the default is a no-op; the top-score-entry unit installs the real
// refresh, and tests pass a probe to confirm the seam fires at the right point.
using TopScoresRefresh = std::function<void(GameContext&)>;

// ── Room for a third section ──────────────────────────────────────────────────────────────────────
//
// The config screen ships with two sections, game type and music type, and two blank rows between
// them. Moving the game-type box up a row and the music-type box down a row opens four, which is
// where a third one goes — a mode a player picks per round, alongside the two the cartridge offers.
//
// The section itself is drawn as SPRITES (src/render/config_section.h) and is never written into a
// map. A box in this screen's style is FIVE rows of art and the gap is four, so it is placed by pixel
// rather than by cell: half a row high, easing four pixels into the empty space above and below. A
// cell cannot do that — it sits on the grid and holds one tile.
//
// The layout below is the half of it the simulation owns, because moving the boxes moves the cursors
// that point into them.
//
// NOTHING INSTALLS THE SECTION TODAY. It is compiled and inert: no caller passes `showSection`, so it
// reads false everywhere, and nothing writes SELECT_MODE_OPTION, so its handler never runs. This is
// the SECTION only — the game-type grid below is a different thing and is live; see `showGrid`.
//
// A choice that wants the section wires four things:
//   1. a settings row that opens a mode screen (systems/mode_screen.h), for the enable;
//   2. a `showSection` seam passed to installMenuScreenHandlers, answering that enable;
//   3. three strings — a title and two labels — handed to render::configSectionSprites from the
//      render loop while the config screen is up;
//   4. whatever the choice means, read off ScreenUiState::modeOptionRight at the round's init.
//
// A choice between more than two game TYPES does not need any of it: that is the grid, which grows the
// game-type box into the same blank rows using background cells and the screen's own art.

// How far each box moves, in cells, and what that is worth in object coordinates — a cursor moves
// with the box it points into.
inline constexpr std::size_t  kConfigSectionShiftRows   = 1;
inline constexpr std::uint8_t kConfigSectionShiftPixels = 8;

// The rows the move opens, and the art that goes in them.
inline constexpr std::size_t kConfigSectionGapFirstRow = 6;
inline constexpr std::size_t kConfigSectionGapRows     = 4;
inline constexpr std::size_t kConfigSectionRows        = 5;

// Where the section's first row of art starts, in viewport pixels. Centred on the gap, which puts it
// half a row above the first blank row and half a row below the last.
inline constexpr int kConfigSectionTop =
    static_cast<int>(kConfigSectionGapFirstRow) * 8 -
    (static_cast<int>(kConfigSectionRows) - static_cast<int>(kConfigSectionGapRows)) * 8 / 2;

// Re-lay the config screen with room for the section: the game-type box a row up, the music-type box
// a row down, and the four rows between them left as the screen's own blank interior.
//
// The two boxes are copied from the stored screen rather than moved within the map, so the result is
// the same whether or not the map already held the vanilla layout.
void layOutConfigSection(BackgroundMap& map);

// ── State handlers ────────────────────────────────────────────────────────────────────────────────

// Three of these take `showSection`, read per frame through the installer's seam. With it false the
// screen and the walk are the cartridge's, cell for cell and state for state; with it true the screen
// grows the third section between its two boxes and the walk passes through it. It defaults to false,
// so a caller that wires nothing gets the cartridge's screen.

// GameState_08 — the config screen. Resets the serial hardware (link-cable mechanism the serial unit
// owns) and runs the shared body below.
void initConfigScreen(GameContext& game, bool showSection = false, bool showGrid = false);

// GameState_08 .loadTiles — the config-screen body: clear the object buffer, load the two cursors,
// place the music-type and game-type cursors, cue the music, and enter game-type selection. Split out
// because the demo-start and two-player paths enter here directly.
void loadConfigScreenBody(GameContext& game, bool showSection = false,
                          bool showGrid = false);

// GameState_0E — game-type selection. Left picks Type A, Right picks Type B; Confirm advances to the
// third section when there is one and to music selection otherwise, Start to the chosen difficulty
// screen.
void selectGameType(GameContext& game, bool showSection = false, bool showGrid = false);

// GameState_0F — music-type selection: a 2x2 grid over the four music choices. Start / Confirm advance
// like the game-type screen; Back steps back one section (one-player) or is inert (two-player).
void selectMusicType(GameContext& game, bool showSection = false);

// GameState_10 — init the Type A difficulty screen: load the level cursor, place it at the current
// level, refresh the top scores, and enter level selection (or name entry if a top score was earned).
void initTypeADifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_11 — Type A level selection: a 2x5 grid over levels 0-9. Start / Confirm begin the game;
// Back returns to the config screen (without unhiding the cursor — the Type B pickers differ).
void selectTypeALevel(GameContext& game, const TopScoresRefresh& refresh = {});

// INIT_TYPE_C_DIFFICULTY — init the Type C difficulty screen. The Type A screen's shape over Type C's
// own stored level: the same backdrop, the same single digit cursor, the same 2x5 grid.
void initTypeCDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});

// TYPE_C_LEVEL_SELECTION — Type C level selection: a 2x5 grid over levels 0-9, writing typeCLevel.
// Start / Confirm begin the round; Back returns to the config screen.
void selectTypeCLevel(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_12 — init the Type B difficulty screen: two cursors (level and starting garbage height),
// each placed at its current value; then enter level selection (or name entry).
void initTypeBDifficultyScreen(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_13 — Type B level selection: a 2x5 grid over levels 0-9. Start begins the game, Confirm
// advances to the height picker, Back returns to the config screen.
void selectTypeBLevel(GameContext& game, const TopScoresRefresh& refresh = {});

// GameState_14 — Type B starting-height selection: a 2x3 grid over heights 0-5. Start / Confirm begin
// the game; Back returns to level selection.
void selectTypeBHeight(GameContext& game, const TopScoresRefresh& refresh = {});

// SELECT_MODE_OPTION — the third section, which sits between the game-type and music-type ones. Left
// and Right move between its two choices (ScreenUiState::modeOptionRight); Confirm goes on to music
// selection and Back returns to game-type selection, the sections either side of it; Start begins the
// game, as it does from either neighbour. The chosen label blinks, which is why nothing here draws —
// the section is a sprite layer and the blink is a flag the bridge reads.
void selectModeOption(GameContext& game);

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
void clearOamObjects(GameContext& game);

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install the eight selection-screen handlers into their dispatch slots. The bare $09 slot keeps its
// default stub; the remaining pre-game states (title / copyright) are installed by their own unit.
//
// The two refresh seams are bound here because the five handlers that take one are wrapped in this
// call. Each difficulty screen refreshes its own game type's table, the way the original binds them:
// `typeA` goes to the Type A init and level picker, `typeB` to the Type B init, level picker and
// height picker. Both default to empty, so a build that installs only the menus still runs.
//
// `showSection` is asked once per frame by the three handlers that change with it. Absent reads as
// off, which is what leaves the cartridge's screen and its walk in place.
// `showGrid` is asked the same way, by the config screen and the game-type selector: with it on, the
// game-type box grows a second row of choices and the walk covers Type C. Absent reads as off, which
// leaves the cartridge's two-choice box and its left-right walk exactly as they are.
void installMenuScreenHandlers(GameStateDispatcher&           dispatcher,
                               const TopScoresRefresh&        typeA = {},
                               const TopScoresRefresh&        typeB = {},
                               const std::function<bool()>&   showSection = {},
                               const std::function<bool()>&   showGrid = {},
                               const TopScoresRefresh&        typeC = {});

}  // namespace kirpich::systems
