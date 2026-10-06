#pragma once

// The Controls screen's logic: which cell the cursor is on, when the screen waits for a press, and what
// a captured press does to the player's bindings. It draws nothing and names no drawing type - the
// picture is a function of this state and of the bindings, built by the components under
// src/render/controls/.
//
// The screen opens from the settings screen's Controls row. It lists the Game Boy's eight buttons in
// rows, with a keyboard column and a controller column, and under them a row that restores the
// defaults. The arrows walk the cells, stopping at every edge. A or Start on a cell waits for a press
// of that cell's kind - a key on the keyboard column, a controller button on the controller column -
// and binds it to the row's button through assignKey or assignPad, so a source another button holds
// swaps with it. Escape while waiting leaves the binding as it was. A or Start on the restore row asks
// first, the way the settings screen's reset rows do, and "yes" puts every binding back to
// kDefaultControls. B goes back to the settings screen with its cursor still on the Controls row.
//
// A change is put into effect and written out as it is made: the host hands the engine the action map
// the new bindings derive, and saves them.
//
// Input is a dispatch table mapping an action to what it does, so what a button does is data a reader
// sees in one place.

#include <functional>
#include <optional>

#include <retropp/input_actions.h>  // CapturedSource

#include "state/controls.h"
#include "systems/game_context.h"
#include "systems/settings_screen.h"  // SettingsWiring

namespace kirpich::systems {

class GameStateDispatcher;

// Everything the Controls screen needs from outside the game state.
//
// `controls` is the live value the screen edits - the host owns it, because it outlives a reset and is
// saved to disk. `listen` asks the platform to capture the next press, and `captured` reads the press
// it caught, or nothing while it waits (SdlPlatform::captureRequest / capturedSource). `apply` puts a
// set of bindings into effect and `save` writes it out; both fire on every binding, in that order.
//
// Every seam defaults to inert, so a build that installs only the screen still runs. Without
// `controls` behind it, the screen treats every captured press as a cancel.
struct ControlsWiring {
    Controls*                                             controls = nullptr;
    std::function<void()>                                 listen;
    std::function<std::optional<retropp::CapturedSource>()> captured;
    std::function<void(const Controls&)>                  apply;
    std::function<void(const Controls&)>                  save;
};

// Enter the screen: the cursor on the keyboard cell of the Up row, shown, with the shared blink armed,
// and not waiting for a press. It writes no background map, saves no caller and empties no object
// buffer - the picture is the screen's components, and the settings screen it was opened from repaints
// itself when the player comes back.
void openControlsSettings(GameContext& game);

// One frame of the screen, in this order:
//
// - **Waiting for a press:** read the captured press and nothing else - no action of the screen's own
//   runs while it waits. Nothing captured yet keeps it waiting. Escape, in either column, ends the wait
//   with the bindings untouched. A key on the keyboard column, or a controller button on the controller
//   column, is bound to the row's button, put into effect and saved, with a menu-move cue. Any other
//   press - a controller button on the keyboard column, a key on the controller column, a mouse button -
//   asks the platform for another press and keeps waiting.
// - **Just finished waiting, or just restored:** ignore input until nothing is held. A change to the
//   bindings changes what a held key means, and the game reads an action that is held now but was not
//   on the last frame as a press, so without this the key the player bound would act on the screen the
//   moment they bound it - on the A row it would start waiting again.
// - **Asking whether to restore:** blink the cursor, then run the question's table. Left and Right
//   move between "no" and "yes" with a menu-move cue, an edge saying nothing. A or Start answers: "yes"
//   puts every binding back to the defaults, put into effect and saved, then holds input until it is let
//   go (as above); "no" changes nothing. B is "no". Leaving the question cues the screen change.
// - **Otherwise:** blink the cursor, then run the input table. The arrows walk the cells with a
//   menu-move cue on a move and nothing at an edge; on the restore row, which is one item, Left and
//   Right are edges. A or Start starts waiting for a press on the cell the cursor is on, or, on the
//   restore row, opens the question on "no" with the screen-change cue - unless the bindings already
//   are the defaults, which leaves nothing to restore: no question, nothing cued. B returns to the
//   settings screen.
void controlsScreen(GameContext& game, const SettingsWiring& settings, const ControlsWiring& controls);

// Install the screen's one handler into the CONTROLS_SETTINGS slot.
void installControlsScreen(GameStateDispatcher& dispatcher, SettingsWiring settings,
                           ControlsWiring controls);

}  // namespace kirpich::systems
