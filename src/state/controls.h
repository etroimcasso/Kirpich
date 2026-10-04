#pragma once

// The player's controls: which keyboard key and which controller button stands for each of the Game
// Boy's eight buttons, and the two calls that keep them across launches.
//
// A binding is held per Game Boy button rather than per game action. The game's actions share their
// buttons - A both rotates clockwise and confirms a menu choice, Down both drops the piece and walks a
// menu cursor - so binding the button keeps those pairs together; the action map the engine samples is
// derived from these eight (src/systems/controls.h).
//
// Like the settings, the controls belong to the player rather than to the machine's state image: they
// survive the reset chord and outlive a launch, so they live outside GameContext. They persist in
// their own save document beside the settings and under the same identity.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <SDL3/SDL_scancode.h>

#include <retropp/input_actions.h>  // PadButton
#include <retropp/save_store.h>

namespace kirpich {

// The Game Boy's eight buttons, in the order the controls screen lists them and the save document
// records them.
enum class GbButton : std::uint8_t {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    A,
    B,
    START,
    SELECT,
};

// How many buttons there are. Tied to the last enumerator so the two cannot drift.
inline constexpr std::size_t kGbButtonCount = static_cast<std::size_t>(GbButton::SELECT) + 1;

// One button's two sources: the keyboard key and the controller button that press it. Both are the
// types an engine action-map row takes, so a binding goes into the map as it is stored.
struct ButtonBinding {
    SDL_Scancode       key = SDL_SCANCODE_UNKNOWN;
    retropp::PadButton pad = retropp::PadButton::FaceSouth;

    friend constexpr bool operator==(const ButtonBinding&, const ButtonBinding&) = default;
};

struct Controls {
    std::array<ButtonBinding, kGbButtonCount> buttons{};

    [[nodiscard]] constexpr ButtonBinding& operator[](GbButton button) noexcept {
        return buttons[static_cast<std::size_t>(button)];
    }
    [[nodiscard]] constexpr const ButtonBinding& operator[](GbButton button) const noexcept {
        return buttons[static_cast<std::size_t>(button)];
    }

    friend constexpr bool operator==(const Controls&, const Controls&) = default;
};

// The controls a player gets until they change them. The keyboard follows the emulator convention -
// the arrows, Z and X for B and A, Enter and Backspace for Start and Select. The two face buttons bind
// by their printed letter, so the controller's A is the Game Boy's A on every pad family.
inline constexpr Controls kDefaultControls{{{
    {.key = SDL_SCANCODE_UP, .pad = retropp::PadButton::DpadUp},
    {.key = SDL_SCANCODE_DOWN, .pad = retropp::PadButton::DpadDown},
    {.key = SDL_SCANCODE_LEFT, .pad = retropp::PadButton::DpadLeft},
    {.key = SDL_SCANCODE_RIGHT, .pad = retropp::PadButton::DpadRight},
    {.key = SDL_SCANCODE_X, .pad = retropp::PadButton::FaceLabelA},
    {.key = SDL_SCANCODE_Z, .pad = retropp::PadButton::FaceLabelB},
    {.key = SDL_SCANCODE_RETURN, .pad = retropp::PadButton::Start},
    {.key = SDL_SCANCODE_BACKSPACE, .pad = retropp::PadButton::Select},
}}};

// The key that backs out of choosing a new binding. It can never be bound to a button, because a
// player pressing it while the controls screen waits for a press means "leave it as it was".
inline constexpr SDL_Scancode kCancelKey = SDL_SCANCODE_ESCAPE;

// The controls save document: schema version and image size. The name is spelled as a literal at the
// call sites, as the other documents' are.
//
// Eight records in GbButton order, three bytes each: the key as a little-endian 16-bit value, then the
// controller button.
inline constexpr std::uint32_t kControlsSchemaVersion = 1;
inline constexpr std::size_t   kControlsRecordBytes   = 3;
inline constexpr std::size_t   kControlsImageBytes    = kGbButtonCount * kControlsRecordBytes;

[[nodiscard]] std::array<std::uint8_t, kControlsImageBytes> encodeControls(const Controls& controls);

// Decode an image into `controls`. Returns false and leaves `controls` untouched unless the whole image
// is a set a player could have made: exactly the right length, every key a real key and not the cancel
// key, every controller button one the engine names, and no key or controller button bound to two
// buttons. A partial set is refused rather than read as far as it goes, because a button left without
// a binding is a button the player cannot press.
[[nodiscard]] bool decodeControls(std::span<const std::uint8_t> image, Controls& controls);

// Persist the controls as document "controls" at the current schema version. Returns whatever the
// atomic write reports.
bool saveControls(const Controls& controls, retropp::SaveStore& store);

// Load the controls from the store. Absent document (ordinary first run) -> leave the defaults, return
// false. Present and valid -> decode, return true. Corrupt or refused -> log an error, leave the
// defaults, leave the file in place, return false.
//
// Declares this document's schema version on the store immediately before reading, because the
// version is the store's rather than the document's (src/state/settings.h says the same).
bool loadControls(retropp::SaveStore& store, Controls& controls);

}  // namespace kirpich
