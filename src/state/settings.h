#pragma once

// The player's display settings, and the two calls that keep them across launches.
//
// Two values: whether the game runs fullscreen, and how many screen pixels one Game Boy pixel is
// drawn as. Neither is game state — nothing in a round reads them, and they survive a reset — so
// they live outside GameContext and travel to the settings screen through its wiring.
//
// They persist in their own save document, beside the top scores and under the same identity. The
// document is two bytes: the fullscreen flag, then the scale. An absent document is an ordinary
// first run and leaves the defaults; a corrupt one is reported, leaves the defaults, and leaves the
// damaged file where it is.
//
// A scale outside the range this build offers is clamped rather than refused. A file written by a
// build that offered more sizes should cost the player that one value, not every value beside it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <retropp/save_store.h>

namespace kirpich {

// The window scales the settings screen offers, in screen pixels per Game Boy pixel. The default is
// the engine's own (EnhancementToggles::windowScale), so a player who never opens the screen gets
// the window the engine would have opened anyway.
inline constexpr std::uint8_t kMinWindowScale = 1;
inline constexpr std::uint8_t kMaxWindowScale = 8;
inline constexpr std::uint8_t kDefaultWindowScale = 4;

struct Settings {
    bool         fullscreen  = false;
    std::uint8_t windowScale = kDefaultWindowScale;

    // Which shade ramp the game is drawn in (src/render/palettes.h). 0 is the greyscale the hardware's
    // own shades map to, so a player who never touches it sees what they always saw.
    std::uint8_t shadeRamp = 0;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// The settings save document: schema version and the image size. The name is spelled as a literal at
// the call sites, as the top-score document's is.
//
// A shorter image is read as far as it goes and the values it does not carry keep their defaults, so
// a document written before a setting existed costs the player only that setting. A longer one is
// refused, since nothing can be said about bytes this build does not understand.
inline constexpr std::uint32_t kSettingsSchemaVersion = 1;
inline constexpr std::size_t   kSettingsImageBytes    = 3;

// Bring a scale into the range this build offers. Values below the floor come up to it and values
// above the ceiling come down to it; the range itself is what a build changes when it offers more.
[[nodiscard]] std::uint8_t clampWindowScale(int scale);

// Encode the settings into the image: the fullscreen flag as 0 or 1, then the scale, then the ramp.
[[nodiscard]] std::array<std::uint8_t, kSettingsImageBytes> encodeSettings(const Settings& settings);

// Decode an image into `settings`. Returns false and leaves `settings` untouched when the image is
// empty or longer than this build writes; true otherwise, with any byte the image does not carry
// left at its default. Any non-zero first byte means fullscreen, and both the scale and the ramp are
// clamped on the way in.
[[nodiscard]] bool decodeSettings(std::span<const std::uint8_t> image, Settings& settings);

// Persist the settings as document "settings" version 1. Returns whatever the atomic write reports.
bool saveSettings(const Settings& settings, retropp::SaveStore& store);

// Load the settings from the store. Absent document (ordinary first run) -> leave the defaults,
// return false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error,
// leave the defaults, leave the damaged file in place, return false.
bool loadSettings(retropp::SaveStore& store, Settings& settings);

}  // namespace kirpich
