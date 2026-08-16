#include "systems/input.h"

#include <array>
#include <cstdint>

#include <SDL3/SDL_scancode.h>

#include <kirpich/action.h>

#include "retropp/input.h"
#include "retropp/input_actions.h"

namespace kirpich::systems {

JoypadState InputSystem::sample(retropp::ActionSet heldNow) {
    // pressed = heldNow & ~prevHeld_, per action bit — the original's xor/and edge
    // (ReadJoypad, tetris.asm:6546-6549) generalized over the action set.
    retropp::ActionSet pressed;
    for (int a = 0; a < retropp::kMaxActions; ++a) {
        const auto id = static_cast<retropp::ActionId>(a);
        pressed.set(id, heldNow.test(id) && !prevHeld_.test(id));
    }
    prevHeld_ = heldNow;
    return JoypadState{.held = heldNow, .pressed = pressed};
}

void InputSystem::reset() { prevHeld_ = retropp::ActionSet{}; }

bool keyRepeatFire(std::uint8_t& timer, bool pressed, bool held) {
    if (pressed) {
        timer = kKeyRepeatInitialDelay;
        return true;
    }
    if (held) {
        --timer;  // uint8 wraparound: 0 -> 255 (dec a / ret nz — a stale-timer 255-frame delay)
        if (timer == 0) {
            timer = kKeyRepeatRate;
            return true;
        }
        return false;
    }
    return false;
}

retropp::ActionSet heldActions(const retropp::InputState& in) {
    // Every game action, walked explicitly (the enum carries no count sentinel).
    constexpr std::array kActions = {
        Action::MoveLeft,
        Action::MoveRight,
        Action::SoftDrop,
        Action::RotateClockwise,
        Action::RotateCounterClockwise,
        Action::Start,
        Action::Select,
        Action::MenuUp,
        Action::MenuDown,
        Action::MenuLeft,
        Action::MenuRight,
        Action::Confirm,
        Action::Back,
    };
    retropp::ActionSet held;
    for (const Action a : kActions) {
        held.set(retropp::actionId(a), in.isHeld(a));
    }
    return held;
}

retropp::ActionMap defaultActionMap() {
    return retropp::ActionMap{
        {Action::MoveLeft, {SDL_SCANCODE_LEFT, retropp::PadButton::DpadLeft}},
        {Action::MoveRight, {SDL_SCANCODE_RIGHT, retropp::PadButton::DpadRight}},
        {Action::SoftDrop, {SDL_SCANCODE_DOWN, retropp::PadButton::DpadDown}},
        {Action::RotateClockwise, {SDL_SCANCODE_X, retropp::PadButton::FaceLabelA}},
        {Action::RotateCounterClockwise, {SDL_SCANCODE_Z, retropp::PadButton::FaceLabelB}},
        {Action::Start, {SDL_SCANCODE_RETURN, retropp::PadButton::Start}},
        {Action::Select, {SDL_SCANCODE_RSHIFT, retropp::PadButton::Select}},

        // Menu navigation shares its gameplay counterpart's sources: the directions with the
        // movement / soft-drop keys and d-pad, Confirm with rotate-clockwise (X / GB A), Back with
        // rotate-counter-clockwise (Z / GB B). MenuUp takes the up direction, which gameplay leaves free.
        {Action::MenuUp, {SDL_SCANCODE_UP, retropp::PadButton::DpadUp}},
        {Action::MenuDown, {SDL_SCANCODE_DOWN, retropp::PadButton::DpadDown}},
        {Action::MenuLeft, {SDL_SCANCODE_LEFT, retropp::PadButton::DpadLeft}},
        {Action::MenuRight, {SDL_SCANCODE_RIGHT, retropp::PadButton::DpadRight}},
        {Action::Confirm, {SDL_SCANCODE_X, retropp::PadButton::FaceLabelA}},
        {Action::Back, {SDL_SCANCODE_Z, retropp::PadButton::FaceLabelB}},
    };
}

}  // namespace kirpich::systems
