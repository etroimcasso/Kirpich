#pragma once

// What the controls mean to the game, and the two ways a player changes them.
//
// Each Game Boy button stands for a fixed set of the game's actions - the one table below - and the
// engine's action map is built from the player's bindings through it, so a rebound button carries its
// gameplay action and its menu action together. Rebinding goes through assignKey and assignPad, which
// keep every key and every controller button standing for one button at most: taking a source another
// button holds swaps the two.

#include <span>

#include <SDL3/SDL_scancode.h>

#include <kirpich/action.h>

#include <retropp/input.h>          // ActionSet, ControllerType
#include <retropp/input_actions.h>  // ActionMap, PadButton

#include "state/controls.h"

namespace kirpich::systems {

// The game's actions a Game Boy button stands for. Up only walks a menu - gameplay leaves it free -
// and Start and Select are their own actions; every other button carries a gameplay action and the
// menu action that shares its button.
[[nodiscard]] std::span<const Action> actionsFor(GbButton button) noexcept;

// The engine action map for a set of controls: for each button, its key and its controller button
// bound to every action the button stands for. Hand the result to the platform; a change to the
// controls is a new map handed over again.
[[nodiscard]] retropp::ActionMap actionMapFor(const Controls& controls);

// Every action the button bound to `key` stands for, or nothing when no button is. The fullscreen
// chord holds these back while Enter is down, so the chord never also presses whichever button Enter
// is.
[[nodiscard]] retropp::ActionSet actionsOnKey(const Controls& controls, SDL_Scancode key) noexcept;

// Bind `key` to `button`. Refuses the cancel key, leaving the controls as they were and returning
// false. A key another button holds is swapped: that button takes this one's old key.
bool assignKey(Controls& controls, GbButton button, SDL_Scancode key) noexcept;

// Bind the controller button at `position` to `button`, from a press on a pad of `family`.
//
// A press names where the button sits on the pad, while the defaults name buttons by their printed
// letter, and a letter sits in different places on different pads - so a letter and a position can be
// one button on one pad and two on another. Before binding, every lettered button in the controls is
// replaced by the position it has on the pad the press came from, so a rebound set names positions
// throughout and means the same physical buttons on every pad. A position another button holds is
// swapped, as with keys.
//
// Refuses a lettered `position` (a press is never one), leaving the controls as they were and
// returning false.
bool assignPad(Controls& controls, GbButton button, retropp::PadButton position,
               retropp::ControllerType family) noexcept;

}  // namespace kirpich::systems
