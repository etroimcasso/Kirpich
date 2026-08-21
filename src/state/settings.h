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

    friend bool operator==(const Settings&, const Settings&) = default;
};

// The settings save document: schema version and the fixed image size. The name is spelled as a
// literal at the call sites, as the top-score document's is.
inline constexpr std::uint32_t kSettingsSchemaVersion = 1;
inline constexpr std::size_t   kSettingsImageBytes    = 2;

// Bring a scale into the range this build offers. Values below the floor come up to it and values
// above the ceiling come down to it; the range itself is what a build changes when it offers more.
[[nodiscard]] std::uint8_t clampWindowScale(int scale);

// Encode the settings into the two-byte image: the fullscreen flag as 0 or 1, then the scale.
[[nodiscard]] std::array<std::uint8_t, kSettingsImageBytes> encodeSettings(const Settings& settings);

// Decode an image into `settings`. Returns false and leaves `settings` untouched when the image is
// not exactly two bytes; true otherwise. Any non-zero first byte means fullscreen, and the scale is
// clamped on the way in.
[[nodiscard]] bool decodeSettings(std::span<const std::uint8_t> image, Settings& settings);

// Persist the settings as document "settings" version 1. Returns whatever the atomic write reports.
bool saveSettings(const Settings& settings, retropp::SaveStore& store);

// Load the settings from the store. Absent document (ordinary first run) -> leave the defaults,
// return false. Present and valid -> decode, return true. Corrupt or wrong length -> log an error,
// leave the defaults, leave the damaged file in place, return false.
bool loadSettings(retropp::SaveStore& store, Settings& settings);

}  // namespace kirpich
