#include "systems/controls.h"

#include <array>
#include <cstddef>
#include <optional>

namespace kirpich::systems {

namespace {

constexpr std::array kUpActions     = {Action::MenuUp};
constexpr std::array kDownActions   = {Action::SoftDrop, Action::MenuDown};
constexpr std::array kLeftActions   = {Action::MoveLeft, Action::MenuLeft};
constexpr std::array kRightActions  = {Action::MoveRight, Action::MenuRight};
constexpr std::array kAActions      = {Action::RotateClockwise, Action::Confirm};
constexpr std::array kBActions      = {Action::RotateCounterClockwise, Action::Back};
constexpr std::array kStartActions  = {Action::Start};
constexpr std::array kSelectActions = {Action::Select};

// The four printed-letter names, which resolve to a position per pad family.
bool isLettered(retropp::PadButton pad) noexcept {
    return pad == retropp::PadButton::FaceLabelA || pad == retropp::PadButton::FaceLabelB ||
           pad == retropp::PadButton::FaceLabelX || pad == retropp::PadButton::FaceLabelY;
}

// Where a lettered button sits on a pad of `family`. A pad with no recognised family is laid out as
// the generic pad is.
retropp::PadButton positionOf(retropp::PadButton lettered, retropp::ControllerType family) noexcept {
    if (family == retropp::ControllerType::Unknown) family = retropp::ControllerType::Standard;
    const std::optional<retropp::PadButton> position =
        retropp::padButtonFrom(retropp::resolvePadButton(lettered, family));
    return position.value_or(lettered);
}

}  // namespace

std::span<const Action> actionsFor(GbButton button) noexcept {
    switch (button) {
        case GbButton::UP:     return kUpActions;
        case GbButton::DOWN:   return kDownActions;
        case GbButton::LEFT:   return kLeftActions;
        case GbButton::RIGHT:  return kRightActions;
        case GbButton::A:      return kAActions;
        case GbButton::B:      return kBActions;
        case GbButton::START:  return kStartActions;
        case GbButton::SELECT: return kSelectActions;
    }
    return {};
}

retropp::ActionMap actionMapFor(const Controls& controls) {
    retropp::ActionMap map;
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        const auto           button  = static_cast<GbButton>(i);
        const ButtonBinding& binding = controls[button];
        for (const Action action : actionsFor(button)) {
            map.bind(action, binding.key);
            map.bind(action, binding.pad);
        }
    }
    return map;
}

retropp::ActionSet actionsOnKey(const Controls& controls, SDL_Scancode key) noexcept {
    retropp::ActionSet set;
    for (std::size_t i = 0; i < kGbButtonCount; ++i) {
        if (controls.buttons[i].key != key) continue;
        for (const Action action : actionsFor(static_cast<GbButton>(i))) {
            set.set(retropp::actionId(action), true);
        }
    }
    return set;
}

bool assignKey(Controls& controls, GbButton button, SDL_Scancode key) noexcept {
    if (key == kCancelKey) return false;

    SDL_Scancode& mine = controls[button].key;
    for (ButtonBinding& other : controls.buttons) {
        if (&other.key != &mine && other.key == key) {
            other.key = mine;
            break;
        }
    }
    mine = key;
    return true;
}

bool assignPad(Controls& controls, GbButton button, retropp::PadButton position,
               retropp::ControllerType family) noexcept {
    if (isLettered(position)) return false;

    for (ButtonBinding& binding : controls.buttons) {
        if (isLettered(binding.pad)) binding.pad = positionOf(binding.pad, family);
    }

    retropp::PadButton& mine = controls[button].pad;
    for (ButtonBinding& other : controls.buttons) {
        if (&other.pad != &mine && other.pad == position) {
            other.pad = mine;
            break;
        }
    }
    mine = position;
    return true;
}

}  // namespace kirpich::systems
