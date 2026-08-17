#pragma once

// The copyright and title screens: the states a player sees from power-on until they pick a mode. Each
// screen is one game state — the frame dispatcher runs its handler once per frame — so these are free
// functions on GameContext, the same shape as the menu, piece, line-clear, and scoring systems; they
// own no state of their own.
//
// The flow: the copyright chain (initCopyrightScreen -> copyrightHold -> copyrightSkippable) shows the
// timed copyright notices, which any button press or the timer skips; it hands off to the title screen
// (initTitleScreen -> titleScreen). The title screen runs an attract countdown that launches a demo when
// it expires, and a one/two-player cursor; pressing Start in one-player enters the config screen
// (initConfigScreen in systems/menu_screens.h). The exact per-screen effects, the field clears, the
// board paint, the input laws, and the transitions — with source line anchors — are in
// docs/contracts/title-screens.md.
//
// What these do NOT do: draw anything. Loading tiles and tilemaps, turning the LCD on, and compiling the
// sprite slots into the display are the renderer's job; the handlers mutate the board, the sprite-object
// slots, the object buffer, the audio cue mailbox, and the game-flow selections, timers, and state. The
// demo launch and the link-cable serial paths are seams other systems fill (see StartDemoHook below and
// the contract).

#include <functional>

#include "systems/game_context.h"

namespace kirpich::systems {

class GameStateDispatcher;

// The seam the title screen fires when its attract countdown reaches zero, where the original launches an
// attract demo (StartDemo). The default is a no-op — a build without the demo system idles at the title —
// and the demo system installs the real launch; tests pass a probe to confirm the seam fires at the right
// point.
using StartDemoHook = std::function<void(GameContext&)>;

// ── State handlers ──────────────────────────────────────────────────────────────────────────────────

// GameState_24 — init the copyright screen: clear the object buffer, seed the piece ring from the demo
// list, arm the display timer, and enter the copyright hold.
void initCopyrightScreen(GameContext& game);

// GameState_25 — hold the copyright screen for the display timer, then advance to the skippable copyright
// screen.
void copyrightHold(GameContext& game);

// GameState_35 — the skippable copyright screen: any newly-pressed input, or the display timer expiring,
// advances to the title-screen init.
void copyrightSkippable(GameContext& game);

// GameState_06 — init the title screen: reset leftover game state from a prior round, clear the score and
// line-clear tallies, paint the title board (walls and floor), place the one/two-player cursor, cue the
// title music, and arm the attract countdown.
void initTitleScreen(GameContext& game);

// GameState_07 — the title screen: run the attract countdown (firing startDemo when it expires), and the
// one/two-player cursor. Select toggles the cursor; Left/Right move it; Start in one-player enters the
// config screen. The two-player serial paths are deferred (see the contract).
void titleScreen(GameContext& game, const StartDemoHook& startDemo = {});

// ── Installer ─────────────────────────────────────────────────────────────────────────────────────

// Install the five copyright / title handlers into their dispatch slots ($24, $25, $35, $06, $07).
void installTitleScreenHandlers(GameStateDispatcher& dispatcher);

}  // namespace kirpich::systems
