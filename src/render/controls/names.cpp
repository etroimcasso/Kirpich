#include "render/controls/names.h"

#include <array>
#include <string_view>

#include <SDL3/SDL_gamepad.h>

namespace kirpich::render {

namespace {

using retropp::ControllerType;
using retropp::PadButton;

struct KeyName {
    SDL_Scancode     key;
    std::string_view name;
};

// Every key with a name of its own, beyond the letters and digits.
constexpr std::array kKeyNames{
    KeyName{SDL_SCANCODE_RETURN, "enter"},     KeyName{SDL_SCANCODE_ESCAPE, "esc"},
    KeyName{SDL_SCANCODE_BACKSPACE, "bksp"},   KeyName{SDL_SCANCODE_TAB, "tab"},
    KeyName{SDL_SCANCODE_SPACE, "space"},      KeyName{SDL_SCANCODE_MINUS, "-"},
    KeyName{SDL_SCANCODE_PERIOD, "."},         KeyName{SDL_SCANCODE_UP, "up"},
    KeyName{SDL_SCANCODE_DOWN, "down"},        KeyName{SDL_SCANCODE_LEFT, "left"},
    KeyName{SDL_SCANCODE_RIGHT, "right"},      KeyName{SDL_SCANCODE_LSHIFT, "lshft"},
    KeyName{SDL_SCANCODE_RSHIFT, "rshft"},     KeyName{SDL_SCANCODE_LCTRL, "lctrl"},
    KeyName{SDL_SCANCODE_RCTRL, "rctrl"},      KeyName{SDL_SCANCODE_LALT, "lalt"},
    KeyName{SDL_SCANCODE_RALT, "ralt"},        KeyName{SDL_SCANCODE_LGUI, "lmeta"},
    KeyName{SDL_SCANCODE_RGUI, "rmeta"},       KeyName{SDL_SCANCODE_CAPSLOCK, "caps"},
    KeyName{SDL_SCANCODE_INSERT, "ins"},       KeyName{SDL_SCANCODE_DELETE, "del"},
    KeyName{SDL_SCANCODE_HOME, "home"},        KeyName{SDL_SCANCODE_END, "end"},
    KeyName{SDL_SCANCODE_PAGEUP, "pgup"},      KeyName{SDL_SCANCODE_PAGEDOWN, "pgdn"},
    KeyName{SDL_SCANCODE_KP_ENTER, "kpent"},   KeyName{SDL_SCANCODE_KP_PERIOD, "kp."},
    KeyName{SDL_SCANCODE_KP_MINUS, "kp-"},     KeyName{SDL_SCANCODE_KP_PLUS, "kpadd"},
    KeyName{SDL_SCANCODE_KP_MULTIPLY, "kpmul"}, KeyName{SDL_SCANCODE_KP_DIVIDE, "kpdiv"},
};

// The digits in the order SDL numbers their keys, on the main row and on the keypad alike: 1 to 9,
// then 0.
constexpr std::string_view kDigitsInKeyOrder = "1234567890";

// The four letters a face button can be printed with, each with the alias that names it.
struct FaceLetter {
    PadButton        alias;
    std::string_view letter;
};

constexpr std::array kFaceLetters{
    FaceLetter{PadButton::FaceLabelA, "a"},
    FaceLetter{PadButton::FaceLabelB, "b"},
    FaceLetter{PadButton::FaceLabelX, "x"},
    FaceLetter{PadButton::FaceLabelY, "y"},
};

// A name that differs by family: the Xbox name, which pads without printed names share, then
// PlayStation's, then Nintendo's.
struct FamilyNames {
    std::string_view xbox;
    std::string_view playStation;
    std::string_view nintendo;

    [[nodiscard]] std::string_view on(ControllerType family) const {
        switch (family) {
            case ControllerType::PlayStation: return playStation;
            case ControllerType::Nintendo:    return nintendo;
            case ControllerType::Unknown:
            case ControllerType::Xbox:
            case ControllerType::Standard:    return xbox;
        }
        return xbox;
    }
};

// The letter printed at a face position on `family`: the letter whose alias reads the same SDL button
// there. The engine holds the per-family layout, so this asks it rather than restating it.
std::string_view faceLetterAt(PadButton position, ControllerType family) {
    const SDL_GamepadButton at = retropp::resolvePadButton(position, family);
    for (const FaceLetter& face : kFaceLetters) {
        if (retropp::resolvePadButton(face.alias, family) == at) {
            return face.letter;
        }
    }
    return {};
}

}  // namespace

std::string keyName(SDL_Scancode key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        return std::string(1, static_cast<char>('a' + (key - SDL_SCANCODE_A)));
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) {
        return std::string(1, kDigitsInKeyOrder[key - SDL_SCANCODE_1]);
    }
    if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_0) {
        return "kp" + std::string(1, kDigitsInKeyOrder[key - SDL_SCANCODE_KP_1]);
    }
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) {
        return "f" + std::to_string(key - SDL_SCANCODE_F1 + 1);
    }
    for (const KeyName& named : kKeyNames) {
        if (named.key == key) {
            return std::string(named.name);
        }
    }
    return "k" + std::to_string(static_cast<int>(key));
}

std::string padButtonName(PadButton button, ControllerType family) {
    switch (button) {
        case PadButton::FaceSouth:
        case PadButton::FaceEast:
        case PadButton::FaceWest:
        case PadButton::FaceNorth:       return std::string(faceLetterAt(button, family));
        case PadButton::FaceLabelA:      return "a";
        case PadButton::FaceLabelB:      return "b";
        case PadButton::FaceLabelX:      return "x";
        case PadButton::FaceLabelY:      return "y";
        case PadButton::DpadUp:          return "up";
        case PadButton::DpadDown:        return "down";
        case PadButton::DpadLeft:        return "left";
        case PadButton::DpadRight:       return "right";
        case PadButton::ShoulderL:       return std::string(FamilyNames{"lb", "l1", "l"}.on(family));
        case PadButton::ShoulderR:       return std::string(FamilyNames{"rb", "r1", "r"}.on(family));
        case PadButton::TriggerL:        return std::string(FamilyNames{"lt", "l2", "zl"}.on(family));
        case PadButton::TriggerR:        return std::string(FamilyNames{"rt", "r2", "zr"}.on(family));
        case PadButton::StickClickL:     return std::string(FamilyNames{"ls", "l3", "ls"}.on(family));
        case PadButton::StickClickR:     return std::string(FamilyNames{"rs", "r3", "rs"}.on(family));
        case PadButton::Start:           return "start";
        case PadButton::Select:          return "selct";
        case PadButton::Guide:           return "home";
        case PadButton::Share:           return "share";
        case PadButton::LeftStickUp:     return "lsup";
        case PadButton::LeftStickDown:   return "lsdn";
        case PadButton::LeftStickLeft:   return "lslft";
        case PadButton::LeftStickRight:  return "lsrgt";
        case PadButton::RightStickUp:    return "rsup";
        case PadButton::RightStickDown:  return "rsdn";
        case PadButton::RightStickLeft:  return "rslft";
        case PadButton::RightStickRight: return "rsrgt";
    }
    return {};
}

}  // namespace kirpich::render
