#pragma once

// What the Controls screen writes in a cell: the name of a key, or of a controller button.
//
// A name is lowercase and at most kControlsNameWidth characters, so it fits the five cells each
// column gives it, and is spelled from what the font draws - letters, digits, a period and a hyphen
// (render/glyphs.h).

#include <cstddef>
#include <string>

#include <SDL3/SDL_scancode.h>

#include <retropp/input.h>          // ControllerType
#include <retropp/input_actions.h>  // PadButton

namespace kirpich::render {

// The widest name either column holds.
inline constexpr std::size_t kControlsNameWidth = 5;

// A key's name. Letters and digits are themselves; the keys a player is likely to bind have short
// names (`enter`, `bksp`, `space`, `lshft`, `f1`, `kp0` and the like); any other key is `k` followed by
// its scancode in decimal. The font has no other punctuation, which is why the comma, slash and bracket
// keys take that last form.
[[nodiscard]] std::string keyName(SDL_Scancode key);

// A controller button's name on a pad of `family`.
//
// A face button bound by position is named for the letter printed there on that pad, so the east
// button reads `a` on a Nintendo pad and `b` on an Xbox pad. PlayStation and other pads without
// printed letters take the Xbox letters. A face button bound by its letter is that letter on every pad.
// Shoulders, triggers and stick clicks take each family's printed names (`lb` / `l1` / `l`), the
// d-pad and the stick directions are named for their direction, and Start, Select, the guide button
// and Share have one name each (`start`, `selct`, `home`, `share`).
[[nodiscard]] std::string padButtonName(retropp::PadButton button, retropp::ControllerType family);

}  // namespace kirpich::render
